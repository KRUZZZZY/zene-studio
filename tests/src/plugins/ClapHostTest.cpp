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

#include "ClapHost.h"

#ifndef CLAP_TEST_PLUGIN_PATH
#define CLAP_TEST_PLUGIN_PATH ""
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
