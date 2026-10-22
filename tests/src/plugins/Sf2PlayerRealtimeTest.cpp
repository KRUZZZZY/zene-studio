/*
 * Sf2PlayerRealtimeTest.cpp - the SF2 player's audio paths must not allocate
 * and the synth must play the state the player asks for.
 *
 * The SF2 player hands a note from the note stage to the render stage. That
 * hand-off used to be a QVector of note handles behind a mutex: the first note
 * (or any note that grew the vector past its capacity) allocated on the audio
 * thread, and both the note stage and the render stage took a lock to do it.
 *
 * This test drives the product's own plugin module: it plays a note through
 * the instrument and counts the allocations the note path makes. The count
 * must be zero - the note's data comes from a preallocated slot and the note
 * event is published into a bounded, preallocated queue. The render path gets
 * the same treatment: the state the player asks for is sent to the synth on
 * the UI thread, so the render path must not allocate either.
 *
 * The count is taken with a replacement global operator new, the same
 * mechanism the plugin migration harness uses (tests/src/plugins/
 * PluginPortsHarness.h): the module's allocations are counted too, because the
 * executable's replacement is the one the loaded module resolves.
 */

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include <fluidsynth.h>
#include <QFile>
#include <QLibrary>
#include <QTemporaryDir>
#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Microtuner.h"
#include "Model.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"
#include "Song.h"

namespace
{

//! Allocations counted while `counting` is true.
std::atomic_bool counting{false};
std::atomic<std::uint64_t> allocationCount{0};

//! The frequency of the strongest period of the signal.
auto fundamentalFrequency(const std::vector<float>& signal, double sampleRate) -> double;

//! Renders a note on a synth and returns the frequency of the tone.
auto synthFrequency(fluid_synth_t* synth, int key, double sampleRate) -> double
{
	std::vector<float> mono;
	float left[16384] = {};
	float right[16384] = {};
	fluid_synth_noteon(synth, 0, key, 100);
	for (int round = 0; round < 4; ++round)
	{
		fluid_synth_write_float(synth, 16384, left, 0, 1, right, 0, 1);
		for (const auto sample : left) { mono.push_back(sample); }
	}
	fluid_synth_noteoff(synth, 0, key);
	return fundamentalFrequency(mono, sampleRate);
}

//! The SF2 module and an instrument of its own.
struct Sf2Fixture
{
	QLibrary library{QString::fromUtf8(SF2PLAYER_MODULE)};
	std::unique_ptr<lmms::InstrumentTrack> track;
	std::unique_ptr<lmms::Instrument> instrument;

	//! Loads the module and creates the instrument.
	//! @return false when this build has no SF2 player module.
	bool create()
	{
		if (QString::fromUtf8(SF2PLAYER_MODULE).isEmpty()) { return false; }
		library.setLoadHints(QLibrary::PreventUnloadHint);
		if (!library.load()) { return false; }

		auto entry = reinterpret_cast<lmms::Plugin* (*)(lmms::Model*, void*)>(
			library.resolve("lmms_plugin_main"));
		if (entry == nullptr) { return false; }

		track = std::make_unique<lmms::InstrumentTrack>(lmms::Engine::getSong());
		instrument.reset(static_cast<lmms::Instrument*>(entry(track.get(), nullptr)));
		return instrument != nullptr;
	}

	//! The FluidSynth synth the player uses.
	auto synth() const -> fluid_synth_t*
	{
		quintptr address = 0;
		if (!QMetaObject::invokeMethod(instrument.get(), "currentSynthAddress",
				Qt::DirectConnection, Q_RETURN_ARG(quintptr, address)))
		{
			return nullptr;
		}
		return reinterpret_cast<fluid_synth_t*>(address);
	}

	//! The SoundFont the synth plays, read through FluidSynth's own API.
	auto font() const -> fluid_sfont_t*
	{
		return synth() != nullptr ? fluid_synth_get_sfont( synth(), 0 ) : nullptr;
	}

