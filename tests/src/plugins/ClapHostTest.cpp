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

#include <QElapsedTimer>

#include <QFileInfo>

#include <cmath>
#include <cstdint>
#include <limits>
#include <cstring>
#include <utility>
#include <limits>
#include <limits>
#include <limits>
#include <vector>

#include <algorithm>

#include "ClapHost.h"
#include "PluginWindowSystem.h"

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
	//! params.flush() calls the host made outside process() - it answering a request_flush
	std::uint32_t hostFlushes;
};
static_assert(sizeof(PluginState) == 128, "the state layout in the fixture changed");

constexpr std::uint32_t PluginStateMagic = 0x4C4D4741u; // "LMGA"
constexpr std::uint32_t PluginStateVersion = 3u;

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
	std::uint32_t expressionCount;
	std::int32_t lastExpressionId;
	std::uint32_t lastExpressionMilli;
	std::int32_t lastExpressionKey;
};
static_assert(sizeof(InstrumentState) == 112, "the fixture's state layout changed");

constexpr std::uint32_t InstrumentStateMagic = 0x4C4D494Eu; // "LMIN"
constexpr std::uint32_t InstrumentStateVersion = 2u;

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
	void cleanup();
	void testModuleListing();
	void testIdentity();
	void testParameters();
	void testParameterSetGet();
	void testStateRoundTrip();
	void testFrameCountValidation();
	void testAudioGain();
	void testAudioSilenceWhenGainIsZero();
	void testBypass();
	void testChannelPartition();
	void testChunkedProcessing();
	void testEffectRefusesNotes();
	void testInstrumentNotePath();
	void testInstrumentRejectsNonFiniteParamMods();
	void testNoteOnRefusesNaNVelocity();
	void testTimerSupportRunsThePluginsTimer();
	void testTheEditorEmbedsResizesAndCloses();
	void testEditorScaleRejectsInfinity();
	void testMainThreadRequestsAreServiced();
	void testParameterRescansRereadTheList();
	void testAnInvalidParamRangeIsNormalized();
	void testAnActiveFlushRequestIsAnsweredWithoutAHostFlush();
	void testAZeroChannelOutputKeepsTheIndexOfLaterPorts();
	void testAnUnreadableInputPortKeepsItsNeighboursIndex();
	void testASideChainFirstPluginReadsItsOwnMainInput();
	void testANonFiniteSampleRateIsRefused();
	void testANoteVelocityKeepsItsFraction();
	void testAPluginClosedEditorIsReopened();
	void testANonFiniteParamValueNeverReachesThePlugin();
	void testReloadingAPluginWithoutNotePortsClearsTheCounters();
	void testTheWindowSystemFollowsThePlatform();
	void testTheEditorEmbedsOnEveryWindowSystem();
	void testThePluginResizesItsOwnEditorOnEveryWindowSystem();
	void testAnEditorThatCannotEmbedIsRefusedByName();
	void testCocoaIsNeverAskedToScale();

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

//! Q050-490: a failed editor case must not leave its fixture switches or window system to the next one.
void ClapHostTest::cleanup()
{
	m_plugin.closeEditor();
	qunsetenv("CLAP_TEST_GAIN_GUI_API");
	qunsetenv("CLAP_TEST_GAIN_REQUEST_RESIZE");
	qunsetenv("CLAP_TEST_GAIN_EXPECT_SCALE");
	m_plugin.setEditorScale(1.0);
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
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

void ClapHostTest::testFrameCountValidation()
{
	QVERIFY(!m_plugin.process(nullptr, nullptr, 0, 0, -1));
	QVERIFY(m_plugin.process(nullptr, nullptr, 0, 0, 0));
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

	// --- E11: an MPE member channel's pressure arrives as a note expression for the channel ---
	plugin.setNoteExpression(0, -1, CLAP_NOTE_EXPRESSION_PRESSURE, 0.5, 3);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.expressionCount, std::uint32_t{1});
	QCOMPARE(state.lastExpressionId, static_cast<std::int32_t>(CLAP_NOTE_EXPRESSION_PRESSURE));
	QCOMPARE(state.lastExpressionMilli, std::uint32_t{500});
	QCOMPARE(state.lastExpressionKey, std::int32_t{-1});

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

void ClapHostTest::testInstrumentRejectsNonFiniteParamMods()
{
	const QString path = QStringLiteral(CLAP_TEST_INSTRUMENT_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		QSKIP("no CLAP instrument fixture in this build");
	}

	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(path, QString::fromLatin1(TestInstrumentId), &error), qPrintable(error));
	QString prepareError;
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &prepareError), qPrintable(prepareError));
	const auto before = plugin.noteCounters();
	plugin.setNoteParamMod(0, 60, InstrumentGainParamId, std::numeric_limits<double>::quiet_NaN());
	plugin.setNoteParamMod(0, 60, InstrumentGainParamId, std::numeric_limits<double>::infinity());
	plugin.setNoteParamMod(0, 60, InstrumentGainParamId, -std::numeric_limits<double>::infinity());

	const std::vector<std::vector<float>> noInputs;
	std::vector<std::vector<float>> outputs(2, std::vector<float>(TestFrames, 0.0f));
	process(plugin, noInputs, outputs, TestFrames);
	InstrumentState state{};
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.eventCount, std::uint32_t{0});
	QCOMPARE(plugin.noteCounters().pushed, before.pushed);

	plugin.setNoteParamMod(0, 60, InstrumentGainParamId, 0.25);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.eventCount, std::uint32_t{1});
	QCOMPARE(state.ignoredEventCount, std::uint32_t{1});
}

