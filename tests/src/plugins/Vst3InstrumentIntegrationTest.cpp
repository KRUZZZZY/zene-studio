/*
 * Vst3InstrumentIntegrationTest.cpp - end to end: a MIDI clip drives a VST3
 * instrument, and its state survives the project file
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*
 * WHAT THIS PROVES, AND HOW IT GETS THERE
 * ---------------------------------------
 * The subject is the fixture (tests/data/vst3-test-instrument/) driven through
 * the product, not through the host in isolation:
 *
 *   MidiClip::addNote() -> NotePlayHandle -> InstrumentTrack::processOutEvent()
 *     -> Instrument::handleMidiEvent() -> HostedPlugin's MIDI queue
 *     -> ProcessData::inputEvents -> the plug-in -> the track's audio bus
 *
 * The instrument itself is the real vst3instrument MODULE, loaded through the
 * plug-in entry point the LMMS plugin factory uses (QLibrary -> "lmms_plugin_main"
 * with the sub-plugin Key), so the module boundary, the descriptor, the key
 * attributes and the constructor are all the shipped ones.
 *
 * What this does NOT prove: that PluginFactory's *descriptor lookup* finds the
 * module (it needs an installed plugin directory). The lookup is pre-existing
 * machinery shared with the VST3 effect host; the module loading and the entry
 * point below are the same two steps it performs. Recorded, not glossed, in
 * docs/VST3-INSTRUMENT-HOSTING.md.
 */

#include <QtTest>

#include <QApplication>
#include <QJsonObject>
#include <QWidget>
#include <QToolButton>

#include "ControlVocabulary.h"

#include "ControlRegistry.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QLibrary>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "AudioBus.h"
#include "BufferManager.h"
#include "DeviceOptions.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentView.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "PluginView.h"
#include "Knob.h"
#include "Vst3Instrument.h"
#include "Song.h"
#include "TimePos.h"
#include "Vst3Instrument.h"

#ifndef VST3_INSTRUMENT_PLUGIN_PATH
#define VST3_INSTRUMENT_PLUGIN_PATH ""
#endif
#ifndef VST3_INSTRUMENT_PLUGIN_DIR
#define VST3_INSTRUMENT_PLUGIN_DIR ""
#endif
#ifndef VST3_TEST_INSTRUMENT_BUNDLE
#define VST3_TEST_INSTRUMENT_BUNDLE ""
#endif

namespace lmms
{

namespace
{

constexpr f_cnt_t BlockFrames = 256;
constexpr int Blocks = 8;
//! The fixture renders this level while a key is held
constexpr float FixtureLevel = 0.5f;
//! The fixture's single parameter ("Level"): normalized value == level
constexpr float WrittenLevel = 0.75f;

using PluginEntryFn = Plugin* (*)(Model* parent, void* data);

auto rms(const std::vector<float>& data) -> double
{
	double sum = 0.0;
	for (const auto value : data) { sum += static_cast<double>(value) * value; }
	return data.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(data.size()));
}

//! One value out of the plug-in key the project writer produced. A Key
//! serialises its attributes as <attribute name=... value=.../> child
//! elements (src/core/Plugin.cpp:293-308), not as XML attributes.
auto keyAttribute(const QDomElement& keyElement, const QString& name) -> QString
{
	for (auto attribute = keyElement.firstChildElement(QStringLiteral("attribute"));
		!attribute.isNull(); attribute = attribute.nextSiblingElement(QStringLiteral("attribute")))
	{
		if (attribute.attribute(QStringLiteral("name")) == name)
		{
			return attribute.attribute(QStringLiteral("value"));
		}
	}
	return {};
}

auto maxDifference(const std::vector<float>& a, const std::vector<float>& b) -> float
{
	if (a.size() != b.size()) { return -1.0f; }
	float worst = 0.0f;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		worst = std::max(worst, std::abs(a[i] - b[i]));
	}
	return worst;
}

//! The base instrument view and nothing else: constructing it runs the exact
//! entry point under test (InstrumentView's ctor -> setModel -> the window
//! icon) without the plug-in's own widget grid. That grid is deliberately not
//! built here: a Knob needs getGUI() to be a live GuiApplication, because
//! SimpleTextFloat::SimpleTextFloat() is
//! QWidget(getGUI()->mainWindow(), Qt::ToolTip), and this binary initialises a
//! render-only Engine (no MainWindow). The grid itself is verified against the
//! running product under Xvfb instead - docs/INSTRUMENT-VIEW-SAFETY.md, "wall
//! 2" (§5) and the product run (§3).
class BareInstrumentView : public gui::InstrumentView
{
public:
	BareInstrumentView(Instrument* instrument, QWidget* parent) :
		gui::InstrumentView(instrument, parent)
	{
	}
};

//! The host's message handler while ScopedWarningCounter is installed.
QtMessageHandler g_previousHandler = nullptr;
//! How many "re-prepare failed" warnings the host has printed.
int g_reprepareWarnings = 0;

//! Counts the host's failed re-prepare warnings and passes every other
//! message on, so a failure elsewhere in the test is still reported.
void countReprepareWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	if (message.contains(QStringLiteral("re-prepare failed")))
	{
		++g_reprepareWarnings;
		return;
	}
	if (g_previousHandler != nullptr) { g_previousHandler(type, context, message); }
}

//! Counts the host's failed re-prepare warnings for the lifetime of the scope.
class ScopedWarningCounter
{
public:
	ScopedWarningCounter()
	{
		g_reprepareWarnings = 0;
		g_previousHandler = qInstallMessageHandler(countReprepareWarnings);
	}

	~ScopedWarningCounter()
	{
		qInstallMessageHandler(g_previousHandler);
		g_previousHandler = nullptr;
	}

