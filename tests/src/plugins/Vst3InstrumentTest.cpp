/*
 * Vst3InstrumentTest.cpp - host-level proof of the VST3 MIDI event path
 *
 * Copyright (c) 2026 LMMS contributors
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
 * WHAT THIS PROVES, AND AGAINST WHAT
 * ----------------------------------
 * The subject is the in-tree VST3 instrument fixture
 * (tests/data/vst3-test-instrument/), a real VST3 module that the pinned SDK's
 * own validator classifies as an instrument. It renders a CONSTANT level while
 * any key is held and exact zero otherwise, so every assertion below is an
 * exact equality and never a DSP coincidence.
 *
 * The point of the exercise is that HostedPlugin - not the fixture's own probe
 * (tests/src/plugins/Vst3InstrumentFixtureProbe.cpp) - is what fills
 * ProcessData::inputEvents. So the contract the probe established (note-on at
 * frame 0 plus note-off at frame 256 in a 512-frame block gives sample[255] =
 * 0.5 and sample[256] = 0.0) is reproduced here THROUGH THE HOST:
 * pushMidiEvent() -> bounded queue -> process() -> the plug-in's event list.
 *
 * The comparators matter as much as the equalities. A block with no events, a
 * block with the note at a different offset, a block with two notes and a
 * block at a different level are each asserted to DIFFER from the reference,
 * so "non-silent" can never be an artefact of a stale buffer.
 */

#include <QtTest>

#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "AllocationProbe.h"
#include "Midi.h"        // lmms::MidiEventTypes - the type codes MidiEventIn carries
#include "Vst3Host.h"

#ifndef VST3_TEST_INSTRUMENT_BUNDLE
#define VST3_TEST_INSTRUMENT_BUNDLE ""
#endif

namespace lmms::vst3
{

namespace
{

constexpr double TestSampleRate = 44100.0;
constexpr int TestBlockSize = 512;

//! The fixture's single parameter: its normalized value IS the rendered level.
constexpr std::uint32_t LevelParamId = 100;
constexpr float FixtureLevel = 0.5f;

auto noteOn(std::uint8_t key, std::uint8_t velocity, std::int32_t frameOffset) -> MidiEventIn
{
	MidiEventIn event;
	event.type = static_cast<std::uint8_t>(MidiNoteOn);
	event.data0 = key;
	event.data1 = velocity;
	event.frameOffset = frameOffset;
	return event;
}

auto noteOff(std::uint8_t key, std::int32_t frameOffset) -> MidiEventIn
{
	MidiEventIn event;
	event.type = static_cast<std::uint8_t>(MidiNoteOff);
	event.data0 = key;
	event.frameOffset = frameOffset;
	return event;
}

auto polyPressure(std::uint8_t key, std::uint8_t pressure, std::int32_t frameOffset) -> MidiEventIn
{
	MidiEventIn event;
	event.type = static_cast<std::uint8_t>(MidiKeyPressure);
	event.data0 = key;
	event.data1 = pressure;
	event.frameOffset = frameOffset;
	return event;
}

auto controlChange(std::uint8_t number, std::uint8_t value, std::int32_t frameOffset) -> MidiEventIn
{
	MidiEventIn event;
	event.type = static_cast<std::uint8_t>(MidiControlChange);
	event.data0 = number;
	event.data1 = value;
	event.frameOffset = frameOffset;
	return event;
}

auto rms(const std::vector<float>& data) -> double
{
	double sum = 0.0;
	for (const auto value : data) { sum += static_cast<double>(value) * value; }
	return data.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(data.size()));
}

//! A rendered block: both channels of the fixture's one stereo output bus.
struct Block
{
	std::vector<float> left;
	std::vector<float> right;

	auto isSilent() const -> bool
	{
		return std::all_of(left.begin(), left.end(), [](float v) { return v == 0.0f; }) &&
			std::all_of(right.begin(), right.end(), [](float v) { return v == 0.0f; });
	}

	auto allEqual(float value) const -> bool
	{
		return std::all_of(left.begin(), left.end(), [value](float v) { return v == value; }) &&
			std::all_of(right.begin(), right.end(), [value](float v) { return v == value; });
	}
};

//! Largest absolute difference between two blocks, or -1 when not comparable.
auto maxDifference(const Block& a, const Block& b) -> float
{
	if (a.left.size() != b.left.size()) { return -1.0f; }
	float worst = 0.0f;
	for (std::size_t i = 0; i < a.left.size(); ++i)
	{
		worst = std::max(worst, std::abs(a.left[i] - b.left[i]));
		worst = std::max(worst, std::abs(a.right[i] - b.right[i]));
	}
	return worst;
}

} // namespace