//! A NaN velocity is not a note: std::clamp passes NaN through and converting
//! it to the queue's int16 velocity is undefined, so the host must refuse the
//! note-on and count it - like every event with nowhere to go - rather than
//! queue a malformed event that would start a voice at an arbitrary level.
void ClapHostTest::testNoteOnRefusesNaNVelocity()
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
	QVERIFY(plugin.acceptsNotes());
	QString prepareError;
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &prepareError), qPrintable(prepareError));

	const std::vector<std::vector<float>> noInputs;
	std::vector<std::vector<float>> outputs(2, std::vector<float>(TestFrames, 0.0f));

	const auto before = plugin.noteCounters();
	plugin.setNoteOn(0, 60, std::numeric_limits<double>::quiet_NaN(), 0);
	const auto after = plugin.noteCounters();
	QCOMPARE(after.pushed - before.pushed, std::uint32_t{0});
	QCOMPARE(after.dropped - before.dropped, std::uint32_t{1});

	// The refused note must not reach the plug-in either: no event, no voice.
	process(plugin, noInputs, outputs, TestFrames);
	QCOMPARE(plugin.noteCounters().delivered - before.delivered, std::uint32_t{0});
	InstrumentState state{};
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.noteOnCount, std::uint32_t{0});
	QCOMPARE(state.activeVoices, std::uint32_t{0});
	QCOMPARE(rms(outputs[0]), 0.0);
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

/*! R4.1: the host answers clap.timer-support. The fixture registers a 10 ms timer from its
 *  init() - before the host has fetched any plug-in extension - and the host's Qt loop must run
 *  it: the delivered count moves only if the host registered the timer AND found the plug-in's
 *  on_timer, so it proves both halves. */
void lmms::clap::ClapHostTest::testTimerSupportRunsThePluginsTimer()
{
	QCOMPARE(m_plugin.editorTimerCount(), 1);
	const std::uint32_t before = m_plugin.timerDeliveries();
	QElapsedTimer clock;
	clock.start();
	// Five calls however long a loaded runner's loop takes (the hosted macOS VM delivered 2-4 in a
	// fixed 150 ms), and never faster than the 10 ms the plug-in asked for.
	QTRY_VERIFY_WITH_TIMEOUT(m_plugin.timerDeliveries() - before >= 5, 3000);
	const std::uint32_t delivered = m_plugin.timerDeliveries() - before;
	const qint64 elapsed = clock.elapsed();
	std::printf("CLAP_TIMER_EVIDENCE 10 ms timer: %u on_timer calls in %lld ms\n", delivered, static_cast<long long>(elapsed));
	QVERIFY2(delivered <= elapsed / 10 + 1, qPrintable(QStringLiteral("%1 on_timer calls in %2 ms: faster than 10 ms").arg(delivered).arg(elapsed)));
}

/*! R4.3: the clap.gui life cycle against the fixture's embedded editor - create, set_parent,
 *  show; the size it reports; a resize adjusted to its minimum; hide + destroy on close, and a
 *  reopen. The parent is a stand-in X11 id: the fixture draws nothing into it. */
void lmms::clap::ClapHostTest::testTheEditorEmbedsResizesAndCloses()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	QString error;
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	QVERIFY(m_plugin.editorOpen());
	int width = 0;
	int height = 0;
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 400);
	QCOMPARE(height, 300);
	QVERIFY(m_plugin.resizeEditor(640, 480));
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 640);
	QCOMPARE(height, 480);
	QVERIFY(m_plugin.resizeEditor(10, 10));
	m_plugin.editorSize(&width, &height);
	QCOMPARE(width, 100);
	QCOMPARE(height, 80);
	m_plugin.closeEditor();
	QVERIFY(!m_plugin.editorOpen());
	QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(error));
	m_plugin.closeEditor();
	QVERIFY2(!m_plugin.openEditor(nullptr, &error), "an editor attached to no window");
	QVERIFY(!m_plugin.editorOpen());
}

void lmms::clap::ClapHostTest::testEditorScaleRejectsInfinity()
{
	m_plugin.setEditorScale(std::numeric_limits<double>::infinity());
	QString error;
	QVERIFY2(m_plugin.openEditor(reinterpret_cast<void*>(std::uintptr_t{0x2a}), &error), qPrintable(error));
	m_plugin.closeEditor();
	m_plugin.setEditorScale(1.0);
}

