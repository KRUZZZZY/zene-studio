/*
 * ClapHostTest.cpp - integration tests for the in-process CLAP host
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

#include <QtTest>

#include <QFileInfo>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <algorithm>

#include "ClapHost.h"

#ifndef CLAP_TEST_PLUGIN_PATH
#define CLAP_TEST_PLUGIN_PATH ""
#endif
//! The instrument subject of the note-path case (feature row 79): a real CLAP
//! generator built from the pinned headers,
//! tests/data/clap-test-plugin/clap-test-instrument.c. Set by
//! tests/CMakeLists.txt; empty in a build without the fixture and the case
//! skips.
#ifndef CLAP_TEST_INSTRUMENT_PATH
#define CLAP_TEST_INSTRUMENT_PATH ""
#endif

namespace lmms::clap
{

namespace
{
constexpr double TestSampleRate = 48000.0;
constexpr int TestBlockSize = 512;
constexpr int TestFrames = 256;

//! Plug-in under test: tests/data/clap-test-plugin/clap-test-gain.c, built
//! from the pinned CLAP headers. Two parameters: a continuous gain (id 1,
//! plain value is the linear gain factor) and a stepped bypass switch (id 2).
constexpr const char* TestPluginId = "org.lmms.test.clap-gain";
constexpr std::uint32_t GainParamId = 1;
constexpr std::uint32_t BypassParamId = 2;

auto rms(const std::vector<float>& data) -> double
{
	double sum = 0.0;
	for (const auto value : data)
	{
		sum += static_cast<double>(value) * static_cast<double>(value);
	}
	return data.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(data.size()));
}

//! Mirrors gain_state_t in tests/data/clap-test-plugin/clap-test-gain.c: the
//! plug-in saves it through clap.state and the host stores the bytes opaquely,
//! so the test reads the plug-in's own account of what it was asked to process.
struct PluginState
{
	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t declaredMaxFrames;
	std::uint32_t blocks;
	std::uint32_t maxBlock;
	std::uint32_t oversizedCalls;
	std::uint32_t recordedCount;
	std::uint32_t recorded[16];
	double gain;
	double bypass;
	double framesProcessed;
};
static_assert(sizeof(PluginState) == 120, "the state layout in the fixture changed");

constexpr std::uint32_t PluginStateMagic = 0x4C4D4741u; // "LMGA"
constexpr std::uint32_t PluginStateVersion = 2u;

//! The INSTRUMENT fixture's identity and state (feature row 79), mirroring
//! tests/data/clap-test-plugin/clap-test-instrument.c: the same read-back trick
//! as PluginState above - the plug-in's own account of what the note path
//! actually delivered to it.
constexpr const char* TestInstrumentId = "org.lmms.test.clap-instrument";
constexpr std::uint32_t InstrumentGainParamId = 1;

//! Mirrors instrument_state_t in clap-test-instrument.c. Doubles first, then
//! an even number of 4-byte members, so the layout has no padding and the
//! static_assert below is the whole contract.
struct InstrumentState
{
	double framesProcessed;
	double gain;

	std::uint32_t magic;
	std::uint32_t version;
	std::uint32_t declaredMaxFrames;
	std::uint32_t blocks;
	std::uint32_t maxBlock;
	std::uint32_t oversizedCalls;
	std::uint32_t eventCount;
	std::uint32_t paramEventCount;
	std::uint32_t noteOnCount;
	std::uint32_t noteOffCount;
	std::uint32_t chokeCount;
	std::uint32_t ignoredEventCount;
	std::uint32_t activeVoices;
	std::uint32_t releaseLeft;
	std::uint32_t lastVelocityMilli;
	std::int32_t lastKey;
	std::int32_t lastChannel;
	std::int32_t lastPortIndex;
	std::int32_t firstNoteTime;
	std::int32_t lastNoteTime;
};
static_assert(sizeof(InstrumentState) == 96, "the fixture's state layout changed");

constexpr std::uint32_t InstrumentStateMagic = 0x4C4D494Eu; // "LMIN"
constexpr std::uint32_t InstrumentStateVersion = 1u;

//! Saves the instrument fixture's state and decodes it.
auto readInstrumentState(const HostedPlugin& plugin, InstrumentState* state) -> bool
{
	QByteArray bytes;
	if (!plugin.saveState(&bytes) || bytes.size() != static_cast<int>(sizeof(InstrumentState)))
	{
		return false;
	}
	std::memcpy(state, bytes.constData(), sizeof(InstrumentState));
	return state->magic == InstrumentStateMagic && state->version == InstrumentStateVersion;
}

//! Saves the plug-in state and decodes it. False when the plug-in saved
//! something that is not this layout, which the caller reports as a failure.
auto readPluginState(const HostedPlugin& plugin, PluginState* state) -> bool
{
	QByteArray bytes;
	if (!plugin.saveState(&bytes) || bytes.size() != static_cast<int>(sizeof(PluginState)))
	{
		return false;
	}
	std::memcpy(state, bytes.constData(), sizeof(PluginState));
	return state->magic == PluginStateMagic && state->version == PluginStateVersion;
}
} // namespace

class ClapHostTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void testModuleListing();
	void testIdentity();
	void testParameters();
	void testParameterSetGet();
	void testStateRoundTrip();
	void testAudioGain();
	void testAudioSilenceWhenGainIsZero();
	void testBypass();
	void testChannelPartition();
	void testChunkedProcessing();
	void testEffectRefusesNotes();
	void testInstrumentNotePath();

private:
	void process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
		std::vector<std::vector<float>>& outputs, int frames);

	HostedPlugin m_plugin;
};

void ClapHostTest::initTestCase()
{
	const QString path = QStringLiteral(CLAP_TEST_PLUGIN_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		QSKIP("no CLAP test plug-in available (build tests/data/clap-test-plugin "
			"from the pinned CLAP headers and configure with -DLMMS_CLAP_PATH=<checkout>)");
	}
	QString error;
	QVERIFY2(m_plugin.load(path, QString::fromLatin1(TestPluginId), &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QString prepareError;
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &prepareError), qPrintable(prepareError));
}

void ClapHostTest::testModuleListing()
{
	const QString path = QStringLiteral(CLAP_TEST_PLUGIN_PATH);
	QString error;
	const auto classes = listClasses(path, &error);
	QVERIFY2(!classes.empty(), qPrintable(error));
	QCOMPARE(classes.size(), std::size_t{1});
	QCOMPARE(classes[0].id, QString::fromLatin1(TestPluginId));
	QCOMPARE(classes[0].name, QStringLiteral("LMMS CLAP Test Gain"));
	QCOMPARE(classes[0].isInstrument, false);
	QVERIFY(!classes[0].vendor.isEmpty());
}

void ClapHostTest::testIdentity()
{
	QCOMPARE(m_plugin.className(), QStringLiteral("LMMS CLAP Test Gain"));
	QCOMPARE(m_plugin.vendor(), QStringLiteral("LMMS contributors"));
	QCOMPARE(m_plugin.isInstrument(), false);
	QCOMPARE(m_plugin.latency(), std::uint32_t{0});

	const auto& layout = m_plugin.busLayout();
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
	QCOMPARE(layout.inputPortChannels.size(), std::size_t{1});
	QCOMPARE(layout.outputPortChannels.size(), std::size_t{1});
	QCOMPARE(layout.inputPortChannels[0], 2);
	QCOMPARE(layout.outputPortChannels[0], 2);
}

void ClapHostTest::testParameters()
{
	const auto& parameters = m_plugin.parameters();
	QCOMPARE(parameters.size(), std::size_t{2});

	QCOMPARE(parameters[0].id, GainParamId);
	QCOMPARE(parameters[0].title, QStringLiteral("Gain"));
	QCOMPARE(parameters[0].stepped, false);
	QCOMPARE(parameters[0].minValue, 0.0);
	QCOMPARE(parameters[0].maxValue, 1.0);
	QCOMPARE(parameters[0].defaultValue, 1.0);

	QCOMPARE(parameters[1].id, BypassParamId);
	QCOMPARE(parameters[1].title, QStringLiteral("Bypass"));
	QCOMPARE(parameters[1].stepped, true);
	QCOMPARE(parameters[1].defaultValue, 0.0);

	// ids must be unique and indexable without allocation
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		QCOMPARE(m_plugin.paramIndex(parameters[i].id), static_cast<int>(i));
	}
	QCOMPARE(m_plugin.paramIndex(0xdeadbeefu), -1);

	// the plug-in's own value-to-text conversion is used
	QCOMPARE(m_plugin.paramDisplayValue(GainParamId, 0.5), QStringLiteral("0.50"));
}

void ClapHostTest::testParameterSetGet()
{
	m_plugin.setParamPlain(GainParamId, 0.25);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.25);
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.25f);

	// out of range values are clamped, not wrapped
	m_plugin.setParamPlain(GainParamId, 2.0);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 1.0);
	m_plugin.setParamPlain(GainParamId, -1.0);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.0);

	// normalized setters map through the plain range
	m_plugin.setParamNormalized(GainParamId, 0.5f);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.5);

	// unknown ids are ignored
	m_plugin.setParamPlain(0xdeadbeefu, 0.5);
	QCOMPARE(m_plugin.paramPlain(0xdeadbeefu), 0.0);

	m_plugin.setParamPlain(GainParamId, 1.0);
}

void ClapHostTest::testStateRoundTrip()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamPlain(GainParamId, 0.3);
	// Parameter changes reach the processor through process(); a host saves the
	// plug-in state after the value has been delivered.
	process(m_plugin, inputs, outputs, TestFrames);

	QByteArray state;
	QVERIFY(m_plugin.saveState(&state));
	QVERIFY(!state.isEmpty());
	qInfo("state round trip: saved %d bytes", int(state.size()));

	m_plugin.setParamPlain(GainParamId, 0.9);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.9);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.45) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	QVERIFY(m_plugin.loadState(state));
	QVERIFY(std::abs(m_plugin.paramPlain(GainParamId) - 0.3) < 1e-6);

	// no process() in between, so the plug-in counter is unchanged: the state
	// must be byte identical when saved again
	QByteArray stateAgain;
	QVERIFY(m_plugin.saveState(&stateAgain));
	QCOMPARE(stateAgain, state);

	// the restored state must reach the processor, not just the host cache
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.15) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void ClapHostTest::testAudioGain()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	// the plain gain value is the linear gain factor
	m_plugin.setParamPlain(GainParamId, 0.5);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED input RMS %.6f -> gain 0.5 output RMS %.6f",
		rms(inputs[0]), rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
	QVERIFY2(std::abs(rms(outputs[1]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[1]))));

	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	m_plugin.setParamPlain(GainParamId, 1.0);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED input RMS %.6f -> gain 1.0 output RMS %.6f",
		rms(inputs[0]), rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.5) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void ClapHostTest::testAudioSilenceWhenGainIsZero()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamPlain(GainParamId, 0.0);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED input RMS %.6f -> gain 0.0 output RMS %.6f",
		rms(inputs[0]), rms(outputs[0]));
	QCOMPARE(rms(outputs[0]), 0.0);
	QCOMPARE(rms(outputs[1]), 0.0);

	m_plugin.setParamPlain(GainParamId, 1.0);
}

void ClapHostTest::testBypass()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	// the stepped bypass parameter must override the gain
	m_plugin.setParamPlain(GainParamId, 0.1);
	m_plugin.setParamPlain(BypassParamId, 1.0);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED bypass=1 with gain 0.1 -> output RMS %.6f", rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.5) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	m_plugin.setParamPlain(BypassParamId, 0.0);
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED bypass=0 with gain 0.1 -> output RMS %.6f", rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.05) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	m_plugin.setParamPlain(GainParamId, 1.0);
}

void ClapHostTest::testChannelPartition()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.0f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	std::fill(inputs[0].begin(), inputs[0].end(), 0.5f);
	std::fill(inputs[1].begin(), inputs[1].end(), 0.25f);

	m_plugin.setParamPlain(GainParamId, 1.0);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY(std::abs(rms(outputs[0]) - 0.5) < 1e-6);
	QVERIFY(std::abs(rms(outputs[1]) - 0.25) < 1e-6);
}

/*!
 * CODE-4: a request for MORE frames than the plug-in was activated for is
 * processed in chunks, exactly, and the tail of the caller's buffers is
 * written rather than left holding whatever was there before.
 *
 * This is the case the host used to get wrong in two different ways: it did
 * `frames = std::min(frames, impl.maxFrames)` (ClapHost.cpp, before this
 * change), which handed the plug-in only the first `maxFrames` frames and
 * returned true, leaving [maxFrames, frames) untouched in the caller's
 * buffers. The subject is the in-tree MIT fixture, tests/data/clap-test-plugin/
 * clap-test-gain.c, which now reports the sequence of block sizes it was asked
 * for (through clap.state) and any call that asked for more frames than
 * activate() declared.
 */