	auto count() const -> int { return g_reprepareWarnings; }

private:
	QtMessageHandler m_previous = nullptr;
};

//! Sets an environment variable for the lifetime of the scope and puts the
//! previous value back (the fixture reads it while it prepares).
class ScopedEnv
{
public:
	ScopedEnv(const char* name, const char* value) :
		m_name(name),
		m_previous(qgetenv(name))
	{
		qputenv(name, value);
	}

	~ScopedEnv()
	{
		if (m_previous.isNull()) { qunsetenv(m_name.constData()); }
		else { qputenv(m_name.constData(), m_previous); }
	}

private:
	QByteArray m_name;
	QByteArray m_previous;
};

} // namespace

class Vst3InstrumentIntegrationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void testTheInstalledInstrumentLoadsAndIsMidiBased();
	void testUnpreparedInstrumentClearsAudioPortsOutput();
	void testAMidiClipDrivesAudioWithSampleAccuracy();
	void testAnUnpreparedInstrumentClearsItsOutput();
	void testAFailedReprepareIsNotRetriedForEver();
	void testDistinctInputGivesDistinctOutput();
	void testTheLevelParameterReachesTheInstrument();
	void testStateSurvivesTheProjectFile();
	void testParameterFallbackWithoutStateChunks();
	void testAGuiLessInstrumentExposesItsParameters();
	void testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow();
	void testTheInstrumentsOwnEditorOpensInItsOwnWindow();
	void testAnEffectsEditorOpensAtItsDeviceScale();
	void testReadOnlyParametersAreNotKnobs();
	void testReadOnlyParametersAreNotEffectKnobs();
	void testReadOnlyParametersAreReportedAndRefusedOverControl();

private:
	//! The real module, loaded exactly as the plugin factory loads it.
	struct LoadedInstrument
	{
		QLibrary library;
		Plugin::Descriptor* descriptor = nullptr;
		PluginEntryFn entry = nullptr;
	};

	auto openModule() -> bool;
	auto makeTrack() -> std::unique_ptr<InstrumentTrack>;
	auto makeInstrument(InstrumentTrack* track) -> Instrument*;
	//! Renders `note` through the track's own MIDI path, the way
	//! InstrumentPlayHandle does: the track's NotePlayHandles first (that is
	//! what emits the MIDI), then the instrument's block.
	auto renderNote(InstrumentTrack* track, Instrument* instrument, const Note& note,
		f_cnt_t noteOffset, f_cnt_t noteFrames) -> std::vector<float>;

	LoadedInstrument m_module;
};

auto Vst3InstrumentIntegrationTest::openModule() -> bool
{
	const QString path = QStringLiteral(VST3_INSTRUMENT_PLUGIN_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path)) { return false; }

	for (auto* descriptor : PluginFactory::instance()->descriptors())
	{
		if (QString::fromLatin1(descriptor->name) == QStringLiteral("vst3instrument"))
		{
			m_module.descriptor = descriptor;
			break;
		}
	}
	if (m_module.descriptor == nullptr)
	{
		qWarning("vst3instrument: the plug-in was not discovered in %s - check that %s "
			"exists and exports vst3instrument_plugin_descriptor + lmms_plugin_main",
			qPrintable(path), qPrintable(path));
		return false;
	}
	return true;
}

void Vst3InstrumentIntegrationTest::initTestCase()
{
	if (QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE).isEmpty() ||
		!QFileInfo::exists(QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE)))
	{
		QSKIP("no VST3 instrument fixture available (configure with "
			"-DWANT_VST3_TEST_INSTRUMENT=ON and a VST3 SDK checkout)");
	}

	// The plug-in must be discoverable BEFORE the plug-in factory is built:
	// PluginFactory scans its search paths once, when Engine::init() first
	// asks for it (src/core/PluginFactory.cpp:74-95). LMMS_PLUGIN_DIR is one
	// of its search paths, so pointing it at the build's plug-in directory is
	// exactly how an installed LMMS finds its plug-ins - and it is what makes
	// the real load path (Instrument::instantiate -> library -> entry point)
	// available to this test.
	const QString pluginDir = QStringLiteral(VST3_INSTRUMENT_PLUGIN_DIR);
	if (!pluginDir.isEmpty())
	{
		qputenv("LMMS_PLUGIN_DIR", pluginDir.toLocal8Bit());
	}

	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	BufferManager::init(BlockFrames);
	NotePlayHandleManager::init();
	qInfo("engine: sampleRate=%u framesPerPeriod=%d",
		static_cast<unsigned>(Engine::audioEngine()->outputSampleRate()),
		static_cast<int>(Engine::audioEngine()->framesPerPeriod()));

	if (!openModule())
	{
		QSKIP("the vst3instrument module is not available (build it with "
			"-DWANT_VST3=ON and a VST3 SDK checkout)");
	}
}

void Vst3InstrumentIntegrationTest::cleanupTestCase()
{
	Engine::destroy();
}

auto Vst3InstrumentIntegrationTest::makeTrack() -> std::unique_ptr<InstrumentTrack>
{
	return std::make_unique<InstrumentTrack>(Engine::getSong());
}

auto Vst3InstrumentIntegrationTest::makeInstrument(InstrumentTrack* track) -> Instrument*
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.desc = m_module.descriptor;
	key.name = QStringLiteral("Zene VST3 Test Instrument");
	key.attributes[QStringLiteral("file")] = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
	key.attributes[QStringLiteral("class")] = QStringLiteral("Zene VST3 Test Instrument");

	// InstrumentTrack::loadInstrument() is the product's path: it asks the
	// plug-in factory for the module, which dlopens it and calls its
	// lmms_plugin_main entry point. Going through it (rather than constructing
	// the class directly) is what makes the track's own MIDI delivery live -
	// InstrumentTrack::processOutEvent() returns immediately when the track has
	// no instrument (src/tracks/InstrumentTrack.cpp:467-473).
	track->loadInstrument(QStringLiteral("vst3instrument"), &key);
	return track->instrument();
}