/*! Engine audit E8: a plug-in's main-thread call is made, its value rescan is honoured and its
 *  dirty state is reported - before, all three were dropped (empty callbacks, and the callback
 *  request taken and discarded). */
void lmms::clap::ClapHostTest::testMainThreadRequestsAreServiced()
{
	qputenv("CLAP_TEST_GAIN_MAIN_THREAD", "1");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	QCOMPARE(plugin.paramPlain(GainParamId), 1.0);
	// One service call makes the main-thread call, which rescans and marks dirty; the same call
	// then honours both.
	QVERIFY(plugin.serviceMainThread());
	QCOMPARE(plugin.paramPlain(GainParamId), 0.25);
	QVERIFY(!plugin.serviceMainThread());
	qunsetenv("CLAP_TEST_GAIN_MAIN_THREAD");
}

/*! W6: a plug-in's parameter rescans reach the host's list. RESCAN_INFO renames a parameter in place - the same
 *  list and ids, nothing re-prepared; RESCAN_ALL, asked for the CLAP way (a restart, then the rescan from
 *  deactivate()), re-reads the list in the next prepare(): the removed parameter is gone, the new ones are there
 *  with their values, and parameter changes still reach the audio. Before, both left the list as it was loaded. */
void lmms::clap::ClapHostTest::testParameterRescansRereadTheList()
{
	constexpr std::uint32_t DriveParamId = 3;
	qputenv("CLAP_TEST_GAIN_RESCAN", "info");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	const auto loaded = plugin.paramsRevision();
	QCOMPARE(plugin.parameters()[0].title, QStringLiteral("Gain"));
	plugin.serviceMainThread();  // the plug-in renames Gain and asks for an info rescan
	QCOMPARE(plugin.parameters()[0].title, QStringLiteral("Level"));
	QCOMPARE(plugin.parameters().size(), std::size_t{2});
	QVERIFY(plugin.paramsRevision() != loaded);
	QVERIFY(!plugin.needsReprepare());

	qputenv("CLAP_TEST_GAIN_RESCAN", "all");
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));  // activate() asks for a call
	plugin.serviceMainThread();  // the plug-in asks for a restart
	qunsetenv("CLAP_TEST_GAIN_RESCAN");
	QVERIFY(plugin.needsReprepare());
	QCOMPARE(plugin.parameters().size(), std::size_t{2});  // nothing is re-read while it is active
	const auto active = plugin.paramsRevision();
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	QVERIFY(plugin.paramsRevision() != active);
	QVERIFY(!plugin.needsReprepare());
	QCOMPARE(plugin.parameters().size(), std::size_t{3});
	QCOMPARE(plugin.parameters()[0].title, QStringLiteral("Level"));
	QCOMPARE(plugin.paramIndex(BypassParamId), -1);
	QCOMPARE(plugin.paramIndex(DriveParamId), 1);
	QCOMPARE(plugin.paramPlain(DriveParamId), 2.0);
	QCOMPARE(plugin.parameters()[1].maxValue, 10.0);
	plugin.setParamPlain(DriveParamId, 5.0);
	QCOMPARE(plugin.paramPlain(DriveParamId), 5.0);
	const auto& layout = plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	plugin.setParamPlain(GainParamId, 0.5);
	process(plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

/*! A plug-in that reports min > max used to have those bounds stored verbatim, and every later
 *  std::clamp(value, min, max) ran with unordered bounds. The host now orders them at load, so the
 *  stored range is valid and the value the plug-in itself reports survives the clamp. */
void lmms::clap::ClapHostTest::testAnInvalidParamRangeIsNormalized()
{
	qputenv("CLAP_TEST_GAIN_BAD_RANGE", "1");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));

	const auto& gain = plugin.parameters()[0];
	QCOMPARE(gain.id, GainParamId);
	QVERIFY2(gain.minValue <= gain.maxValue,
		qPrintable(QStringLiteral("the inverted range survived: %1 .. %2")
			.arg(gain.minValue).arg(gain.maxValue)));
	QCOMPARE(gain.minValue, 0.0);
	QCOMPARE(gain.maxValue, 1.0);
	QVERIFY2(gain.defaultValue >= gain.minValue && gain.defaultValue <= gain.maxValue,
		"the default value lies outside the range");

	// the value the host holds (loaded through the very clamp) is inside the ordered range
	const auto held = plugin.paramPlain(GainParamId);
	QVERIFY2(held >= gain.minValue && held <= gain.maxValue,
		qPrintable(QStringLiteral("the held value %1 is outside %2 .. %3")
			.arg(held).arg(gain.minValue).arg(gain.maxValue)));

	// setting a value goes through the same clamp and lands inside the range
	plugin.setParamPlain(GainParamId, 5.0);
	QVERIFY(plugin.paramPlain(GainParamId) >= gain.minValue);
	QVERIFY(plugin.paramPlain(GainParamId) <= gain.maxValue);
	plugin.setParamPlain(GainParamId, -5.0);
	QVERIFY(plugin.paramPlain(GainParamId) >= gain.minValue);
	qunsetenv("CLAP_TEST_GAIN_BAD_RANGE");
}