class Vst3InstrumentTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	void testTheFixtureIsAnInstrumentWithAnEventBus();
	void testNoEventsIsExactlySilent();
	void testSampleAccurateNoteOff();
	void testNoteOnOffsetIsHonoured();
	void testDistinctInputGivesDistinctOutput();
	void testNoEventLeaksBetweenBlocks();
	void testTheQueueIsBoundedAndCountsWhatItDrops();
	void testStateRoundTripsIntoAFreshInstance();
	void testProcessAllocatesNothing();

private:
	auto renderWith(const std::vector<MidiEventIn>& events) -> Block;
	auto renderInto(const std::vector<MidiEventIn>& events, float* left, float* right) -> void;

	HostedPlugin m_plugin;
};

void Vst3InstrumentTest::initTestCase()
{
	const QString bundle = QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE);
	if (bundle.isEmpty() || !QFileInfo::exists(bundle))
	{
		QSKIP("no VST3 instrument fixture available (configure with "
			"-DWANT_VST3_TEST_INSTRUMENT=ON and a VST3 SDK checkout)");
	}

	QString error;
	QVERIFY2(m_plugin.load(bundle, QString{}, &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &error),
		qPrintable(error));
}

void Vst3InstrumentTest::cleanupTestCase()
{
	m_plugin.release();
}

void Vst3InstrumentTest::renderInto(const std::vector<MidiEventIn>& events,
	float* left, float* right)
{
	for (const auto& event : events) { m_plugin.pushMidiEvent(event); }
	float* channels[2] = {left, right};
	m_plugin.process(nullptr, channels, 0, 2, TestBlockSize);
}

auto Vst3InstrumentTest::renderWith(const std::vector<MidiEventIn>& events) -> Block
{
	Block block;
	// Sentinel, not zero: a plug-in that never writes its output buffers
	// cannot pass a silence assertion by accident.
	block.left.assign(TestBlockSize, -1.0f);
	block.right.assign(TestBlockSize, -1.0f);
	renderInto(events, block.left.data(), block.right.data());
	return block;
}

void Vst3InstrumentTest::testTheFixtureIsAnInstrumentWithAnEventBus()
{
	QCOMPARE(m_plugin.isInstrument(), true);
	QCOMPARE(m_plugin.receivesMidi(), true);
	// The fixture declares exactly one event input; the host drives bus 0.
	QCOMPARE(m_plugin.eventInputBusIndex(), 0);

	// An instrument has no audio input and one stereo output - the bus shape
	// an effect does not have.
	const auto& layout = m_plugin.busLayout();
	QCOMPARE(layout.inputs, 0);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(int(layout.inputBusChannels.size()), 0);
	QCOMPARE(int(layout.outputBusChannels.size()), 1);
	qInfo("fixture: audio buses in=%d out=%d, event input bus=%d",
		layout.inputs, layout.outputs, m_plugin.eventInputBusIndex());
}

void Vst3InstrumentTest::testNoEventsIsExactlySilent()
{
	// Nothing was pushed: the host must hand the plug-in an EMPTY event list
	// (not a stale one), so the output is exact zero rather than the sentinel.
	const auto block = renderWith({});
	QVERIFY2(block.isSilent(), "a block with no MIDI events rendered non-silence");
}

void Vst3InstrumentTest::testSampleAccurateNoteOff()
{
	// The fixture's probe established this contract against the SDK directly:
	// note-on at frame 0 and note-off at frame 256 of a 512-frame block give
	// non-zero up to sample 255 and exact zero from sample 256 onwards.
	// Reproduced here through the HOST's MIDI path.
	const auto block = renderWith({noteOn(69, 1, 0), noteOff(69, 256)});

	QCOMPARE(block.left[0], FixtureLevel);
	QCOMPARE(block.left[255], FixtureLevel);
	QCOMPARE(block.left[256], 0.0f);
	QCOMPARE(block.left[511], 0.0f);
	QCOMPARE(block.right[255], FixtureLevel);
	QCOMPARE(block.right[256], 0.0f);

	// and as whole-window statements, so a one-sample error cannot hide
	QVERIFY(std::all_of(block.left.begin(), block.left.begin() + 256,
		[](float v) { return v == 0.5f; }));
	QVERIFY(std::all_of(block.left.begin() + 256, block.left.end(),
		[](float v) { return v == 0.0f; }));
	QVERIFY2(rms(block.left) > 0.3, "the note-bearing half is not audible");

	qInfo("through the host: sample[0]=%.6f sample[255]=%.6f sample[256]=%.6f "
		"sample[511]=%.6f", block.left[0], block.left[255], block.left[256],
		block.left[511]);
}