auto Vst3InstrumentIntegrationTest::renderNote(InstrumentTrack* track,
	Instrument* instrument, const Note& note, f_cnt_t noteOffset,
	f_cnt_t noteFrames) -> std::vector<float>
{
	NotePlayHandle nph{track, noteOffset, noteFrames, note, nullptr, -1,
		NotePlayHandle::Origin::MidiClip};

	std::vector<SampleFrame> storage(BlockFrames);
	std::vector<float> out;
	out.reserve(static_cast<std::size_t>(Blocks) * BlockFrames);

	for (int block = 0; block < Blocks; ++block)
	{
		std::fill(storage.begin(), storage.end(), SampleFrame{0.0f, 0.0f});
		const std::span<SampleFrame> span{storage};
		// InstrumentPlayHandle::play() order: the track's note handles first
		// (this is what emits MidiNoteOn/MidiNoteOff at their sample offsets),
		// then the instrument renders the block.
		nph.play(span);
		instrument->play(span);
		for (f_cnt_t f = 0; f < BlockFrames; ++f) { out.push_back(storage[f].left()); }
	}
	return out;
}

void Vst3InstrumentIntegrationTest::testTheInstalledInstrumentLoadsAndIsMidiBased()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY2(instrument != nullptr, "the module entry point returned no instrument");

	// The flags are what make the engine treat it as MIDI-driven rather than
	// as a NotePlayHandle-per-voice instrument.
	QCOMPARE(instrument->isMidiBased(), true);
	QCOMPARE(instrument->isSingleStreamed(), true);

	// The VST3 parameters are surfaced as LMMS models - the fixture has two:
	// the automatable "Level" and the read-only "Meter".
	QCOMPARE(instrument->parameterCount(), 2);
	QCOMPARE(instrument->parameterName(0), QStringLiteral("Level"));
	qInfo("instrument: %s, %d parameter(s), first is \"%s\"",
		qPrintable(instrument->displayName()), instrument->parameterCount(),
		qPrintable(instrument->parameterName(0)));

}

void Vst3InstrumentIntegrationTest::testUnpreparedInstrumentClearsAudioPortsOutput()
{
	auto track = makeTrack();
	auto* instrument = static_cast<Vst3Instrument*>(makeInstrument(track.get()));
	QVERIFY(instrument != nullptr);
	QVERIFY(instrument->vst3Plugin()->isPrepared());

	const Note held{TimePos::fromFrames(Blocks * BlockFrames * 2, 1.0),
		TimePos(0), 69, DefaultVolume};
	const auto primed = renderNote(track.get(), instrument, held, 0,
		Blocks * BlockFrames * 2);
	QVERIFY(std::any_of(primed.begin(), primed.end(),
		[](float sample) { return sample == FixtureLevel; }));

	instrument->vst3Plugin()->release();
	QVERIFY(!instrument->vst3Plugin()->isPrepared());

	constexpr float Stale = -1.0f;
	std::vector<SampleFrame> output(BlockFrames, SampleFrame{Stale, Stale});
	instrument->play(std::span<SampleFrame>{output});
	QVERIFY2(std::all_of(output.begin(), output.end(), [](const SampleFrame& frame) {
		return frame.left() == 0.0f && frame.right() == 0.0f;
	}), "the unprepared instrument routed stale processor samples to the track");
}

void Vst3InstrumentIntegrationTest::testAMidiClipDrivesAudioWithSampleAccuracy()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	// A real MidiClip on a real InstrumentTrack, with a real note: the note
	// the render below plays is the clip's own Note object. Clip's constructor
	// already registers the clip with its track (src/core/Clip.cpp:49-52), so
	// addClip() must NOT be called again - doing it twice puts the same
	// pointer in the track's clip vector twice and the track then deletes it
	// twice. Quantization is off because it consults the piano roll window,
	// which a headless test has no use for and no access to.
	auto* clip = new MidiClip(track.get());
	auto* note = clip->addNote(Note(TimePos::fromFrames(Blocks * BlockFrames, 1.0),
		TimePos(0), 69, DefaultVolume), /*_quant_pos=*/false);
	QVERIFY(note != nullptr);
	QCOMPARE(int(clip->notes().size()), 1);
	QCOMPARE(int(track->getClips().size()), 1);

	qInfo("instrument: midiBased=%d singleStreamed=%d portsModel=%p parameters=%d",
		int(instrument->isMidiBased()), int(instrument->isSingleStreamed()),
		static_cast<const void*>(instrument->audioPortsModel()),
		instrument->parameterCount());

	const auto rendered = renderNote(track.get(), instrument, *note, 0,
		Blocks * BlockFrames);

	qInfo("clip-driven render: s0=%.4f s127=%.4f s128=%.4f s255=%.4f rms=%.6f",
		rendered[0], rendered[127], rendered[128], rendered[255], rms(rendered));

	QVERIFY2(rms(rendered) > 0.3,
		"a MIDI clip through the fixture produced (almost) no audio");
	QVERIFY(std::all_of(rendered.begin(), rendered.end(),
		[](float v) { return v == FixtureLevel; }));

	// Sample accuracy through the WHOLE path: give the same clip note a start
	// offset of 128 frames and assert nothing sounds before it. This is the
	// fixture probe's contract (an event at frame N affects exactly frame N)
	// carried through the track's MIDI path rather than through the SDK.
	constexpr f_cnt_t Offset = 128;
	const auto delayed = renderNote(track.get(), instrument, *note, Offset,
		Blocks * BlockFrames);
	QVERIFY(std::all_of(delayed.begin(), delayed.begin() + Offset,
		[](float v) { return v == 0.0f; }));
	QCOMPARE(delayed[Offset], FixtureLevel);
	QCOMPARE(delayed[Offset - 1], 0.0f);
	QVERIFY2(maxDifference(rendered, delayed) >= FixtureLevel,
		"the note offset made no difference to the render");

	qInfo("clip -> track -> instrument: RMS=%.6f, sample[%d]=%.6f, sample[%d]=%.6f",
		rms(rendered), int(Offset - 1), delayed[Offset - 1], int(Offset),
		delayed[Offset]);

}