/*! A plug-in that changes a value while it is ACTIVE (its own UI) and asks for a flush gets that value back
 *  through the next process() call: the plug-in returns the changed value in its output events and the host
 *  publishes it. CLAP would also accept the request being answered by params.flush() (params.h:373-375), and
 *  says process() alone already achieves the update while processing (params.h:297-301). The host's own
 *  choice is to make no flush call of its own here, so this test pins that choice and not a CLAP requirement. */
void lmms::clap::ClapHostTest::testAnActiveFlushRequestIsAnsweredWithoutAHostFlush()
{
	qputenv("CLAP_TEST_GAIN_REQUEST_FLUSH", "1");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	PluginState state{};
	QVERIFY2(readPluginState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.hostFlushes, std::uint32_t{0});

	const auto& layout = plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	process(plugin, inputs, outputs, TestFrames);  // start_processing(): the plug-in asks for its own call
	plugin.serviceMainThread();  // the plug-in's own call: it changes its gain and asks for the flush
	QVERIFY2(plugin.paramPlain(GainParamId) != 0.75,
		"the changed value reached the host before the process() call that returns it");

	process(plugin, inputs, outputs, TestFrames);  // answers the request: the value comes back in process()
	QCOMPARE(plugin.paramPlain(GainParamId), 0.75);
	QVERIFY2(readPluginState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.hostFlushes, std::uint32_t{0});  // the host's choice: it makes no flush call of its own

	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	process(plugin, inputs, outputs, TestFrames);
	QCOMPARE(plugin.paramPlain(GainParamId), 0.75);
	QVERIFY2(readPluginState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.hostFlushes, std::uint32_t{0});
	qunsetenv("CLAP_TEST_GAIN_REQUEST_FLUSH");
}

/*! A sample rate that is not a usable finite positive number is refused before activate(). The host
 *  used to reject only <= 0, and every comparison against NaN is false, so a NaN or an infinity
 *  reached the plug-in's activate() as an invalid rate. */
void lmms::clap::ClapHostTest::testANonFiniteSampleRateIsRefused()
{
	constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
	constexpr double Infinity = std::numeric_limits<double>::infinity();
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));

	// The zero and negative cases were already refused; they are the control for the two below.
	QVERIFY(!plugin.prepare(0.0, TestBlockSize, &error));
	QVERIFY(!error.isEmpty());
	error.clear();
	QVERIFY(!plugin.prepare(-TestSampleRate, TestBlockSize, &error));
	QVERIFY(!error.isEmpty());
	error.clear();

	QVERIFY2(!plugin.prepare(NaN, TestBlockSize, &error), "a NaN sample rate reached activate()");
	QVERIFY(!error.isEmpty());
	error.clear();
	QVERIFY2(!plugin.prepare(Infinity, TestBlockSize, &error), "an infinite sample rate reached activate()");
	QVERIFY(!error.isEmpty());

	// A refused prepare leaves nothing prepared, and a good rate still works afterwards.
	QVERIFY(!plugin.needsReprepare());
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
}

/*! A note-on's velocity is CLAP's own double in 0..1, and the plug-in must be handed that number,
 *  not a MIDI-scale integer that is converted back. The host rounded it to 0..127 on the way in, so
 *  0.5 arrived as 63/127 (the fixture records the velocity in millis, and saw 496). */
void lmms::clap::ClapHostTest::testANoteVelocityKeepsItsFraction()
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
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	QVERIFY(plugin.acceptsNotes());

	const std::vector<std::vector<float>> noInputs;
	std::vector<std::vector<float>> outputs(2, std::vector<float>(TestFrames, 0.0f));

	// Each velocity is the one the plug-in must receive, in millis (the fixture's own unit): the
	// values a 7-bit round trip cannot represent.
	const std::vector<std::pair<double, std::uint32_t>> cases{
		{0.5, 500}, {0.25, 250}, {0.1, 100}, {1.0, 1000}, {0.0, 0}};
	std::int16_t key = 60;
	for (const auto& [velocity, expectedMilli] : cases)
	{
		plugin.setNoteOn(0, key, velocity, 0);
		process(plugin, noInputs, outputs, TestFrames);
		InstrumentState state{};
		QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
		QCOMPARE(state.lastVelocityMilli, expectedMilli);
		plugin.setNoteOff(0, key, 0);
		process(plugin, noInputs, outputs, TestFrames);
		++key;
	}

	// Out-of-range velocities are still clamped to CLAP's 0..1 scale.
	plugin.setNoteOn(0, 80, 2.5, 0);
	process(plugin, noInputs, outputs, TestFrames);
	InstrumentState state{};
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.lastVelocityMilli, std::uint32_t{1000});
	plugin.setNoteOn(0, 81, -1.0, 0);
	process(plugin, noInputs, outputs, TestFrames);
	QVERIFY2(readInstrumentState(plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.lastVelocityMilli, std::uint32_t{0});
}