	//! The plugin's own model with that display name.
	template<typename T> auto findModel(const char* name) const -> T*
	{
		const auto models = instrument->findChildren<T*>();
		for (auto* model : models)
		{
			if (model->displayName() == QString::fromUtf8(name)) { return model; }
		}
		return nullptr;
	}

	//! Plays a note and returns the frequency of the tone in the rendered audio.
	auto playedFrequency(std::span<lmms::SampleFrame> span, int key) const -> double
	{
		const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
		// The note lasts for the whole measurement, so the tone is there all
		// the time.
		const auto noteFrames = static_cast<lmms::f_cnt_t>(frames * 32);
		const lmms::Note note{lmms::TimePos{static_cast<lmms::tick_t>(noteFrames)},
			lmms::TimePos{0}, key};
		lmms::NotePlayHandle handle{track.get(), 0, noteFrames, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip};

		std::vector<float> mono;
		for (int period = 0; period < 32; ++period)
		{
			instrument->playNote(&handle, span);
			instrument->play(span);
			for (const auto& frame : span) { mono.push_back(frame[0]); }
		}
		return fundamentalFrequency(mono, lmms::Engine::audioEngine()->outputSampleRate());
	}
};


//! The frequency of the strongest period of the signal, found with an
//! autocorrelation. Deterministic: no timing, no randomness.
auto fundamentalFrequency(const std::vector<float>& signal, double sampleRate) -> double
{
	if (signal.size() < 4096) { return 0.; }

	// The search starts above the shortest lags: a tone that is mixed with
	// noise correlates with itself at every short lag.
	double best = 0.;
	std::size_t bestLag = 0;
	const std::size_t maxLag = std::min<std::size_t>(4000, signal.size() / 2);
	for (std::size_t lag = 20; lag < maxLag; ++lag)
	{
		double sum = 0.;
		double energyA = 0.;
		double energyB = 0.;
		for (std::size_t i = 0; i + lag < signal.size(); ++i)
		{
			sum += static_cast<double>(signal[i]) * signal[i + lag];
			energyA += static_cast<double>(signal[i]) * signal[i];
			energyB += static_cast<double>(signal[i + lag]) * signal[i + lag];
		}
		if (energyA <= 0. || energyB <= 0.) { continue; }
		const double correlation = sum / std::sqrt(energyA * energyB);
		if (correlation > best) { best = correlation; bestLag = lag; }
	}
	if (bestLag == 0 || best < 0.5) { return 0.; }
	return sampleRate / static_cast<double>(bestLag);
}

} // namespace

void* operator new(std::size_t size)
{
	if (counting.load(std::memory_order_relaxed))
	{
		allocationCount.fetch_add(1, std::memory_order_relaxed);
	}
	if (void* memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
	throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
	::operator delete(memory);
}

void operator delete[](void* memory) noexcept
{
	::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
	::operator delete(memory);
}


class Sf2PlayerRealtimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void notePathAllocatesNothing();
	void renderPathAppliesTheState();
	void disabledTuningKeepsEqualTemperament();
	void newSynthStartsWithTheRequestedEffectState();
	void everySynthHasItsOwnSettings();
	void moreNotesThanTheOldPoolGetSlots();
	void renderSurvivesAPendingReload();
	void playsWithARealSoundFont();
	void reloadKeepsTheSoundFontWhenTheFileIsGone();
	void everySynthHasItsOwnSoundFont();
	void theTuningOffsetMatchesFluidSynthsKeyTuning();
};