void Vst3InstrumentIntegrationTest::testAnUnpreparedInstrumentClearsItsOutput()
{
	auto track = makeTrack();
	auto* instrument = static_cast<Vst3Instrument*>(makeInstrument(track.get()));
	QVERIFY(instrument != nullptr);
	QVERIFY(instrument->vst3Plugin()->isPrepared());

	const Note held{TimePos::fromFrames(Blocks * BlockFrames, 1.0), TimePos(0), 60,
		DefaultVolume};
	const auto previousBlock = renderNote(track.get(), instrument, held, 0,
		Blocks * BlockFrames);
	QVERIFY(std::all_of(previousBlock.begin(), previousBlock.end(),
		[](float value) { return value == FixtureLevel; }));

	instrument->vst3Plugin()->release();
	QVERIFY(!instrument->vst3Plugin()->isPrepared());

	std::vector<SampleFrame> output(BlockFrames, SampleFrame{0.25f, -0.75f});
	instrument->play(std::span<SampleFrame>{output});
	QVERIFY(instrument->needsReprepare());
	QVERIFY(std::all_of(output.begin(), output.end(), [](const SampleFrame& frame) {
		return frame.left() == 0.0f && frame.right() == 0.0f;
	}));
}

void Vst3InstrumentIntegrationTest::testAFailedReprepareIsNotRetriedForEver()
{
	auto track = makeTrack();
	auto* instrument = static_cast<Vst3Instrument*>(makeInstrument(track.get()));
	QVERIFY(instrument != nullptr);
	QVERIFY(instrument->vst3Plugin()->isPrepared());

	// The plug-in is loaded, but from now on it REFUSES to be set up: the
	// "unsupported sample rate" case. Every prepare the host attempts fails.
	ScopedEnv failPrepare("VST3_TEST_INSTRUMENT_FAIL_PREPARE", "1");
	ScopedWarningCounter warnings;

	instrument->vst3Plugin()->release();
	QVERIFY(!instrument->vst3Plugin()->isPrepared());

	// One audio block per cycle: the unprepared plug-in makes the audio thread
	// record the re-prepare request, and the GUI thread's poll() services it.
	// poll() is called directly (not by its 100 ms timer), so the test is
	// deterministic: no waiting, no sleeping, no race with the timer.
	constexpr int Cycles = 8;
	std::vector<SampleFrame> output(BlockFrames, SampleFrame{0.0f, 0.0f});
	for (int cycle = 0; cycle < Cycles; ++cycle)
	{
		instrument->play(std::span<SampleFrame>{output});
		QVERIFY(instrument->needsReprepare());
		QVERIFY(QMetaObject::invokeMethod(instrument, "poll", Qt::DirectConnection));
	}

	// The host must not set the plug-in up again and again for the SAME
	// configuration: the failure is reported a few times, not once per poll.
	// Without a retry policy every cycle warns again (one warning per poll).
	QVERIFY2(warnings.count() > 0, "the failed re-prepare was not reported at all");
	QVERIFY2(warnings.count() < Cycles,
		"the failed re-prepare was retried on every poll: the request is never given up");
}

void Vst3InstrumentIntegrationTest::testDistinctInputGivesDistinctOutput()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	// The fixture's voice is deliberately pitch-agnostic: it renders one
	// constant level while any key is held, whatever the key is. So the honest
	// comparator between "distinct notes" is their TIMING - a held note, a
	// released one, and one that starts later - and each of those is asserted
	// below to differ from the others. Both compared renders are audible, so
	// this is never "silence versus sound".
	constexpr f_cnt_t Full = Blocks * BlockFrames;
	constexpr f_cnt_t Short = 2 * BlockFrames;
	const Note held{TimePos::fromFrames(Full, 1.0), TimePos(0), 60, DefaultVolume};
	const Note shortNote{TimePos::fromFrames(Short, 1.0), TimePos(0), 60, DefaultVolume};

	const auto one = renderNote(track.get(), instrument, held, 0, Full);
	const auto two = renderNote(track.get(), instrument, shortNote, 0, Short);
	const auto late = renderNote(track.get(), instrument, shortNote, 128, Short);

	// 'two' and 'late' only sound while their note is held, so audibility is
	// measured over the window the note actually occupies.
	QVERIFY(rms(one) > 0.3);
	QVERIFY(rms(std::vector<float>(two.begin(), two.begin() + (Short - 128))) > 0.3);
	QVERIFY(rms(std::vector<float>(late.begin() + 128, late.begin() + Short)) > 0.3);

	// one note held for the whole render: level everywhere
	QVERIFY(std::all_of(one.begin(), one.end(),
		[](float v) { return v == FixtureLevel; }));
	// the same note released after two blocks: level, then exact silence
	QVERIFY(std::all_of(two.begin(), two.begin() + Short,
		[](float v) { return v == FixtureLevel; }));
	QVERIFY(std::all_of(two.begin() + Short, two.end(),
		[](float v) { return v == 0.0f; }));
	// ...and started 128 frames later: silence first, then level
	QVERIFY(std::all_of(late.begin(), late.begin() + 128,
		[](float v) { return v == 0.0f; }));
	QCOMPARE(late[128], FixtureLevel);

	QVERIFY2(maxDifference(one, two) >= FixtureLevel,
		"a note released after two blocks rendered like a note held throughout");
	QVERIFY2(maxDifference(two, late) >= FixtureLevel,
		"starting the note 128 frames later rendered identically");
}