/*! A plug-in that closes its OWN editor (clap.gui's closed callback) leaves the host's GUI state
 *  stale: guiClosed() only set a flag, so a reopen returned true without showing anything, and a
 *  GUI the plug-in had already destroyed was hidden and destroyed a second time. Both cases of
 *  was_destroyed are covered: "hide" leaves the GUI alive, "destroy" takes it down. */
void lmms::clap::ClapHostTest::testAPluginClosedEditorIsReopened()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});

	// --- was_destroyed = false: the GUI is alive but hidden, so a reopen SHOWS it again ---
	qputenv("CLAP_TEST_GAIN_CLOSE_GUI", "hide");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	QVERIFY2(plugin.openEditor(standInWindow, &error), qPrintable(error));
	QVERIFY(plugin.editorOpen());
	plugin.serviceMainThread();  // the plug-in closes its own editor (the return is the state-dirty flag)
	QVERIFY(!plugin.editorOpen());
	QVERIFY2(plugin.openEditor(standInWindow, &error),
		qPrintable(QStringLiteral("reopening a GUI the plug-in closed: %1").arg(error)));
	QVERIFY(plugin.editorOpen());
	plugin.closeEditor();
	qunsetenv("CLAP_TEST_GAIN_CLOSE_GUI");

	// --- was_destroyed = true: the GUI is gone, so a reopen CREATES a fresh one ---
	qputenv("CLAP_TEST_GAIN_CLOSE_GUI", "destroy");
	HostedPlugin recreating;
	QString recreateError;
	QVERIFY2(recreating.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId),
			&recreateError), qPrintable(recreateError));
	QVERIFY2(recreating.prepare(TestSampleRate, TestBlockSize, &recreateError), qPrintable(recreateError));
	QVERIFY2(recreating.openEditor(standInWindow, &recreateError), qPrintable(recreateError));
	recreating.serviceMainThread();  // the plug-in destroys its own editor
	QVERIFY(!recreating.editorOpen());
	QVERIFY2(recreating.openEditor(standInWindow, &recreateError),
		qPrintable(QStringLiteral("reopening a destroyed GUI: %1").arg(recreateError)));
	QVERIFY(recreating.editorOpen());
	// The reopened editor is a live one: the new instance is created at the fixture's default size,
	// not the size of an editor that no longer exists.
	int width = 0;
	int height = 0;
	recreating.editorSize(&width, &height);
	QCOMPARE(width, 400);
	QCOMPARE(height, 300);
	recreating.closeEditor();
	qunsetenv("CLAP_TEST_GAIN_CLOSE_GUI");
}

/*! A non-finite value must never reach a plug-in, from either side. std::clamp does NOT sanitize
 *  NaN: every comparison against it is false, so a NaN is returned unchanged and stored verbatim -
 *  and then published to the processor as a CLAP_EVENT_PARAM_VALUE. The host now refuses a
 *  non-finite value at both boundaries (the caller and the plug-in's own reported value). */