void Sf2PlayerRealtimeTest::playsWithARealSoundFont()
{
	if (!QFile::exists(QString::fromUtf8(SF2_TEST_SOUNDFONT)))
	{
		QSKIP("this tree has no SoundFont to play");
	}

	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// The player loads the soundfont off the audio thread and publishes the
	// synth it built.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "openFile", Qt::DirectConnection,
		Q_ARG(QString, QString::fromUtf8(SF2_TEST_SOUNDFONT))));

	fixture.instrument->play(span);

	const lmms::Note note{lmms::TimePos{0}, lmms::TimePos{0}, lmms::DefaultKey};
	lmms::NotePlayHandle handle{fixture.track.get(), 0, frames, note, nullptr, -1,
		lmms::NotePlayHandle::Origin::MidiClip};
	for (int i = 0; i < 20; ++i)
	{
		fixture.instrument->playNote(&handle, span);
		fixture.instrument->play(span);
	}
	double peak = 0;
	for (const auto& frame : buffer)
	{
		peak = std::max(peak, std::abs(static_cast<double>(frame[0])));
	}
	QVERIFY2(peak > 0.0001, "the player made no sound with a real soundfont");
};

void Sf2PlayerRealtimeTest::initTestCase()
{
	lmms::Engine::init(true);
	lmms::NotePlayHandleManager::init();
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "the engine did not initialise");
	// The test drives the instrument itself; the device must not render it too.
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();
}


void Sf2PlayerRealtimeTest::notePathAllocatesNothing()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	{
		// The note the player is asked to play. It is created before the count
		// is armed: the note handle itself is not part of the note path.
		const lmms::Note note{lmms::TimePos{0}, lmms::TimePos{0}, lmms::DefaultKey};
		lmms::NotePlayHandle handle{fixture.track.get(), 0, frames, note, nullptr, -1,
			lmms::NotePlayHandle::Origin::MidiClip};

		// The render path is warmed up first: the synth allocates its own
		// buffers when it is created and when it renders for the first time,
		// and that is not what this test is about.
		fixture.instrument->play(span);

		// From here on the note path must not allocate: the note's data comes
		// from a preallocated slot and the note event goes into a bounded
		// queue.
		allocationCount.store(0, std::memory_order_relaxed);
		counting.store(true, std::memory_order_relaxed);
		fixture.instrument->playNote(&handle, span);
		counting.store(false, std::memory_order_relaxed);

		const auto allocations = allocationCount.load(std::memory_order_relaxed);
		QVERIFY2(allocations == 0,
			qPrintable(QStringLiteral("the SF2 note path allocated %1 time(s) on the audio thread")
				.arg(allocations)));

		// The note is still played: the hand-off to the render stage happened.
		fixture.instrument->play(span);
	}
	// The note gave its data back here; the instrument may go now.
}



void Sf2PlayerRealtimeTest::renderPathAppliesTheState()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// The render thread is the only user of the synth, so the synth runs with
	// FluidSynth's thread-safe API switched off: no FluidSynth call on the
	// audio thread can take a mutex and wait for another thread.
	fixture.instrument->play(span);
	auto* synth = fixture.synth();
	QVERIFY(synth != nullptr);
	auto* settings = fluid_synth_get_settings(synth);
	QVERIFY(settings != nullptr);
	int threadSafeApi = 1;
	QCOMPARE(fluid_settings_getint(settings, "synth.threadsafe-api", &threadSafeApi), FLUID_OK);
	QCOMPARE(threadSafeApi, 0);

	// The UI asks for another gain. The request is published, not applied: the
	// UI thread must not call into the synth the render thread is playing.
	auto* gainModel = fixture.findModel<lmms::FloatModel>("Gain");
	QVERIFY(gainModel != nullptr);
	const float before = fluid_synth_get_gain(synth);
	gainModel->setValue(0.5f);
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updateGain", Qt::DirectConnection));
	QCOMPARE(fluid_synth_get_gain(synth), before);

	// The render thread applies the request: the state reaches the synth with
	// the next period, without the audio thread waiting for the UI thread.
	fixture.instrument->play(span);
	QVERIFY2(std::abs(fluid_synth_get_gain(synth) - 0.5f) < 0.001f,
		qPrintable(QStringLiteral("the synth plays with the gain %1, the player asks for 0.5")
			.arg(fluid_synth_get_gain(synth))));

	// The render path allocates nothing: the state it sends is a number, and
	// the synth's own buffers were allocated when it was created.
	allocationCount.store(0, std::memory_order_relaxed);
	counting.store(true, std::memory_order_relaxed);
	fixture.instrument->play(span);
	counting.store(false, std::memory_order_relaxed);

	const auto allocations = allocationCount.load(std::memory_order_relaxed);
	QVERIFY2(allocations == 0,
		qPrintable(QStringLiteral("the SF2 render path allocated %1 time(s) on the audio thread")
			.arg(allocations)));
}