void Vst3InstrumentIntegrationTest::testTheLevelParameterReachesTheInstrument()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	auto* level = instrument->parameterModel(0);
	QVERIFY(level != nullptr);
	QCOMPARE(level->displayName(), QStringLiteral("Level"));

	const Note note{TimePos::fromFrames(Blocks * BlockFrames, 1.0), TimePos(0), 60,
		DefaultVolume};
	const auto full = renderNote(track.get(), instrument, note, 0,
		Blocks * BlockFrames);
	QVERIFY(std::all_of(full.begin(), full.end(),
		[](float v) { return v == FixtureLevel; }));

	level->setValue(0.25f);
	// Wait for Vst3Instrument::poll(): the plug-in window's 100 ms timer
	// mirrors every LMMS parameter model into the plug-in's own edit
	// controller on the GUI thread. That is the shipped path for a model
	// value to reach the plug-in, and it is what a user's knob turn does.
	//
	// The audio-thread parameter path (ProcessData::inputParameterChanges)
	// is filled as well, by the pre-existing effect-host plumbing, but this
	// fixture's synthetic voice does not read it - so this test proves the
	// model -> controller mirror, not the audio-path delivery. Named in
	// docs/VST3-INSTRUMENT-HOSTING.md.
	QTest::qWait(400);

	const auto quiet = renderNote(track.get(), instrument, note, 0,
		Blocks * BlockFrames);
	QVERIFY2(std::all_of(quiet.begin(), quiet.end(),
		[](float v) { return v == 0.25f; }),
		"the instrument did not render the value its parameter model carries");
	QVERIFY2(maxDifference(full, quiet) >= 0.24f,
		"a parameter change did not reach the audio");

}

void Vst3InstrumentIntegrationTest::testStateSurvivesTheProjectFile()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	auto* level = instrument->parameterModel(0);
	QVERIFY(level != nullptr);
	level->setValue(WrittenLevel);
	// See testTheLevelParameterReachesTheInstrument: the 100 ms poll timer is
	// what carries a model value into the instrument's own state.
	QTest::qWait(400);

	const Note note{TimePos::fromFrames(Blocks * BlockFrames, 1.0), TimePos(0), 60,
		DefaultVolume};
	const auto saved = renderNote(track.get(), instrument, note, 0,
		Blocks * BlockFrames);
	QVERIFY2(std::all_of(saved.begin(), saved.end(),
		[](float v) { return v == WrittenLevel; }),
		"the level to be saved was not audible before saving");

	// Save through the track, i.e. through the code the project writer uses.
	// Track::saveState() puts the track's attributes on the element it is
	// given and the track's own settings in a child element named after the
	// track type (src/core/Track.cpp:209-214), so the <instrument> element
	// hangs off that child - which is also the element Track::restoreState()
	// wants back.
	QDomDocument doc;
	auto trackElement = doc.createElement(QStringLiteral("track"));
	doc.appendChild(trackElement);
	track->saveState(doc, trackElement);

	// Track::saveState() -> saveSettings() -> saveTrack() nests two levels:
	// the element SerializingObject::saveState() creates, and inside it the
	// element named after the track type that carries the track's settings
	// (src/core/Track.cpp:196-214). Track::restoreState() wants the OUTER of
	// those two, and searches its children for the settings element itself
	// (src/core/Track.cpp:284-294) - so that is the element captured here.
	const auto savedTrackElement = trackElement.firstChildElement();
	QVERIFY2(!savedTrackElement.isNull(), "the project writer produced no track element");
	const auto instrumentElement =
		trackElement.elementsByTagName(QStringLiteral("instrument")).item(0).toElement();
	QVERIFY2(!instrumentElement.isNull(),
		"the project writer produced no <instrument> element");
	QCOMPARE(instrumentElement.attribute(QStringLiteral("name")),
		QStringLiteral("vst3instrument"));
	// ...the module and class identity travels in the key, which the project
	// writer puts INSIDE the instrument's own state element (it appends it to
	// the element saveState() returned, src/tracks/InstrumentTrack.cpp:861-870)
	const auto keyElement =
		instrumentElement.elementsByTagName(QStringLiteral("key")).item(0).toElement();
	QVERIFY2(!keyElement.isNull(), "the <instrument> element carries no <key>");
	QCOMPARE(keyAttribute(keyElement, QStringLiteral("file")),
		QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE));
	QCOMPARE(keyAttribute(keyElement, QStringLiteral("class")),
		QStringLiteral("Zene VST3 Test Instrument"));
	// ...and the plug-in's own state next to it
	const auto stateElement = instrumentElement.firstChildElement();
	QVERIFY2(!stateElement.isNull(), "the <instrument> element carries no state");
	QCOMPARE(stateElement.tagName(), QStringLiteral("vst3instrument"));
	QVERIFY2(!stateElement.firstChildElement(QStringLiteral("componentstate")).isNull(),
		"the instrument's component state was not written into the project");

	// Reload through the PRODUCT's own reload path: restoreState() on a track
	// with no instrument re-instantiates the plug-in from the <key> the
	// project carries (src/tracks/InstrumentTrack.cpp:957-975) and then hands
	// it the saved state.
	auto freshTrack = makeTrack();
	freshTrack->restoreState(savedTrackElement);
	auto* freshInstrument = freshTrack->instrument();
	QVERIFY2(freshInstrument != nullptr, "the project carried no instrument to reload");
	QCOMPARE(QString::fromLatin1(freshInstrument->descriptor()->name),
		QStringLiteral("vst3instrument"));
	QCOMPARE(freshInstrument->parameterModel(0)->value<float>(), FixtureLevel);

	const auto restored = renderNote(freshTrack.get(), freshInstrument, note, 0,
		Blocks * BlockFrames);
	QVERIFY2(std::all_of(restored.begin(), restored.end(),
		[](float v) { return v == WrittenLevel; }),
		"the reloaded instrument did not render the state the project holds");
	QVERIFY2(maxDifference(saved, restored) == 0.0f,
		"the reloaded instrument renders differently from the saved one");

	qInfo("project round trip: saved %.6f, reloaded %.6f (identical render)",
		rms(saved), rms(restored));

}

