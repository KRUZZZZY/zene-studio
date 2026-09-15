/*
 * Vst3ChunkProbeTest.cpp - CODE-4: the VST3 host processes exactly the frames
 *                          it is asked for, chunked, with no over-run
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * WHAT THIS PROVES. The host was prepared for a 512-frame block and is then
 * asked for a request that is larger than that and not a multiple of it
 * (2 * 512 + 37 = 1061 frames). The contract the host now keeps is stated on
 * HostedPlugin::process() in plugins/Vst3Effect/Vst3Host.h:
 *
 *   1. all 1061 frames are processed, in three chunks (512, 512, 37), so the
 *      plug-in is asked for 37 frames in its last call rather than being asked
 *      for 1061 in one call it was never prepared for;
 *   2. every frame of every channel the caller supplied is written, and the
 *      plug-in never sees a call larger than the block size setupProcessing()
 *      declared - which is the over-run this lane removed;
 *   3. a second instance of the same plug-in, prepared for the whole 1061-frame
 *      request instead of one 512-frame block, produces byte-identical audio:
 *      chunking is transparent, not an approximation.
 *
 * The subject is tests/data/vst3-chunk-probe/, a purpose-built in-tree MIT
 * fixture, because no third-party VST3 plug-in can be installed on this
 * machine. It reports what it was asked for through its own state; the layout
 * is mirrored in ProbeState below.
 */

#include <QtTest>

#include <QFileInfo>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Vst3Host.h"

#ifndef VST3_CHUNK_PROBE_PATH
#define VST3_CHUNK_PROBE_PATH ""
#endif

namespace lmms::vst3
{

namespace
{
constexpr double TestSampleRate = 48000.0;
constexpr int TestBlockSize = 512;

//! The fixture, tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp.
constexpr const char* TestPluginClass = "Zene VST3 Chunk Probe";
constexpr std::uint32_t GainParamId = 100;

/*! Mirrors ProbeState in the fixture: the plug-in's own account of the calls
 * it was given, saved through IComponent::getState and read back by the host's
 * saveState(). All members are 4 bytes wide, so the layout has no padding.
 */
struct ProbeState
{
	std::int32_t magic;
	std::int32_t version;
	float gain;
	std::int32_t declaredMaxBlock;
	std::int32_t blocks;
	std::int32_t framesProcessed;
	std::int32_t maxBlock;
	std::int32_t oversizedCalls;
	std::int32_t recordedCount;
	std::int32_t recorded[16];
};
static_assert(sizeof(ProbeState) == 100, "the state layout in the fixture changed");

constexpr std::int32_t ProbeStateMagic = 0x43484E4B; // 'CHNK'
constexpr std::int32_t ProbeStateVersion = 1;
constexpr std::int32_t MaxRecorded = 16;

auto readProbeState(HostedPlugin& plugin, ProbeState* state) -> bool
{
	QByteArray component;
	QByteArray controller;
	if (!plugin.saveState(&component, &controller)) { return false; }
	if (component.size() != static_cast<int>(sizeof(ProbeState))) { return false; }
	std::memcpy(state, component.constData(), sizeof(ProbeState));
	return state->magic == ProbeStateMagic && state->version == ProbeStateVersion;
}
} // namespace

class Vst3ChunkProbeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void testIdentity();
	void testChunkedProcessing();
	void testChannelWithoutACallerBufferOverAFullRequest();
	void testZeroFramesIsANoOp();

private:
	void process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
		std::vector<std::vector<float>>& outputs, int frames);

	HostedPlugin m_plugin;
};

void Vst3ChunkProbeTest::initTestCase()
{
	const QString path = QStringLiteral(VST3_CHUNK_PROBE_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		QSKIP("no VST3 chunk probe available (build tests/data/vst3-chunk-probe and "
			  "configure with -DLMMS_VST3_SDK_PATH=<checkout>)");
	}
	QString error;
	QVERIFY2(m_plugin.load(path, QString::fromLatin1(TestPluginClass), &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QString prepareError;
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &prepareError),
		qPrintable(prepareError));
	QCOMPARE(m_plugin.isInstrument(), false);

	const auto& layout = m_plugin.busLayout();
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
}