void Sf2PlayerRealtimeTest::disabledTuningKeepsEqualTemperament()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	if (!QFile::exists(QString::fromUtf8(SF2_TEST_SOUNDFONT)))
	{
		QSKIP("this tree has no SoundFont to play");
	}

	// The microtuner is off by default: the notes play the normal equal
	// temperament.
	QVERIFY(!fixture.track->microtuner()->enabledModel()->value());

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "openFile", Qt::DirectConnection,
		Q_ARG(QString, QString::fromUtf8(SF2_TEST_SOUNDFONT))));
	fixture.instrument->play(span);

	// The soundfont plays a fixed tone. With the microtuner off the note plays
	// at that tone; a tuning table that was applied as a pitch (the table of
	// cents from MIDI note 0) would shift the note down by whole octaves.
	const double frequency = fixture.playedFrequency(span, 60);
	QVERIFY2(std::abs(frequency - 50.0) < 1.0,
		qPrintable(QStringLiteral("the note plays at %1 Hz, the equal temperament wants 50 Hz")
			.arg(frequency)));
}



void Sf2PlayerRealtimeTest::newSynthStartsWithTheRequestedEffectState()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};
	// The render thread adopts the synth with its first period.
	fixture.instrument->play(span);

	auto* synth = fixture.synth();
	QVERIFY(synth != nullptr);
	auto* settings = fluid_synth_get_settings(synth);
	QVERIFY(settings != nullptr);

	// The player's models have the effects off, while FluidSynth's own
	// defaults have them on. A new synth has to start with the state the
	// models ask for: FluidSynth reads these two settings when it is created.
	int reverbActive = 1;
	int chorusActive = 1;
	QCOMPARE(fluid_settings_getint(settings, "synth.reverb.active", &reverbActive), FLUID_OK);
	QCOMPARE(fluid_settings_getint(settings, "synth.chorus.active", &chorusActive), FLUID_OK);
	QCOMPARE(reverbActive, 0);
	QCOMPARE(chorusActive, 0);
}



void Sf2PlayerRealtimeTest::everySynthHasItsOwnSettings()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};
	// The render thread adopts the synth with its first period.
	fixture.instrument->play(span);

	auto* firstSynth = fixture.synth();
	QVERIFY(firstSynth != nullptr);
	auto* firstSettings = fluid_synth_get_settings(firstSynth);
	QVERIFY(firstSettings != nullptr);

	// A reload builds a second synth. FluidSynth allows one synth per settings
	// object, so the second synth has to have a settings object of its own.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "reloadSynth", Qt::DirectConnection));
	// The render thread adopts the replacement with its next period.
	fixture.instrument->play(span);

	auto* secondSynth = fixture.synth();
	QVERIFY(secondSynth != nullptr);
	QVERIFY(secondSynth != firstSynth);
	auto* secondSettings = fluid_synth_get_settings(secondSynth);
	QVERIFY(secondSettings != nullptr);
	QVERIFY2(secondSettings != firstSettings,
		"the two synths share one FluidSynth settings object");
}