void ClapHostTest::testChunkedProcessing()
{
	constexpr int Prepared = TestBlockSize;   // 512, what prepare() was given
	constexpr int Frames = Prepared * 2 + 37; // 1061: two blocks and a remainder
	constexpr float Stale = -12345.0f;
	constexpr float Input = 0.25f;
	constexpr float Expected = Input * 0.5f; // gain 0.5

	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(Frames, Input));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(Frames, Stale));

	m_plugin.setParamPlain(GainParamId, 0.5);
	// The fixture counts every process() call it has ever been given, and the
	// test cases before this one drove it too: measure the DELTA this request
	// made, not an absolute count.
	PluginState beforeState{};
	QVERIFY2(readPluginState(m_plugin, &beforeState), "the fixture saved an unexpected state layout");
	const auto before = hostChunkingStats();
	process(m_plugin, inputs, outputs, Frames);
	const auto after = hostChunkingStats();

	// 1. every frame of every channel the caller supplied was written: a
	//    truncating host leaves the sentinel in place from maxFrames on.
	for (std::size_t channel = 0; channel < outputs.size(); ++channel)
	{
		for (int frame = 0; frame < Frames; ++frame)
		{
			QVERIFY2(outputs[channel][frame] == Expected,
				qPrintable(QStringLiteral("channel %1 frame %2 holds %3, not %4 (%5-frame request "
										  "into a %6-frame prepared block)")
							   .arg(channel)
							   .arg(frame)
							   .arg(outputs[channel][frame])
							   .arg(Expected)
							   .arg(Frames)
							   .arg(Prepared)));
		}
	}

	// 2. the plug-in's own account: 1061 frames in 3 calls, none longer than
	//    the block it was activated for
	PluginState state{};
	QVERIFY2(readPluginState(m_plugin, &state), "the fixture saved an unexpected state layout");
	QVERIFY2(state.declaredMaxFrames == static_cast<std::uint32_t>(Prepared),
		qPrintable(QStringLiteral("plug-in was activated for %1 frames").arg(state.declaredMaxFrames)));
	QCOMPARE(state.blocks - beforeState.blocks, std::uint32_t{3});
	QCOMPARE(state.framesProcessed - beforeState.framesProcessed, static_cast<double>(Frames));
	QCOMPARE(state.recordedCount - beforeState.recordedCount, std::uint32_t{3});
	QVERIFY2(state.recordedCount <= 16, "the fixture's recorded-block ring overflowed");
	const std::uint32_t first = beforeState.recordedCount;
	QCOMPARE(state.recorded[first + 0], static_cast<std::uint32_t>(Prepared));
	QCOMPARE(state.recorded[first + 1], static_cast<std::uint32_t>(Prepared));
	QCOMPARE(state.recorded[first + 2], static_cast<std::uint32_t>(Frames - 2 * Prepared));
	QCOMPARE(state.maxBlock, static_cast<std::uint32_t>(Prepared));
	QCOMPARE(state.oversizedCalls, beforeState.oversizedCalls);
	qInfo("MEASURED chunk sequence for a %d-frame request into a %d-frame block: %u, %u, %u",
		Frames, Prepared, state.recorded[first], state.recorded[first + 1], state.recorded[first + 2]);

	// 3. the host's own counters agree with the plug-in's
	QCOMPARE(after.requests - before.requests, std::uint64_t{1});
	QCOMPARE(after.chunks - before.chunks, std::uint64_t{3});
	QCOMPARE(after.frames - before.frames, static_cast<std::uint64_t>(Frames));
	QCOMPARE(after.multiChunkRequests - before.multiChunkRequests, std::uint64_t{1});
	QCOMPARE(after.framesBeyondPreparedBlock - before.framesBeyondPreparedBlock,
		static_cast<std::uint64_t>(Frames - Prepared));

	// 4. the same input through a second instance of the same fixture, prepared
	//    for the whole request instead of one block, produces identical audio:
	//    the chunking is transparent, not an approximation.
	HostedPlugin wide;
	QString loadError;
	QVERIFY2(wide.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId),
			 &loadError),
		qPrintable(loadError));
	QString prepareError;
	QVERIFY2(wide.prepare(TestSampleRate, Frames, &prepareError), qPrintable(prepareError));
	wide.setParamPlain(GainParamId, 0.5);
	std::vector<std::vector<float>> wideOutputs(layout.outputs, std::vector<float>(Frames, Stale));
	process(wide, inputs, wideOutputs, Frames);

	for (std::size_t channel = 0; channel < wideOutputs.size(); ++channel)
	{
		for (int frame = 0; frame < Frames; ++frame)
		{
			QCOMPARE(wideOutputs[channel][frame], outputs[channel][frame]);
		}
	}
	PluginState wideState{};
	QVERIFY2(readPluginState(wide, &wideState), "the fixture saved an unexpected state layout");
	QCOMPARE(wideState.declaredMaxFrames, static_cast<std::uint32_t>(Frames));
	QCOMPARE(wideState.blocks, std::uint32_t{1});
	QCOMPARE(wideState.framesProcessed, static_cast<double>(Frames));
	qInfo("MEASURED same %d frames, one instance asked for %u-frame blocks and one for %u-frame "
		  "blocks: identical output",
		Frames, state.maxBlock, wideState.maxBlock);

	m_plugin.setParamPlain(GainParamId, 1.0);
}