void lmms::clap::ClapHostTest::testANonFiniteParamValueNeverReachesThePlugin()
{
	constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
	constexpr double Infinity = std::numeric_limits<double>::infinity();

	// --- the caller sets a non-finite value: the held value stays the last good one ---
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	plugin.setParamPlain(GainParamId, 0.5);
	QCOMPARE(plugin.paramPlain(GainParamId), 0.5);
	plugin.setParamPlain(GainParamId, NaN);
	QVERIFY2(std::isfinite(plugin.paramPlain(GainParamId)),
		"a NaN the caller set was stored in the parameter slot");
	QVERIFY2(plugin.paramPlain(GainParamId) >= 0.0 && plugin.paramPlain(GainParamId) <= 1.0,
		"a NaN the caller set landed outside the parameter's range");
	plugin.setParamPlain(GainParamId, Infinity);
	QVERIFY2(std::isfinite(plugin.paramPlain(GainParamId)),
		"an infinite value the caller set was stored in the parameter slot");
	QVERIFY2(plugin.paramPlain(GainParamId) >= 0.0 && plugin.paramPlain(GainParamId) <= 1.0,
		"an infinite value the caller set landed outside the parameter's range");

	// --- the PLUG-IN reports a non-finite value: what it read back is still finite, and is what
	//     the processor is handed ---
	qputenv("CLAP_TEST_GAIN_NAN_VALUE", "1");
	HostedPlugin reporting;
	QVERIFY2(reporting.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(reporting.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	const auto held = reporting.paramPlain(GainParamId);
	QVERIFY2(std::isfinite(held),
		qPrintable(QStringLiteral("the plug-in's own NaN was stored: %1").arg(held)));
	const auto& gain = reporting.parameters()[0];
	QVERIFY2(held >= gain.minValue && held <= gain.maxValue,
		qPrintable(QStringLiteral("the held value %1 is outside %2 .. %3")
			.arg(held).arg(gain.minValue).arg(gain.maxValue)));

	// processing it must not put a NaN into the audio either
	const std::vector<std::vector<float>> noInputs;
	std::vector<std::vector<float>> outputs(2, std::vector<float>(TestFrames, 0.0f));
	process(reporting, noInputs, outputs, TestFrames);
	for (const auto& channel : outputs)
	{
		for (const auto sample : channel)
		{
			QVERIFY2(std::isfinite(sample), "a NaN parameter value reached the processed audio");
		}
	}
	qunsetenv("CLAP_TEST_GAIN_NAN_VALUE");
}

/*! The per-instance note counters describe THIS instance, so reloading a plug-in that takes no
 *  notes must start them over. scanNotePorts() returned before resetting them when the plug-in had
 *  no clap.note-ports extension at all, so an instance reloaded onto such a plug-in still reported
 *  the previous instance's totals. */
void lmms::clap::ClapHostTest::testReloadingAPluginWithoutNotePortsClearsTheCounters()
{
	const QString instrumentPath = QStringLiteral(CLAP_TEST_INSTRUMENT_PATH);
	if (instrumentPath.isEmpty() || !QFileInfo::exists(instrumentPath))
	{
		QSKIP("no CLAP instrument fixture in this build (tests/data/clap-test-plugin/"
			"clap-test-instrument.c, built when the pinned CLAP headers are present)");
	}

	// --- an instance that DOES take notes: its counters move ---
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(instrumentPath, QString::fromLatin1(TestInstrumentId), &error), qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
	QVERIFY(plugin.acceptsNotes());
	QCOMPARE(plugin.noteCounters().ports, std::uint32_t{1});
	plugin.setNoteOn(0, 60, 1.0, 0);
	plugin.setNoteOff(0, 60, 0);
	// Delivering is what process() does, so the block is run: pushed, delivered and played all move.
	const std::vector<std::vector<float>> noInputs;
	std::vector<std::vector<float>> outputs(2, std::vector<float>(TestFrames, 0.0f));
	process(plugin, noInputs, outputs, TestFrames);
	const auto busy = plugin.noteCounters();
	QCOMPARE(busy.pushed, std::uint32_t{2});
	QCOMPARE(busy.delivered, std::uint32_t{2});
	QCOMPARE(busy.played, std::uint32_t{1});

	// --- the SAME instance reloaded onto a plug-in with no note ports: zeroed, not inherited ---
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY(!plugin.acceptsNotes());
	QCOMPARE(plugin.noteInputPorts().size(), std::size_t{0});
	const auto after = plugin.noteCounters();
	QCOMPARE(after.ports, std::uint32_t{0});
	QVERIFY2(after.pushed == 0 && after.dropped == 0 && after.delivered == 0 && after.played == 0,
		qPrintable(QStringLiteral("the reloaded instance kept the old counters: pushed %1, dropped %2, "
			"delivered %3, played %4")
			.arg(after.pushed).arg(after.dropped).arg(after.delivered).arg(after.played)));
}

/*! A zero-channel output port ahead of the real one must not shift the real output. The plug-in writes its
 *  main output at its own port index 1, so the host has to hand that index the stereo buffer. Zero-channel
 *  ports used to be dropped from the buffer list, which put the real output at index 0. */
void lmms::clap::ClapHostTest::testAZeroChannelOutputKeepsTheIndexOfLaterPorts()
{
	qputenv("CLAP_TEST_GAIN_LAYOUT", "zero-output");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));

	const auto& layout = plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	process(plugin, inputs, outputs, TestFrames);
	for (const auto& channel : outputs)
	{
		QVERIFY2(std::all_of(channel.begin(), channel.end(), [](float v) { return v == 0.5f; }),
			"the main output did not receive the main input: the buffer went to another port");
	}
	QCOMPARE(layout.pluginOutputPorts, 2);
	QCOMPARE(layout.outputPortIndices, std::vector<int>{1});
	qunsetenv("CLAP_TEST_GAIN_LAYOUT");
}

/*! A plug-in whose clap.audio-ports get() fails for one input must keep that port's slot: the main input is
 *  the plug-in's port 1, and the host has to hand it audio_inputs[1]. Dropping the unreadable port used to
 *  put the main input at index 0, and the plug-in then found no main input at its own index. */
void lmms::clap::ClapHostTest::testAnUnreadableInputPortKeepsItsNeighboursIndex()
{
	qputenv("CLAP_TEST_GAIN_LAYOUT", "unreadable-input");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));

	const auto& layout = plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	process(plugin, inputs, outputs, TestFrames);
	for (const auto& channel : outputs)
	{
		QVERIFY2(std::all_of(channel.begin(), channel.end(), [](float v) { return v == 0.5f; }),
			"the main input did not reach the plug-in's main port: the unreadable port shifted the indices");
	}
	QCOMPARE(layout.pluginInputPorts, 2);
	QCOMPARE(layout.inputPortIndices, std::vector<int>{1});
	qunsetenv("CLAP_TEST_GAIN_LAYOUT");
}