void Sf2PlayerRealtimeTest::moreNotesThanTheOldPoolGetSlots()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// FluidSynth's default polyphony is 256 voices, so a full load of notes
	// has to fit into the player's pool of plugin data slots.
	constexpr int noteCount = 200;
	std::vector<lmms::NotePlayHandle*> handles;
	handles.reserve(noteCount);
	for (int i = 0; i < noteCount; ++i)
	{
		const lmms::Note note{lmms::TimePos{0}, lmms::TimePos{0}, lmms::DefaultKey};
		handles.push_back(lmms::NotePlayHandleManager::acquire(fixture.track.get(), 0, frames,
			note, nullptr, -1, lmms::NotePlayHandle::Origin::MidiClip));
	}

	for (auto* handle : handles)
	{
		fixture.instrument->playNote(handle, span);
	}

	int withoutData = 0;
	for (auto* handle : handles)
	{
		if (handle->m_pluginData == nullptr) { ++withoutData; }
	}
	QVERIFY2(withoutData == 0,
		qPrintable(QStringLiteral("%1 of %2 notes did not get a plugin data slot")
			.arg(withoutData).arg(noteCount)));

	// The notes give their plugin data back.
	for (auto* handle : handles)
	{
		lmms::NotePlayHandleManager::release(handle);
	}
}



void Sf2PlayerRealtimeTest::renderSurvivesAPendingReload()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// The UI publishes a replacement synth; the render thread adopts it on the
	// next period instead of waiting for a mutex the UI might hold.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "reloadSynth", Qt::DirectConnection));
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updateGain", Qt::DirectConnection));
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updateTuning", Qt::DirectConnection));
	fixture.instrument->play(span);

	// The old synth is retired for the UI to delete, and the player keeps
	// rendering: nothing on the audio path blocked or crashed.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "reloadSynth", Qt::DirectConnection));
	fixture.instrument->play(span);
}

void Sf2PlayerRealtimeTest::reloadKeepsTheSoundFontWhenTheFileIsGone()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	if (!QFile::exists(QString::fromUtf8(SF2_TEST_SOUNDFONT)))
	{
		QSKIP("this tree has no SoundFont to play");
	}

	// A copy of the soundfont: the test removes that one, not the file in the
	// tree.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath("reload-test.sf2");
	QVERIFY(QFile::copy(QString::fromUtf8(SF2_TEST_SOUNDFONT), path));

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "openFile", Qt::DirectConnection,
		Q_ARG(QString, path)));
	fixture.instrument->play(span);
	QVERIFY(fixture.playedFrequency(span, 60) > 40.);

	// The file behind the font is gone. A reload must not lose the font: the
	// player keeps the synth that still plays it, and the preset list with it.
	QVERIFY(QFile::remove(path));
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "reloadSynth", Qt::DirectConnection));
	fixture.instrument->play(span);

	const double frequency = fixture.playedFrequency(span, 60);
	QVERIFY2(frequency > 40.,
		qPrintable(QStringLiteral("the player lost the soundfont: the note plays at %1 Hz")
			.arg(frequency)));

	QString patchName;
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "getCurrentPatchName",
		Qt::DirectConnection, Q_RETURN_ARG(QString, patchName)));
	QVERIFY2(!patchName.isEmpty(), "the preset list was cleared by the reload");
}