void Vst3InstrumentTest::testNoteOnOffsetIsHonoured()
{
	// An event at frame 128 must affect exactly frame 128 and nothing before
	// it - the sample-accuracy claim in the other direction.
	const auto block = renderWith({noteOn(60, 1, 128)});

	QVERIFY(std::all_of(block.left.begin(), block.left.begin() + 128,
		[](float v) { return v == 0.0f; }));
	QCOMPARE(block.left[128], FixtureLevel);
	QCOMPARE(block.left[511], FixtureLevel);

	// The offset really moved the sound: against the same note at frame 0 the
	// two blocks differ in their first half.
	const auto atZero = renderWith({noteOn(60, 1, 0), noteOff(60, TestBlockSize)});
	QVERIFY2(maxDifference(block, atZero) >= FixtureLevel,
		"a note at frame 128 rendered like a note at frame 0");
}

void Vst3InstrumentTest::testDistinctInputGivesDistinctOutput()
{
	// Comparator 1: one held note against two notes in sequence. Both are
	// audible, so this is not "silence versus sound".
	const auto oneNote = renderWith({noteOn(60, 1, 0), noteOff(60, TestBlockSize)});
	const auto twoNotes = renderWith({noteOn(60, 1, 64), noteOff(60, 192),
		noteOn(67, 1, 320), noteOff(67, 448)});
	QVERIFY(rms(oneNote.left) > 0.3);
	QVERIFY(rms(twoNotes.left) > 0.3);
	QCOMPARE(twoNotes.left[0], 0.0f);   // silent until the first note-on
	QCOMPARE(twoNotes.left[64], FixtureLevel);
	QCOMPARE(twoNotes.left[192], 0.0f); // the gap between the two notes
	QCOMPARE(twoNotes.left[320], FixtureLevel);
	QCOMPARE(twoNotes.left[448], 0.0f);
	QVERIFY2(maxDifference(oneNote, twoNotes) >= FixtureLevel,
		"two different note patterns rendered identically");

	// Comparator 2: the level the plug-in renders follows the value the host
	// holds for its level parameter, and a different level therefore renders
	// differently. This is also the state-delivery proof.
	//
	// The value is written with notifyController(), which is the host's
	// GUI-thread value mirror (Vst3Instrument::poll() calls it for every
	// model). It is NOT the audio path: for a live model change the host also
	// puts the value into ProcessData::inputParameterChanges, which is the
	// pre-existing effect-host plumbing - and which this fixture's synthetic
	// voice does not read, so that half is not witnessed here. Recorded in
	// docs/VST3-INSTRUMENT-HOSTING.md rather than implied.
	m_plugin.notifyController(LevelParamId, 0.25f);
	const auto quiet = renderWith({noteOn(60, 1, 0), noteOff(60, TestBlockSize)});
	QVERIFY(quiet.allEqual(0.25f));
	QVERIFY2(maxDifference(quiet, oneNote) >= 0.24f,
		"a level change of 0.5 -> 0.25 did not reach the output");
	m_plugin.notifyController(LevelParamId, FixtureLevel);
}

void Vst3InstrumentTest::testNoEventLeaksBetweenBlocks()
{
	// A note pushed for block N sounds in N and not in N+1.
	const auto withNote = renderWith({noteOn(72, 1, 0), noteOff(72, 256)});
	QCOMPARE(withNote.left[0], FixtureLevel);
	QCOMPARE(withNote.left[256], 0.0f);

	const auto after = renderWith({});
	QVERIFY2(after.isSilent(), "the note leaked into the next block");

	// ...and the other direction: a note held across a block boundary stays
	// held until its note-off, even though the block it started in has ended.
	const auto held = renderWith({noteOn(74, 1, 300)});
	QCOMPARE(held.left[299], 0.0f);
	QCOMPARE(held.left[300], FixtureLevel);
	QCOMPARE(held.left[511], FixtureLevel);

	const auto stillHeld = renderWith({});
	QVERIFY2(stillHeld.allEqual(FixtureLevel),
		"a note held across a block boundary was dropped");

	const auto released = renderWith({noteOff(74, 0)});
	QVERIFY2(released.isSilent(), "the note lasted past its note-off");
}

void Vst3InstrumentTest::testTheQueueIsBoundedAndCountsWhatItDrops()
{
	// The queue between the MIDI path and the audio thread is fixed size: a
	// flood must drop and COUNT, never grow and never block. This is the
	// bounded-growth half of the real-time rule.
	const auto before = m_plugin.droppedMidiEvents();
	constexpr std::uint32_t Flood = 4096;
	for (std::uint32_t i = 0; i < Flood; ++i)
	{
		m_plugin.pushMidiEvent(i % 2 == 0 ? noteOn(60, 1, 0) : noteOff(60, 0));
	}
	const auto dropped = m_plugin.droppedMidiEvents() - before;
	// Exactly kMidiQueueCapacity events fit; everything beyond that is dropped.
	QCOMPARE(dropped, static_cast<std::uint64_t>(Flood) - kMidiQueueCapacity);

	// Drain what is left, then assert nothing further was lost.
	for (int i = 0; i < 8; ++i) { renderWith({}); }
	QCOMPARE(m_plugin.droppedMidiEvents() - before, dropped);
}