void Vst3InstrumentIntegrationTest::testParameterFallbackWithoutStateChunks()
{
	auto track = makeTrack();
	auto* instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);
	auto* level = instrument->parameterModel(0);
	QVERIFY(level != nullptr);
	level->setValue(WrittenLevel);
	QTest::qWait(400);

	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("parent"));
	QDomElement state = instrument->saveState(doc, parent);
	QVERIFY(!state.firstChildElement(QStringLiteral("parameters")).isNull());
	QVERIFY(!state.firstChildElement(QStringLiteral("componentstate")).isNull());
	state.removeChild(state.firstChildElement(QStringLiteral("componentstate")));
	state.removeChild(state.firstChildElement(QStringLiteral("controllerstate")));

	level->setValue(FixtureLevel);
	QTest::qWait(400);
	instrument->restoreState(state);
	QCOMPARE(level->value<float>(), WrittenLevel);

	const Note note{TimePos::fromFrames(Blocks * BlockFrames, 1.0), TimePos(0), 60,
		DefaultVolume};
	const auto restored = renderNote(track.get(), instrument, note, 0,
		Blocks * BlockFrames);
	QVERIFY2(std::all_of(restored.begin(), restored.end(),
		[](float sample) { return sample == WrittenLevel; }),
		"the no-chunk fallback did not restore the audible parameter");
}

void Vst3InstrumentIntegrationTest::testAGuiLessInstrumentExposesItsParameters()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	// What a user gets for a GUI-less instrument is LMMS' own generated
	// parameter view, built from exactly this surface. That surface is asserted
	// here, and it is what testTheLevelParameterReachesTheInstrument then drives.
	QCOMPARE(instrument->parameterCount(), 2);
	QCOMPARE(instrument->parameterName(0), QStringLiteral("Level"));
	auto* level = instrument->parameterModel(0);
	QVERIFY(level != nullptr);
	QVERIFY(!level->displayName().isEmpty());

	// The view's own construction is NOT asserted in this binary, and the
	// reason is a harness limit, measured rather than guessed:
	// Plugin::createView -> Vst3InstrumentView builds a Knob per parameter, and
	// a Knob needs a live GuiApplication - SimpleTextFloat::SimpleTextFloat()
	// is QWidget(getGUI()->mainWindow(), Qt::ToolTip) and getGUI() is null
	// because this binary runs a render-only Engine (Engine::init(true)).
	// That wall sits *behind* the entry point this lane was asked about:
	// InstrumentView::setModel()'s window-icon dereference, which is what the
	// next test covers. The generated grid is verified in the running product
	// under Xvfb - docs/INSTRUMENT-VIEW-SAFETY.md §3 and §5.
}

// The entry point the instrument window uses. InstrumentView's constructor
// calls setModel(), which used to dereference the InstrumentTrackWindow it
// expects to be parented inside, unconditionally: with any other parent that is
// a null pointer, and the process died inside Qt's QWidget::setWindowIcon
// (reproduced here against the pre-fix build - docs/INSTRUMENT-VIEW-SAFETY.md
// §2.1). A view's parent is a plain widget for every caller that is not
// InstrumentTrackWindow::updateInstrumentView(), which is exactly what a test
// harness or a future embedding looks like.
void Vst3InstrumentIntegrationTest::testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	QWidget parent;
	BareInstrumentView view{instrument, &parent};

	// We get past the entry point: the model was set and the view is real...
	QVERIFY2(view.model() == instrument, "setModel() did not take the instrument");
	QVERIFY2(view.parentWidget() == &parent, "the view was reparented");
	// ...and the window it would icon genuinely does not exist here, so the
	// guarded call is the branch this test is exercising.
	QVERIFY2(view.instrumentTrackWindow() == nullptr,
		"the view unexpectedly found an instrument window to icon");

	qInfo("instrument view built without an instrument window: model=%p parent=%p window=%p",
		static_cast<void*>(view.model()), static_cast<void*>(view.parentWidget()),
		static_cast<void*>(view.instrumentTrackWindow()));
}

/*! R4.4, through the product's own path: the installed vst3instrument module, loaded by an
 *  InstrumentTrack, opens its plug-in's editor view in a native window of its own
 *  (Plugin::openNativeEditor -> PluginNativeEditor -> Vst3EditorSession). The window takes the
 *  view's size, a second open raises rather than stacks, and closing leaves nothing open. It
 *  runs on the offscreen platform, whose native ids no plug-in draws into; the fixture draws
 *  nothing either, so what is proved is the attach/size/close contract, not pixels. */
