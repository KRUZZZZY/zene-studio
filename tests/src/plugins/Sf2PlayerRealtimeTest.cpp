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
#include <QLibrary>
#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Microtuner.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "SampleFrame.h"
#include "Song.h"

namespace
{

//! Allocations counted while `counting` is true.
std::atomic_bool counting{false};
std::atomic<std::uint64_t> allocationCount{0};

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

	//! The FluidSynth synth the player owns.
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
};

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
	void renderPathAppliesNoState();
	void disabledTuningKeepsEqualTemperament();
	void newSynthStartsWithTheRequestedEffectState();
	void everySynthHasItsOwnSettings();
	void moreNotesThanTheOldPoolGetSlots();
	void renderSurvivesAPendingReload();
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



void Sf2PlayerRealtimeTest::renderPathAppliesNoState()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// The state the player asks for is sent to the synth by the UI thread: the
	// tuning activation allocates a tuning object inside FluidSynth and takes
	// FluidSynth's API mutex, so it cannot run on the audio thread. The synth
	// therefore knows the state before the first render - the render thread
	// does not configure it.
	auto* synth = fixture.synth();
	QVERIFY(synth != nullptr);

	// The player's gain model asks for 1.0, FluidSynth's own default is 0.2:
	// the synth has the player's state before anything was rendered.
	const float gain = fluid_synth_get_gain(synth);
	QVERIFY2(std::abs(gain - 1.0f) < 0.001f,
		qPrintable(QStringLiteral("the synth plays with the gain %1, the player asks for 1.0")
			.arg(gain)));

	char name[64] = {};
	double pitch[128] = {};
	QCOMPARE(fluid_synth_tuning_dump(synth, 0, 0, name, sizeof(name), pitch), FLUID_OK);

	// The UI asks for a new state. Sending it to the synth is the UI thread's
	// job, so the render path that follows must not allocate.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updateTuning", Qt::DirectConnection));
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updateGain", Qt::DirectConnection));
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "updateReverbOn", Qt::DirectConnection));

	// The render path is warmed up first: the synth allocates its own buffers
	// when it is created and when it renders for the first time.
	fixture.instrument->play(span);

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

	const auto frames = lmms::Engine::audioEngine()->framesPerPeriod();
	std::vector<lmms::SampleFrame> buffer(static_cast<std::size_t>(frames));
	const std::span<lmms::SampleFrame> span{buffer};

	// The microtuner is off by default: the player has to ask FluidSynth for
	// the normal equal temperament.
	QVERIFY(!fixture.track->microtuner()->enabledModel()->value());

	// Render a period, so that the state the player asks for has reached the
	// synth whichever thread does that.
	fixture.instrument->play(span);

	auto* synth = fixture.synth();
	QVERIFY(synth != nullptr);

	// FluidSynth reads the tuning table as cents from MIDI note 0. The equal
	// temperament is 100 cents per key; an all-zero table would pin every key
	// to MIDI note 0.
	char name[64] = {};
	double pitch[128] = {};
	QCOMPARE(fluid_synth_tuning_dump(synth, 0, 0, name, sizeof(name), pitch), FLUID_OK);

	for (int key = 0; key < 128; ++key)
	{
		QVERIFY2(std::abs(pitch[key] - key * 100.0) < 0.001,
			qPrintable(QStringLiteral("key %1 is tuned to %2 cents, the equal temperament wants %3")
				.arg(key).arg(pitch[key]).arg(key * 100.0)));
	}
}



void Sf2PlayerRealtimeTest::newSynthStartsWithTheRequestedEffectState()
{
	Sf2Fixture fixture;
	if (!fixture.create())
	{
		QSKIP("this build has no SF2 player module (no FluidSynth)");
	}

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

	auto* firstSynth = fixture.synth();
	QVERIFY(firstSynth != nullptr);
	auto* firstSettings = fluid_synth_get_settings(firstSynth);
	QVERIFY(firstSettings != nullptr);

	// A reload builds a second synth. FluidSynth allows one synth per settings
	// object, so the second synth has to have a settings object of its own.
	QVERIFY(QMetaObject::invokeMethod(fixture.instrument.get(), "reloadSynth", Qt::DirectConnection));

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

QTEST_MAIN(Sf2PlayerRealtimeTest)

#include "Sf2PlayerRealtimeTest.moc"