void Vst3InstrumentTest::testStateRoundTripsIntoAFreshInstance()
{
	// Set state, save it, load it into a plug-in that has never seen it, and
	// assert the state is audible again. The fixture keeps its state as a
	// single float - the level it renders while a key is held - so the
	// observable is audio, not a parameter read-back.
	constexpr float Written = 0.75f;
	// notifyController() is the host's GUI-thread value mirror; it is what
	// puts the value where the plug-in's own state lives.
	m_plugin.notifyController(LevelParamId, Written);

	QByteArray componentState;
	QByteArray controllerState;
	QVERIFY(m_plugin.saveState(&componentState, &controllerState));
	QVERIFY2(!componentState.isEmpty(),
		"the fixture writes component state but nothing was captured");

	auto renderFresh = [](HostedPlugin& plugin, float* left, float* right) {
		plugin.pushMidiEvent(noteOn(60, 1, 0));
		plugin.pushMidiEvent(noteOff(60, TestBlockSize));
		float* channels[2] = {left, right};
		plugin.process(nullptr, channels, 0, 2, TestBlockSize);
	};

	Block before;
	before.left.assign(TestBlockSize, 0.0f);
	before.right.assign(TestBlockSize, 0.0f);
	renderFresh(m_plugin, before.left.data(), before.right.data());
	QVERIFY2(before.allEqual(Written), "the written state was not audible");

	HostedPlugin fresh;
	QString error;
	QVERIFY2(fresh.load(QStringLiteral(VST3_TEST_INSTRUMENT_BUNDLE), QString{}, &error),
		qPrintable(error));
	QVERIFY2(fresh.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));

	Block defaulted;
	defaulted.left.assign(TestBlockSize, 0.0f);
	defaulted.right.assign(TestBlockSize, 0.0f);
	renderFresh(fresh, defaulted.left.data(), defaulted.right.data());
	QVERIFY2(defaulted.allEqual(FixtureLevel),
		"a fresh instance did not start at the plug-in's own default");

	QVERIFY(fresh.loadState(componentState, controllerState));

	Block restored;
	restored.left.assign(TestBlockSize, 0.0f);
	restored.right.assign(TestBlockSize, 0.0f);
	renderFresh(fresh, restored.left.data(), restored.right.data());
	QVERIFY2(restored.allEqual(Written),
		"the restored state did not reach the audio path");
	QVERIFY2(maxDifference(before, restored) == 0.0f,
		"the reloaded instance renders differently from the saved one");

	m_plugin.notifyController(LevelParamId, FixtureLevel);
}

void Vst3InstrumentTest::testProcessAllocatesNothing()
{
	// The real-time rule, measured on the thread that calls process(): the MIDI
	// drain, the offset ordering and the event-list fill must all be allocation
	// free. AllocationProbe replaces global operator new for this binary and
	// counts only while tlCountAllocations is set, so the buffers below are
	// allocated BEFORE the counted region on purpose.
	std::vector<float> left(TestBlockSize, 0.0f);
	std::vector<float> right(TestBlockSize, 0.0f);

	// The event list is built BEFORE the counted region on purpose: building a
	// std::vector from a braced list allocates, and that would be counted.
	const std::vector<MidiEventIn> events{noteOn(60, 1, 0), noteOff(60, 128),
		noteOn(64, 1, 200), polyPressure(64, 90, 300), controlChange(7, 100, 400),
		noteOff(64, 480)};

	// warm up (the first block after a parameter change builds the queues)
	std::vector<MidiEventIn> warmup{noteOn(60, 1, 0), noteOff(60, 128)};
	renderInto(warmup, left.data(), right.data());

	lmms::test::tlCountAllocations = true;
	lmms::test::resetAllocationCount();
	for (int block = 0; block < 8; ++block)
	{
		renderInto(events, left.data(), right.data());
	}
	lmms::test::tlCountAllocations = false;

	const auto allocations = lmms::test::tlAllocationCount;
	QVERIFY2(allocations == 0,
		qPrintable(QStringLiteral("allocation probe: %1 allocation(s) on the "
			"process thread").arg(allocations)));
	qInfo("allocation probe: 8 MIDI-carrying blocks, 0 allocations on the process thread");
}

} // namespace lmms::vst3

QTEST_GUILESS_MAIN(lmms::vst3::Vst3InstrumentTest)
#include "Vst3InstrumentTest.moc"