void Vst3InstrumentIntegrationTest::testTheInstrumentsOwnEditorOpensInItsOwnWindow()
{
	auto track = makeTrack();
	auto* instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);
	QVERIFY(!instrument->nativeEditorOpen());
	QString error;
	QVERIFY2(instrument->openNativeEditor(&error), qPrintable(error));
	QVERIFY(instrument->nativeEditorOpen());
	QWidget* window = nullptr;
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		if (top->isVisible() && top->windowTitle() == instrument->displayName()) { window = top; }
	}
	QVERIFY2(window != nullptr, "no window titled after the instrument is showing");
	// The plug-in's 400x300 view with the host header (0.5.0 MX-091) above it; the header's
	// buttons may make the window wider than the view, never narrower.
	const auto* header = window->findChild<QWidget*>(QStringLiteral("pluginHostHeader"));
	QVERIFY(header != nullptr);
	QCOMPARE(window->height(), 300 + header->sizeHint().height());
	QVERIFY(window->width() >= 400);
	QVERIFY2(instrument->openNativeEditor(&error), qPrintable(error));  // raised, not stacked
	int windows = 0;
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		windows += top->isVisible() && top->windowTitle() == instrument->displayName() ? 1 : 0;
	}
	QCOMPARE(windows, 1);
	instrument->closeNativeEditor();
	QVERIFY(!instrument->nativeEditorOpen());

	// The same path through the socket's verbs (their replies held to their schemas here:
	// the test runs with ZENE_CONTROL_CHECK_RESULTS, tests/CMakeLists.txt).
	ControlRegistry::setReady(true);
	const QJsonObject address{{QStringLiteral("target"), control::trackIdOf(track.get())},
		{QStringLiteral("plugin"), QStringLiteral("inst")}};
	// An unattended run has no display to open a window on, and the verb says so, typed.
	const bool headless = ControlRegistry::instance()->isHeadless();
	ControlRegistry::instance()->setHeadless(true);
	QCOMPARE(ControlRegistry::instance()->invoke(QStringLiteral("plugin.editor_open"), address).errorKind,
		ControlErrorKind::Requires);
	// This process DOES have one - the offscreen platform its windows open on above.
	ControlRegistry::instance()->setHeadless(false);
	const ControlResult opened = ControlRegistry::instance()->invoke(QStringLiteral("plugin.editor_open"), address);
	QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
	QVERIFY(opened.result.value(QStringLiteral("open")).toBool());
	const ControlResult closed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.editor_close"), address);
	QVERIFY2(closed.ok, qPrintable(closed.errorMessage));
	QVERIFY(closed.result.value(QStringLiteral("was_open")).toBool());
	QVERIFY(!closed.result.value(QStringLiteral("open")).toBool());

	// And from the instrument's own control view: its "Show plugin editor" button.
	QWidget parent;
	gui::PluginView* view = instrument->createView(&parent);
	QVERIFY(view != nullptr);
	QToolButton* show = nullptr;
	for (QToolButton* button : view->findChildren<QToolButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String("plugin.editor_open")) { show = button; }
	}
	QVERIFY2(show != nullptr, "the instrument view has no plugin-editor button");
	QTest::mouseClick(show, Qt::LeftButton);
	QVERIFY2(instrument->nativeEditorOpen(), "the view's button did not open the editor");
	instrument->closeNativeEditor();
	ControlRegistry::instance()->setHeadless(headless);
	ControlRegistry::setReady(false);
}

/*! 0.5.0 MX-094, through the product's own path: the installed vst3effect module, hosting the
 *  fixture's class in an effect chain, opens the plug-in's editor at the device's editor-scale
 *  option (plugin.options editor_scale). The fixture's view scales itself, so the window takes the
 *  scaled view: 1.5 x 400 x 300. At the default 1 it opens as it always did. */
void Vst3InstrumentIntegrationTest::testAnEffectsEditorOpensAtItsDeviceScale()
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.name = QStringLiteral("Zene VST3 Test Instrument");
	key.attributes[QStringLiteral("file")] = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
	key.attributes[QStringLiteral("class")] = QStringLiteral("Zene VST3 Test Instrument");
	EffectChain chain(nullptr);
	Effect* effect = Effect::instantiate(QStringLiteral("vst3effect"), &chain, &key);
	QVERIFY2(effect != nullptr, "the vst3effect module did not instantiate through the plug-in factory");
	chain.appendEffect(effect);

	const auto openWindowHeight = [effect](int* viewHeight) {
		QString error;
		if (!effect->openNativeEditor(&error)) { return QStringLiteral("not opened: ") + error; }
		for (QWidget* top : QApplication::topLevelWidgets())
		{
			if (!top->isVisible() || top->windowTitle() != effect->displayName()) { continue; }
			const auto* header = top->findChild<QWidget*>(QStringLiteral("pluginHostHeader"));
			*viewHeight = top->height() - (header != nullptr ? header->sizeHint().height() : 0);
			return QString();
		}
		return QStringLiteral("no window titled after the effect is showing");
	};
	int viewHeight = 0;
	effect->deviceOptions().editorScale = 1.5f;
	QString problem = openWindowHeight(&viewHeight);
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
	QCOMPARE(viewHeight, 450);
	effect->closeNativeEditor();

	effect->deviceOptions().editorScale = 1.f;
	problem = openWindowHeight(&viewHeight);
	QVERIFY2(problem.isEmpty(), qPrintable(problem));
	QCOMPARE(viewHeight, 300);
	effect->closeNativeEditor();
	QVERIFY(!effect->nativeEditorOpen());
}

/*! cluster 4555 BUG 4: a visible READ-ONLY parameter (the fixture's "Meter",
 *  declared with ParameterInfo::kIsReadOnly) must not be presented as an
 *  editable knob. The generated grid builds one Knob per parameter it shows;
 *  a knob is an editor - dragging it writes into the model and the host polls
 *  that back into the plug-in, which the plug-in is free to ignore or reset.
 *  The grid must therefore carry exactly the automatable "Level" knob. */