void Sf2PlayerRealtimeTest::everySynthHasItsOwnSoundFont()
{
	if (!QFile::exists(QString::fromUtf8(SF2_TEST_SOUNDFONT))) { QSKIP("this tree has no SoundFont to play"); }

	Sf2Fixture fixture;
	if (!fixture.create()) { QSKIP("this build has no SF2 player module (no FluidSynth)"); }

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "openFile", Qt::DirectConnection,
		Q_ARG(QString, QString::fromUtf8(SF2_TEST_SOUNDFONT))));
	fixture.instrument->play(span);

	// The font the first synth plays.
	auto* firstFont = fixture.font();
	QVERIFY2(firstFont != nullptr, "the player loaded no soundfont");

	// A reload builds a second synth while the first one is still alive: both
	// play the font at the same time.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "reloadSynth", Qt::DirectConnection));
	fixture.instrument->play(span);
	auto* secondFont = fixture.font();
	QVERIFY2(secondFont != nullptr, "the reloaded synth plays no soundfont");

	// FluidSynth hands the ownership of a font to the synth it is added to and
	// frees the font with that synth. Two synths that share one font object
	// free it twice, so every synth needs a font instance of its own.
	QVERIFY2(secondFont != firstFont,
		"the two synths share one SoundFont object: deleting one of them leaves the other with a freed font");

	// The retired synth is deleted with the instrument, and the font keeps playing.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updatePatch", Qt::DirectConnection));
	fixture.instrument->play(span);
	QVERIFY2(fixture.playedFrequency(span, 60) > 40., "the reloaded synth does not play the font");

	QString patchName;
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "getCurrentPatchName",
		Qt::DirectConnection, Q_RETURN_ARG(QString, patchName)));
	QVERIFY2(!patchName.isEmpty(), "the preset list was cleared by the reload");
	// The instrument is destroyed here, and the retired synth goes with it.
}

void Sf2PlayerRealtimeTest::theTuningOffsetMatchesFluidSynthsKeyTuning()
{
	if (!QFile::exists(QString::fromUtf8(SF2_TEST_SOUNDFONT)))
	{
		QSKIP("this tree has no SoundFont to play");
	}

	const double sampleRate = 44100.;

	// FluidSynth's key tuning shifts a note by the difference between its
	// pitch and the equal temperament. The player cannot use it: the tuning
	// activation allocates a tuning object and cannot run on the audio thread.
	// The player shifts the note with the voice's coarse tune instead, and
	// this case shows that both shifts are the same.
	auto makeSynth = [](fluid_synth_t** synthOut) -> bool
	{
		auto* settings = new_fluid_settings();
		auto* synth = new_fluid_synth(settings);
		const int fontId = fluid_synth_sfload(synth, SF2_TEST_SOUNDFONT, 1);
		if (fontId < 0) { return false; }
		fluid_synth_program_select(synth, 0, fontId, 0, 0);
		*synthOut = synth;
		return true;
	};

	fluid_synth_t* tuned = nullptr;
	QVERIFY(makeSynth(&tuned));
	double pitch[128] = {};
	for (int i = 0; i < 128; ++i) { pitch[i] = i * 100.; }
	pitch[60] = 6100.; // +100 cents on the key that is played
	fluid_synth_activate_key_tuning(tuned, 0, 0, "", pitch, true);
	for (int channel = 0; channel < 16; ++channel)
	{
		fluid_synth_activate_tuning(tuned, channel, 0, 0, true);
	}
	const double tunedFrequency = synthFrequency(tuned, 60, sampleRate);

	fluid_synth_t* coarse = nullptr;
	QVERIFY(makeSynth(&coarse));
	// A coarse tune of 1.0 is 100 cents: the same shift.
	fluid_synth_set_gen(coarse, 0, GEN_COARSETUNE, 1.0f);
	const double coarseFrequency = synthFrequency(coarse, 60, sampleRate);

	QVERIFY2(tunedFrequency > 0. && coarseFrequency > 0.,
		qPrintable(QStringLiteral("the soundfont did not play: %1 Hz and %2 Hz")
			.arg(tunedFrequency).arg(coarseFrequency)));
	QVERIFY2(std::abs(tunedFrequency - coarseFrequency) < 0.5,
		qPrintable(QStringLiteral("FluidSynth's key tuning plays at %1 Hz, the coarse tune at %2 Hz")
			.arg(tunedFrequency).arg(coarseFrequency)));
}

QTEST_MAIN(Sf2PlayerRealtimeTest)

#include "Sf2PlayerRealtimeTest.moc"