void Vst3ChunkProbeTest::testIdentity()
{
	QCOMPARE(m_plugin.className(), QString::fromLatin1(TestPluginClass));
	QCOMPARE(m_plugin.vendor(), QStringLiteral("Zene Studio test fixture"));
	QVERIFY(m_plugin.paramIndex(GainParamId) >= 0);

	// the block size the plug-in was prepared with, as the plug-in itself saw it
	ProbeState state{};
	QVERIFY2(readProbeState(m_plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.declaredMaxBlock, static_cast<std::int32_t>(TestBlockSize));
}

/*!
 * The case this lane exists for: a request larger than the prepared block, and
 * not a multiple of it. Before this change the host set
 * `processData.numSamples = frames` (1061) and handed the plug-in one call with
 * buffers - `silence` and `scratchOutput` - sized to the prepared block, so the
 * plug-in was asked to read and write 1061 frames of memory that was 512 long.
 */
void Vst3ChunkProbeTest::testChunkedProcessing()
{
	constexpr int Prepared = TestBlockSize;    // 512
	constexpr int Frames = Prepared * 2 + 37;  // 1061
	constexpr float Stale = -12345.0f;
	constexpr float Input = 0.25f;
	constexpr float Expected = Input * 0.5f; // gain 0.5

	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(Frames, Input));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(Frames, Stale));

	m_plugin.setParamNormalized(GainParamId, 0.5f);
	ProbeState beforeState{};
	QVERIFY2(readProbeState(m_plugin, &beforeState), "the fixture saved an unexpected state layout");
	const auto before = hostChunkingStats();
	process(m_plugin, inputs, outputs, Frames);
	const auto after = hostChunkingStats();

	// 1. every frame of every channel the caller supplied was written by the
	//    plug-in - the same check that fails if a chunk is skipped or truncated
	for (std::size_t channel = 0; channel < outputs.size(); ++channel)
	{
		for (int frame = 0; frame < Frames; ++frame)
		{
			QVERIFY2(outputs[channel][frame] == Expected,
				qPrintable(QStringLiteral("channel %1 frame %2 holds %3, not %4 "
										  "(%5-frame request into a %6-frame prepared block)")
							   .arg(channel)
							   .arg(frame)
							   .arg(outputs[channel][frame])
							   .arg(Expected)
							   .arg(Frames)
							   .arg(Prepared)));
		}
	}

	// 2. the plug-in's own account of the calls it was given
	ProbeState state{};
	QVERIFY2(readProbeState(m_plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.declaredMaxBlock, static_cast<std::int32_t>(Prepared));
	QCOMPARE(state.blocks - beforeState.blocks, std::int32_t{3});
	QCOMPARE(state.framesProcessed - beforeState.framesProcessed, std::int32_t{Frames});
	QCOMPARE(state.oversizedCalls - beforeState.oversizedCalls, std::int32_t{0});
	QCOMPARE(state.recordedCount - beforeState.recordedCount, std::int32_t{3});
	QVERIFY2(state.recordedCount <= MaxRecorded, "the fixture's recorded-block ring overflowed");
	const std::int32_t first = beforeState.recordedCount;
	QCOMPARE(state.recorded[first + 0], std::int32_t{Prepared});
	QCOMPARE(state.recorded[first + 1], std::int32_t{Prepared});
	QCOMPARE(state.recorded[first + 2], std::int32_t{Frames - 2 * Prepared});
	QCOMPARE(state.maxBlock, std::int32_t{Prepared});
	qInfo("MEASURED chunk sequence for a %d-frame request into a %d-frame block: %d, %d, %d",
		Frames, Prepared, state.recorded[first], state.recorded[first + 1],
		state.recorded[first + 2]);

	// 3. the host's own counters say the same
	QCOMPARE(after.requests - before.requests, std::uint64_t{1});
	QCOMPARE(after.chunks - before.chunks, std::uint64_t{3});
	QCOMPARE(after.frames - before.frames, static_cast<std::uint64_t>(Frames));
	QCOMPARE(after.multiChunkRequests - before.multiChunkRequests, std::uint64_t{1});
	QCOMPARE(after.framesBeyondPreparedBlock - before.framesBeyondPreparedBlock,
		static_cast<std::uint64_t>(Frames - Prepared));

	// 4. a second instance of the same fixture, prepared for the whole request
	//    instead of one block, produces identical audio
	HostedPlugin wide;
	QString loadError;
	QVERIFY2(wide.load(QStringLiteral(VST3_CHUNK_PROBE_PATH), QString::fromLatin1(TestPluginClass),
			 &loadError),
		qPrintable(loadError));
	QString prepareError;
	QVERIFY2(wide.prepare(TestSampleRate, Frames, &prepareError), qPrintable(prepareError));
	wide.setParamNormalized(GainParamId, 0.5f);
	std::vector<std::vector<float>> wideOutputs(layout.outputs, std::vector<float>(Frames, Stale));
	process(wide, inputs, wideOutputs, Frames);

	for (std::size_t channel = 0; channel < wideOutputs.size(); ++channel)
	{
		for (int frame = 0; frame < Frames; ++frame)
		{
			QCOMPARE(wideOutputs[channel][frame], outputs[channel][frame]);
		}
	}
	ProbeState wideState{};
	QVERIFY2(readProbeState(wide, &wideState), "the fixture saved an unexpected state layout");
	QCOMPARE(wideState.declaredMaxBlock, std::int32_t{Frames});
	QCOMPARE(wideState.blocks, std::int32_t{1});
	QCOMPARE(wideState.framesProcessed, std::int32_t{Frames});
	qInfo("MEASURED same %d frames: one instance asked for %d-frame blocks and one for %d-frame "
		  "blocks, identical output",
		Frames, state.maxBlock, wideState.maxBlock);

	m_plugin.setParamNormalized(GainParamId, 1.0f);
}