/*! A plug-in that reports its side-chain input BEFORE its main one reads its main signal at its own index 1.
 *  The transport lays the main channels first, so runChunk must hand audio_inputs[1] the transport's first
 *  channels, not the side-chain. The side-chain is fed at 0.25 and the main at 0.5, so a misplaced buffer
 *  reads back as 0.25. */
void lmms::clap::ClapHostTest::testASideChainFirstPluginReadsItsOwnMainInput()
{
	qputenv("CLAP_TEST_GAIN_LAYOUT", "sidechain-first");
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
		qPrintable(error));
	QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));

	const auto& layout = plugin.busLayout();
	QVERIFY(layout.hasSideChain);
	QCOMPARE(layout.sideChainChannel, 2);
	// transport order: the main channels first (0-1), then the side-chain (2-3)
	std::vector<std::vector<float>> inputs;
	for (int c = 0; c < layout.inputs; ++c)
	{
		inputs.emplace_back(TestFrames, c < layout.sideChainChannel ? 0.5f : 0.25f);
	}
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	process(plugin, inputs, outputs, TestFrames);
	for (const auto& channel : outputs)
	{
		QVERIFY2(std::all_of(channel.begin(), channel.end(), [](float v) { return v == 0.5f; }),
			"the plug-in's main input carried the side-chain: runChunk placed the buffers out of port order");
	}
	QCOMPARE(layout.pluginInputPorts, 2);
	QCOMPARE(layout.inputPortIndices, (std::vector<int>{1, 0}));
	qunsetenv("CLAP_TEST_GAIN_LAYOUT");
}

namespace
{
//! Q050-490: the three window systems, with the name and clap.gui api a refusal must carry.
struct WindowCase
{
	lmms::PluginWindowSystem system;
	const char* name;
	const char* api;
};
constexpr WindowCase WindowCases[] = {
	{lmms::PluginWindowSystem::X11, "X11", "x11"},
	{lmms::PluginWindowSystem::Win32, "Windows", "win32"},
	{lmms::PluginWindowSystem::Cocoa, "macOS", "cocoa"},
};
} // namespace

/*! Q050-490: the window system is read from the build's platform and the running Qt platform -
 *  Windows embeds in an HWND, macOS in an NSView, elsewhere X11 (xcb, and the offscreen platform
 *  the tests run on) unless the session is native Wayland, which has no X11 window to give. */
void lmms::clap::ClapHostTest::testTheWindowSystemFollowsThePlatform()
{
#if defined(_WIN32)
	QVERIFY(pluginWindowSystemFor(QStringLiteral("windows")) == PluginWindowSystem::Win32);
	QVERIFY(pluginWindowSystemFor(QStringLiteral("offscreen")) == PluginWindowSystem::Win32);
#elif defined(__APPLE__)
	QVERIFY(pluginWindowSystemFor(QStringLiteral("cocoa")) == PluginWindowSystem::Cocoa);
	QVERIFY(pluginWindowSystemFor(QStringLiteral("offscreen")) == PluginWindowSystem::Cocoa);
#else
	QVERIFY(pluginWindowSystemFor(QStringLiteral("xcb")) == PluginWindowSystem::X11);
	QVERIFY(pluginWindowSystemFor(QStringLiteral("offscreen")) == PluginWindowSystem::X11);
	QVERIFY(pluginWindowSystemFor(QStringLiteral("wayland")) == PluginWindowSystem::None);
	QVERIFY(pluginWindowSystemFor(QStringLiteral("wayland-egl")) == PluginWindowSystem::None);
#endif
}

/*! Q050-490: the clap.gui life cycle on each window system - created for x11, win32 or cocoa, the
 *  parent handed in that api's union member (the fixture reads only that one, so a handle in the
 *  wrong member is no parent), sized, resized by the host and to its minimum, closed and reopened.
 *  The window system is chosen explicitly so all three run on every machine; the default (the
 *  running platform's) is what testTheEditorEmbedsResizesAndCloses runs on each hosted job. */
void lmms::clap::ClapHostTest::testTheEditorEmbedsOnEveryWindowSystem()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	for (const WindowCase& each : WindowCases)
	{
		// The fixture takes ONLY this api: an editor that opens was created and parented with it.
		qputenv("CLAP_TEST_GAIN_GUI_API", each.api);
		m_plugin.setEditorWindowSystem(each.system);
		QString error;
		QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(each.name), error)));
		QVERIFY(m_plugin.editorOpen());
		int width = 0;
		int height = 0;
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 400);
		QCOMPARE(height, 300);
		QVERIFY(m_plugin.resizeEditor(640, 480));
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 640);
		QCOMPARE(height, 480);
		QVERIFY(m_plugin.resizeEditor(10, 10));
		m_plugin.editorSize(&width, &height);
		QCOMPARE(width, 100);
		QCOMPARE(height, 80);
		m_plugin.closeEditor();
		QVERIFY(!m_plugin.editorOpen());
		QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1 reopen: %2").arg(QLatin1String(each.name), error)));
		QVERIFY(m_plugin.editorOpen());
		m_plugin.closeEditor();
		std::printf("CLAP_EDITOR_PLATFORM_EVIDENCE %s (%s): open 400x300, resize 640x480, min 100x80, close, reopen\n",
			each.name, each.api);
	}
	qunsetenv("CLAP_TEST_GAIN_GUI_API");
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