void Vst3InstrumentIntegrationTest::testReadOnlyParametersAreNotKnobs()
{
	auto track = makeTrack();
	auto instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);

	// The read-only parameter reaches the surface with its flag intact.
	QCOMPARE(instrument->parameterCount(), 2);
	QCOMPARE(instrument->parameterName(1), QStringLiteral("Meter"));

	// The generated grid: one knob per editable visible parameter.
	QWidget parent;
	gui::PluginView* view = instrument->createView(&parent);
	QVERIFY2(view != nullptr, "the instrument has no generated view");

	// A knob is an editor: dragging it writes into the model and the host
	// polls that back into the plug-in, which may ignore or reset it. The
	// read-only "Meter" must therefore not own a knob.
	const auto knobs = view->findChildren<gui::Knob*>();
	QCOMPARE(knobs.size(), 1);
	QVERIFY2(knobs.first()->model() == instrument->parameterModel(0),
		"the surviving knob is not the automatable Level parameter");

}

/*! cluster new-p14-364 BUG 4, on the effect side: the vst3effect module hosting the same
 *  fixture builds its generated effect view from the same descriptors. Its read-only "Meter"
 *  must not own a knob either - the effect's view filters hidden parameters only otherwise,
 *  and a knob dragged there writes into a value the plug-in owns. Only "Level" may keep one. */
void Vst3InstrumentIntegrationTest::testReadOnlyParametersAreNotEffectKnobs()
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.name = QStringLiteral("Zene VST3 Test Instrument");
	key.attributes[QStringLiteral("file")] = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
	key.attributes[QStringLiteral("class")] = QStringLiteral("Zene VST3 Test Instrument");
	EffectChain chain(nullptr);
	Effect* effect = Effect::instantiate(QStringLiteral("vst3effect"), &chain, &key);
	QVERIFY2(effect != nullptr, "the vst3effect module did not instantiate through the plug-in factory");
	chain.appendEffect(effect);

	// Declared after the chain, so the view is destroyed before the effect its knobs edit
	std::unique_ptr<gui::EffectControlDialog> view{effect->controls()->createView()};
	QVERIFY2(view != nullptr, "the effect has no generated view");

	const auto knobs = view->findChildren<gui::Knob*>();
	QCOMPARE(knobs.size(), 1);
	QVERIFY2(knobs.first()->model()->displayName() == QStringLiteral("Level"),
		"the surviving effect knob is not the automatable Level parameter");
}

/*! stress-4 (a): the plug-in stress run's state round-trip "lost" read-only parameters - DPF's "Buffer Size"
 *  and "Sample Rate", AIDA-X's "Meter Out" - because plugin.param_get listed them with a range and
 *  plugin.param_set accepted a write the plug-in then overwrote. The fixture's "Meter" is declared with
 *  ParameterInfo::kIsReadOnly: over the control surface it must say read_only, a write to it must be refused
 *  with a typed error and leave it as it was, and plugin.randomise must not move it - while "Level", the
 *  writable parameter beside it, keeps working exactly as before. */
void Vst3InstrumentIntegrationTest::testReadOnlyParametersAreReportedAndRefusedOverControl()
{
	auto track = makeTrack();
	auto* instrument = makeInstrument(track.get());
	QVERIFY(instrument != nullptr);
	QCOMPARE(instrument->parameterName(1), QStringLiteral("Meter"));

	ControlRegistry::setReady(true);
	auto* registry = ControlRegistry::instance();
	const auto address = [&track](int index) {
		return QJsonObject{{QStringLiteral("target"), control::trackIdOf(track.get())},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), index}};
	};
	const auto parameterOf = [](const ControlResult& reply) {
		return reply.result.value(QStringLiteral("parameter")).toObject();
	};

	const ControlResult meter = registry->invoke(QStringLiteral("plugin.param_get"), address(1));
	QVERIFY2(meter.ok, qPrintable(meter.errorMessage));
	QVERIFY2(parameterOf(meter).value(QStringLiteral("read_only")).toBool(),
		"plugin.param_get does not report the kIsReadOnly Meter as read_only");
	const ControlResult level = registry->invoke(QStringLiteral("plugin.param_get"), address(0));
	QVERIFY2(level.ok, qPrintable(level.errorMessage));
	QVERIFY2(!parameterOf(level).contains(QStringLiteral("read_only")), "the writable Level is reported read-only");

	AutomatableModel* meterModel = instrument->parameterModel(1);
	const float before = meterModel->value<float>();
	QJsonObject write = address(1);
	write.insert(QStringLiteral("value"), before < 0.5f ? 1.0 : 0.0);
	const ControlResult refused = registry->invoke(QStringLiteral("plugin.param_set"), write);
	QVERIFY2(!refused.ok, "plugin.param_set wrote a read-only parameter");
	QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
	QCOMPARE(meterModel->value<float>(), before);

	// A sibling write path: plugin.randomise changes the device's settings, and a read-only one is not one.
	const QJsonObject randomise{{QStringLiteral("target"), control::trackIdOf(track.get())},
		{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("amount"), 1.0}, {QStringLiteral("seed"), 7}};
	const ControlResult randomised = registry->invoke(QStringLiteral("plugin.randomise"), randomise);
	QVERIFY2(randomised.ok, qPrintable(randomised.errorMessage));
	QCOMPARE(meterModel->value<float>(), before);

	// The writable parameter is untouched by any of it.
	QJsonObject levelWrite = address(0);
	levelWrite.insert(QStringLiteral("value"), 0.25);
	const ControlResult written = registry->invoke(QStringLiteral("plugin.param_set"), levelWrite);
	QVERIFY2(written.ok, qPrintable(written.errorMessage));
	QCOMPARE(instrument->parameterModel(0)->value<float>(), 0.25f);
	ControlRegistry::setReady(false);
}

} // namespace lmms

QTEST_MAIN(lmms::Vst3InstrumentIntegrationTest)

#include "Vst3InstrumentIntegrationTest.moc"