/*!
 * The mapping the over-run actually corrupted: the plug-in declares two input
 * channels and the caller supplies one, so the host hands the plug-in its
 * zeroed `silence` block for channel 1 and its `scratchOutput` block if an
 * output channel is missing too. Both blocks are exactly the prepared block
 * size, so before this change a request of 1061 frames made the plug-in read
 * 1061 frames out of a 512-frame block and write 1061 frames into another. The
 * observable contract now: the request is processed whole, the channel the
 * caller supplied holds the dry gain, and the plug-in was never asked for more
 * than it declared.
 */
void Vst3ChunkProbeTest::testChannelWithoutACallerBufferOverAFullRequest()
{
	constexpr int Prepared = TestBlockSize;
	constexpr int Frames = Prepared + 129; // 641: one full chunk and a tail
	constexpr float Stale = -12345.0f;

	m_plugin.setParamNormalized(GainParamId, 1.0f);
	ProbeState beforeState{};
	QVERIFY2(readProbeState(m_plugin, &beforeState), "the fixture saved an unexpected state layout");

	// one input channel and one output channel, for a plug-in that declares
	// two of each: channel 1 of both buses is the host's own scratch
	std::vector<float> input(Frames, 0.5f);
	std::vector<float> output(Frames, Stale);
	const float* inputPointers[1] = {input.data()};
	float* outputPointers[1] = {output.data()};
	m_plugin.process(inputPointers, outputPointers, 1, 1, Frames);

	for (int frame = 0; frame < Frames; ++frame)
	{
		QVERIFY2(output[frame] == 0.5f,
			qPrintable(QStringLiteral("frame %1 of the caller's channel holds %2, not 0.5")
						   .arg(frame)
						   .arg(output[frame])));
	}

	ProbeState state{};
	QVERIFY2(readProbeState(m_plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.framesProcessed - beforeState.framesProcessed, std::int32_t{Frames});
	QCOMPARE(state.oversizedCalls - beforeState.oversizedCalls, std::int32_t{0});
	QCOMPARE(state.blocks - beforeState.blocks, std::int32_t{2}); // 512 + 129
	qInfo("MEASURED %d frames through %d declared channels with one supplied: %d blocks, "
		  "no oversized call",
		Frames, 2, state.blocks - beforeState.blocks);
}

void Vst3ChunkProbeTest::testZeroFramesIsANoOp()
{
	ProbeState beforeState{};
	QVERIFY2(readProbeState(m_plugin, &beforeState), "the fixture saved an unexpected state layout");
	const auto before = hostChunkingStats();

	float input = 0.0f;
	float output = -12345.0f;
	const float* inputPointers[1] = {&input};
	float* outputPointers[1] = {&output};
	m_plugin.process(inputPointers, outputPointers, 1, 1, 0);

	const auto after = hostChunkingStats();
	QCOMPARE(after.requests, before.requests); // no request reached the plug-in
	ProbeState state{};
	QVERIFY2(readProbeState(m_plugin, &state), "the fixture saved an unexpected state layout");
	QCOMPARE(state.blocks, beforeState.blocks);
	QCOMPARE(output, -12345.0f); // and nothing was written
}

void Vst3ChunkProbeTest::process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
	std::vector<std::vector<float>>& outputs, int frames)
{
	std::vector<const float*> inputPointers;
	inputPointers.reserve(inputs.size());
	for (const auto& channel : inputs) { inputPointers.push_back(channel.data()); }

	std::vector<float*> outputPointers;
	outputPointers.reserve(outputs.size());
	for (auto& channel : outputs) { outputPointers.push_back(channel.data()); }

	plugin.process(inputPointers.data(), outputPointers.data(),
		static_cast<int>(inputPointers.size()), static_cast<int>(outputPointers.size()), frames);
}

} // namespace lmms::vst3

QTEST_GUILESS_MAIN(lmms::vst3::Vst3ChunkProbeTest)

#include "Vst3ChunkProbeTest.moc"