/*! Q050-490: a resize the plug-in makes from its own editor (clap.gui request_resize) is granted on
 *  each window system - the host answers it with set_size, and the editor reports the new size. */
void lmms::clap::ClapHostTest::testThePluginResizesItsOwnEditorOnEveryWindowSystem()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	qputenv("CLAP_TEST_GAIN_REQUEST_RESIZE", "520x390");
	for (const WindowCase& each : WindowCases)
	{
		qputenv("CLAP_TEST_GAIN_GUI_API", each.api);
		HostedPlugin plugin;
		QString error;
		QVERIFY2(plugin.load(QStringLiteral(CLAP_TEST_PLUGIN_PATH), QString::fromLatin1(TestPluginId), &error),
			qPrintable(error));
		QVERIFY2(plugin.prepare(TestSampleRate, TestBlockSize, &error), qPrintable(error));
		plugin.setEditorWindowSystem(each.system);
		QVERIFY2(plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(each.name), error)));
		plugin.serviceMainThread();  // the plug-in asks for 520x390 from its main-thread callback
		int width = 0;
		int height = 0;
		plugin.editorSize(&width, &height);
		QCOMPARE(width, 520);
		QCOMPARE(height, 390);
		plugin.closeEditor();
	}
	qunsetenv("CLAP_TEST_GAIN_GUI_API");
	qunsetenv("CLAP_TEST_GAIN_REQUEST_RESIZE");
}

/*! Q050-490: a plug-in whose editor cannot embed in this platform's window is refused with the
 *  platform and the window API named - nothing is created, so no empty window is left behind. A
 *  session with no window to embed in (native Wayland) is refused before the plug-in is asked. */
void lmms::clap::ClapHostTest::testAnEditorThatCannotEmbedIsRefusedByName()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	for (const WindowCase& each : WindowCases)
	{
		// A plug-in that embeds only in some OTHER api.
		qputenv("CLAP_TEST_GAIN_GUI_API", each.system == PluginWindowSystem::X11 ? "win32" : "x11");
		m_plugin.setEditorWindowSystem(each.system);
		QString error;
		QVERIFY2(!m_plugin.openEditor(standInWindow, &error), each.name);
		QVERIFY(!m_plugin.editorOpen());
		const QString expected = QStringLiteral("cannot embed on %1 (CLAP window API %2)").arg(QLatin1String(each.name),
			QLatin1String(each.api));
		QVERIFY2(error.contains(expected), qPrintable(error));
		std::printf("CLAP_EDITOR_REFUSAL_EVIDENCE %s\n", qPrintable(error));
	}
	qunsetenv("CLAP_TEST_GAIN_GUI_API");
	m_plugin.setEditorWindowSystem(PluginWindowSystem::None);
	QString error;
	QVERIFY(!m_plugin.openEditor(standInWindow, &error));
	QVERIFY(!m_plugin.editorOpen());
	QVERIFY2(error.contains(QLatin1String("no native window a plug-in editor can embed in")), qPrintable(error));
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

/*! Q050-490: cocoa forbids set_scale, and win32/x11 must receive the device's zoom. The fixture
 *  refuses a cocoa set_scale and requires an exact 1.5 set_scale before parenting on win32/x11. */
void lmms::clap::ClapHostTest::testCocoaIsNeverAskedToScale()
{
	void* const standInWindow = reinterpret_cast<void*>(std::uintptr_t{0x2a});
	m_plugin.setEditorScale(1.5);
	for (const WindowCase& each : WindowCases)
	{
		m_plugin.setEditorWindowSystem(each.system);
		if (each.system == PluginWindowSystem::Cocoa) { qunsetenv("CLAP_TEST_GAIN_EXPECT_SCALE"); }
		else { qputenv("CLAP_TEST_GAIN_EXPECT_SCALE", "1.5"); }
		QString error;
		QVERIFY2(m_plugin.openEditor(standInWindow, &error), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(each.name), error)));
		m_plugin.closeEditor();
	}
	qunsetenv("CLAP_TEST_GAIN_EXPECT_SCALE");
	m_plugin.setEditorScale(1.0);
	m_plugin.setEditorWindowSystem(currentPluginWindowSystem());
}

QTEST_GUILESS_MAIN(lmms::clap::ClapHostTest)

#include "ClapHostTest.moc"
