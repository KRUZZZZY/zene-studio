// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p23-90: sfizz's EQHolder must still write its outputs when the buffer pool
//! cannot supply the scratch buffers for the modulated EQ parameters.

#include <QtTest>

#include <algorithm>
#include <vector>

#include "BufferPool.h"
#include "Config.h"
#include "EQDescription.h"
#include "EQPool.h"
#include "Region.h"
#include "Resources.h"

class SfzEQPoolExhaustionTest : public QObject
{
	Q_OBJECT

private:
	static constexpr unsigned numFrames = 64;
	static constexpr float sentinel = -1234.0f;

	// Runs one block through an EQ. When exhaustPool is set, every mono scratch buffer is held
	// first, so the EQ's getBuffer() calls fail. The outputs start at the sentinel, and the
	// inputs are returned for comparison.
	void runBlock(bool stereo, bool exhaustPool,
		std::vector<std::vector<float>>& inputStorage,
		std::vector<std::vector<float>>& outputStorage)
	{
		sfz::Resources resources;
		sfz::Region region { 0 };
		region.hasStereoSample = stereo;
		region.equalizers.emplace_back();

		sfz::EQHolder eq { resources };
		eq.setup(region, 0, 0.5f);

		sfz::BufferPool& bufferPool = resources.getBufferPool();
		using ScratchBuffer = decltype(bufferPool.getBuffer(numFrames));
		std::vector<ScratchBuffer> held;
		if (exhaustPool) {
			while (auto buffer = bufferPool.getBuffer(numFrames))
				held.push_back(std::move(buffer));
			QCOMPARE(held.size(), static_cast<size_t>(sfz::config::bufferPoolSize));
			QVERIFY(!bufferPool.getBuffer(numFrames));
		}

		const unsigned channels = stereo ? 2 : 1;
		inputStorage.assign(channels, std::vector<float>(numFrames));
		outputStorage.assign(channels, std::vector<float>(numFrames, sentinel));
		const float* inputs[2] {};
		float* outputs[2] {};
		for (unsigned ch = 0; ch < channels; ++ch) {
			for (unsigned i = 0; i < numFrames; ++i)
				inputStorage[ch][i] = 0.25f * static_cast<float>(ch + 1) + 0.001f * static_cast<float>(i);
			inputs[ch] = inputStorage[ch].data();
			outputs[ch] = outputStorage[ch].data();
		}

		eq.process(inputs, outputs, numFrames);
	}

private slots:
	void exhaustedMonoPoolPassesInputThroughToOutput()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(false, true, inputs, outputs);
		QVERIFY(std::equal(outputs[0].begin(), outputs[0].end(), inputs[0].begin()));
	}

	void exhaustedStereoPoolPassesInputThroughToOutput()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(true, true, inputs, outputs);
		for (unsigned ch = 0; ch < 2; ++ch)
			QVERIFY(std::equal(outputs[ch].begin(), outputs[ch].end(), inputs[ch].begin()));
	}

	void availablePoolWritesEveryOutputSample()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(true, false, inputs, outputs);
		for (unsigned ch = 0; ch < 2; ++ch)
			QVERIFY(std::none_of(outputs[ch].begin(), outputs[ch].end(),
				[](float value) { return value == sentinel; }));
	}
};

QTEST_APPLESS_MAIN(SfzEQPoolExhaustionTest)

#include "SfzEQPoolExhaustionTest.moc"