//! A plug-in with no note input port takes no notes, and the refusal is
//! counted rather than silently dropped (feature row 79): the effect fixture
//! implements no clap.note-ports, so the note path must recognise that.
void ClapHostTest::testEffectRefusesNotes()
{
	QVERIFY2(!m_plugin.isInstrument(), "the gain fixture is an effect");
	QVERIFY(!m_plugin.acceptsNotes());
	QCOMPARE(m_plugin.noteInputPorts().size(), std::size_t{0});
	QCOMPARE(m_plugin.noteCounters().ports, std::uint32_t{0});

	const auto before = m_plugin.noteCounters();
	m_plugin.setNoteOn(0, 60, 1.0, 0);
	m_plugin.setNoteOff(0, 60, 0);
	const auto after = m_plugin.noteCounters();
	QCOMPARE(after.pushed - before.pushed, std::uint32_t{0});
	QCOMPARE(after.dropped - before.dropped, std::uint32_t{2});
	QCOMPARE(after.delivered - before.delivered, std::uint32_t{0});
}

//! The note-ports / input-events path end to end (feature row 79, board task
//! #669), against the in-tree MIT instrument fixture: a real CLAP generator
//! that declares one CLAP-dialect note input port and no audio input port.
//!
//! What this proves, in the plug-in's own account of what it was handed:
//!  - the host reads clap.note-ports and knows the port (index, dialect);
//!  - a note-off/on reaches the plug-in's input event list, at the frame
//!    offset LMMS computed, on the plug-in's own port index;
//!  - a note produces AUDIO OUT (the fixture's level), and silence without one
//!    - so the note path is not merely bookkeeping;
//!  - note-off runs the plug-in's release and then falls silent;
//!  - note-choke silences within the same block;
//!  - the events of a request belong to the chunk that starts it (a request
//!    split into four chunks delivers the note once, not four times);
//!  - parameter events and note events travel in the same input list.
void ClapHostTest::testInstrumentNotePath()
{
	const QString path = QStringLiteral(CLAP_TEST_INSTRUMENT_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		QSKIP("no CLAP instrument fixture in this build (tests/data/clap-test-plugin/"
			"clap-test-instrument.c, built when the pinned CLAP headers are present)");
	}

	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(path, QString::fromLatin1(TestInstrumentId), &error), qPrintable(error));
	QVERIFY2(plugin.isInstrument(), "the instrument fixture is not reported as an instrument");

	// --- the port discovery half -----------------------------------------
	const auto& notePorts = plugin.noteInputPorts();
	QCOMPARE(notePorts.size(), std::size_t{1});
	QCOMPARE(notePorts[0].id, std::uint32_t{1});
	QCOMPARE(notePorts[0].name, QStringLiteral("Notes"));
	QVERIFY2((notePorts[0].supportedDialects & NoteDialectClap) != 0,
		"the fixture's port does not declare the CLAP dialect");
	QVERIFY((notePorts[0].supportedDialects & NoteDialectMidi) != 0);
	QCOMPARE(notePorts[0].preferredDialect, NoteDialectClap);
	QVERIFY2(notePorts[0].preferred, "no note port was selected for delivery");
	QCOMPARE(plugin.preferredNotePort(), std::uint32_t{0});
	QVERIFY(plugin.acceptsNotes());

	// --- the audio-output configuration half ------------------------------
	// A generator: no audio input at all, a stereo output. This is what the
	// instrument host hands to AudioPorts and what makes a load possible at
	// all (a "no usable audio ports" refusal would be wrong here).
	QCOMPARE(plugin.busLayout().inputs, 0);
	QCOMPARE(plugin.busLayout().outputs, 2);
	QCOMPARE(plugin.busLayout().outputPortChannels.size(), std::size_t{1});
	QCOMPARE(plugin.busLayout().outputPortChannels[0], 2);

	QString prepareError;
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &prepareError),
		qPrintable(prepareError));

	QCOMPARE(plugin.noteCounters().ports, std::uint32_t{1});
	QCOMPARE(plugin.noteCounters().pushed, std::uint32_t{0});

	// No audio input: the track routes nothing in, which is a generator's
	// shape. The output is the instrument's own.
	const std::vector<std::vector<float>> noInputs;
	std::vector<std::vector<float>> outputs(2, std::vector<float>(TestFrames, 0.0f));

	// --- silence before a note -------------------------------------------
	process(plugin, noInputs, outputs, TestFrames);
	QCOMPARE(rms(outputs[0]), 0.0);
	InstrumentState state{};
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.noteOnCount, std::uint32_t{0});
	QCOMPARE(state.activeVoices, std::uint32_t{0});
	QCOMPARE(state.framesProcessed, static_cast<double>(TestFrames));

	// --- a note at frame 7 of the block: audio out ------------------------
	plugin.setNoteOn(0, 60, 1.0, 7);
	QCOMPARE(plugin.noteCounters().pushed, std::uint32_t{1});
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.noteOnCount, std::uint32_t{1});
	QCOMPARE(state.lastKey, std::int32_t{60});
	QCOMPARE(state.lastChannel, std::int32_t{0});
	QCOMPARE(state.lastPortIndex, std::int32_t{0});   // the plug-in's own port index
	QCOMPARE(state.lastVelocityMilli, std::uint32_t{1000});
	QCOMPARE(state.firstNoteTime, std::int32_t{7});   // the offset LMMS computed arrived
	QCOMPARE(state.activeVoices, std::uint32_t{1});
	QCOMPARE(plugin.noteCounters().delivered, std::uint32_t{1});
	QCOMPARE(plugin.noteCounters().played, std::uint32_t{1});
	QVERIFY2(std::abs(rms(outputs[0]) - 0.25) < 1e-6,
		qPrintable(QStringLiteral("a sounding note wrote %1 instead of 0.25")
			.arg(rms(outputs[0]))));
	QCOMPARE(rms(outputs[1]), rms(outputs[0])); // both output channels

	// --- a parameter change travels in the same input list ----------------
	plugin.setParamPlain(InstrumentGainParamId, 0.5);
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	const auto beforeParam = state.paramEventCount;
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.paramEventCount - beforeParam, std::uint32_t{1});
	QVERIFY2(std::abs(rms(outputs[0]) - 0.125) < 1e-6,
		qPrintable(QStringLiteral("the parameter change did not reach the audio: %1")
			.arg(rms(outputs[0]))));

	// --- note-off: the release runs, then silence -------------------------
	plugin.setNoteOff(0, 60, 3);
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.noteOffCount, std::uint32_t{1});
	QVERIFY2(state.releaseLeft == 0,
		qPrintable(QStringLiteral("the release did not run out: %1 frames left").arg(state.releaseLeft)));

	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.activeVoices, std::uint32_t{0});
	QCOMPARE(rms(outputs[0]), 0.0);

	// --- note-choke silences within the same block ------------------------
	plugin.setNoteOn(0, 64, 0.8, 0);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY(rms(outputs[0]) > 0.0);
	plugin.setNoteChoke(0, 64, 0);
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.chokeCount, std::uint32_t{1});
	QCOMPARE(state.activeVoices, std::uint32_t{0});
	QCOMPARE(rms(outputs[0]), 0.0);

	// --- a chunked request delivers the note ONCE -------------------------
	// The events belong to the chunk that starts the request, so a 256-frame
	// request into a 64-frame block is four process() calls with one note.
	HostedPlugin chunked;
	QVERIFY2(chunked.load(path, QString::fromLatin1(TestInstrumentId), &error), qPrintable(error));
	QString chunkedPrepareError;
	QVERIFY2(chunked.prepare(TestSampleRate, 64, &chunkedPrepareError),
		qPrintable(chunkedPrepareError));
	const int ChunkedFrames = 256;
	std::vector<std::vector<float>> chunkedOutputs(2, std::vector<float>(ChunkedFrames, 0.0f));
	chunked.setNoteOn(0, 69, 1.0, 0);
	process(chunked, noInputs, chunkedOutputs, ChunkedFrames);
	InstrumentState chunkedState{};
	QVERIFY2(readInstrumentState(chunked, &chunkedState),
		"the fixture saved an unexpected state layout");
	QCOMPARE(chunkedState.blocks, std::uint32_t{4});
	QCOMPARE(chunkedState.noteOnCount, std::uint32_t{1});
	QCOMPARE(chunked.noteCounters().delivered, std::uint32_t{1});
	qInfo("MEASURED %d-frame request into a 64-frame block: %u plug-in calls, %u note(s) "
		  "delivered, audio level %f",
		ChunkedFrames, chunkedState.blocks, chunkedState.noteOnCount, rms(chunkedOutputs[0]));
}

void ClapHostTest::process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
	std::vector<std::vector<float>>& outputs, int frames)
{
	std::vector<const float*> inputPointers;
	inputPointers.reserve(inputs.size());
	for (const auto& channel : inputs) { inputPointers.push_back(channel.data()); }

	std::vector<float*> outputPointers;
	outputPointers.reserve(outputs.size());
	for (auto& channel : outputs) { outputPointers.push_back(channel.data()); }

	QVERIFY(plugin.process(inputPointers.data(), outputPointers.data(),
		static_cast<int>(inputPointers.size()), static_cast<int>(outputPointers.size()), frames));
}

} // namespace lmms::clap

QTEST_GUILESS_MAIN(lmms::clap::ClapHostTest)

#include "ClapHostTest.moc"
