/*
 * FilterPoolTest.cpp - a reset filter holder bypasses until its next setup, and a holder whose
 * scratch buffers the pool cannot supply passes its inputs through instead of dropping the block.
 */

#include <QtTest>

#include <array>
#include <algorithm>
#include <cmath>
#include <vector>

#include "sfizz/BufferPool.h"
#include "sfizz/Config.h"
#include "sfizz/FilterDescription.h"
#include "sfizz/FilterPool.h"
#include "sfizz/Region.h"
#include "sfizz/Resources.h"

class FilterPoolTest : public QObject
{
	Q_OBJECT

private:
	static constexpr unsigned numFrames = 64;
	static constexpr float sentinel = -1234.0f;

	//! Runs one block through a filter. With exhaustPool set, every mono scratch buffer is held
	//! first, so the filter's getBuffer() calls fail. Outputs start at the sentinel, and the
	//! inputs are handed back for comparison. With inPlace set, inputs and outputs are the same
	//! buffer, which is how Voice::Impl::filterStage* calls the holder.
	void runBlock(bool stereo, bool exhaustPool, bool inPlace,
		std::vector<std::vector<float>>& inputStorage,
		std::vector<std::vector<float>>& outputStorage)
	{
		sfz::Resources resources;
		resources.setSampleRate(48000.f);
		resources.setSamplesPerBlock(numFrames);

		sfz::Region region { 0 };
		region.hasStereoSample = stereo;
		region.filters.emplace_back();
		region.filters.back().type = sfz::kFilterLpf2p;
		region.filters.back().cutoff = 800.f;

		sfz::FilterHolder holder { resources };
		holder.setup(region, 0, 60, 1.f);

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
			// Nyquist-rate square wave: a lowpass at 800 Hz changes it far more than rounding could.
			for (unsigned i = 0; i < numFrames; ++i)
				inputStorage[ch][i] = (i % 2 == 0 ? 1.0f : -1.0f) * static_cast<float>(ch + 1);
			inputs[ch] = inputStorage[ch].data();
			outputs[ch] = inPlace ? inputStorage[ch].data() : outputStorage[ch].data();
		}

		holder.process(inputs, outputs, numFrames);
	}

private slots:
	void resetBypassesThePreviousFilter()
	{
		sfz::Resources resources;
		resources.setSampleRate(48000.f);
		resources.setSamplesPerBlock(64);

		sfz::Region region{0};
		region.filters.emplace_back();
		auto& description = region.filters.back();
		description.type = sfz::kFilterLpf2p;
		description.cutoff = 800.f;

		sfz::FilterHolder holder{resources};
		holder.setup(region, 0, 60, 1.f);
		holder.reset();

		std::array<float, 64> input{};
		std::array<float, 64> output{};
		for (std::size_t i = 0; i < input.size(); ++i)
			input[i] = i % 2 == 0 ? 1.f : -1.f;
		const float* inputs[] { input.data() };
		float* outputs[] { output.data() };

		holder.process(inputs, outputs, static_cast<unsigned>(input.size()));

		QVERIFY2(std::equal(input.begin(), input.end(), output.begin()),
			"a reset, not-yet-set-up holder must pass audio through");
	}

	void exhaustedMonoPoolPassesInputThroughToOutput()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(false, true, false, inputs, outputs);
		QVERIFY2(std::equal(outputs[0].begin(), outputs[0].end(), inputs[0].begin()),
			"an exhausted buffer pool must pass the mono input through to the output");
	}

	void exhaustedStereoPoolPassesInputThroughToOutput()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(true, true, false, inputs, outputs);
		for (unsigned ch = 0; ch < 2; ++ch)
			QVERIFY2(std::equal(outputs[ch].begin(), outputs[ch].end(), inputs[ch].begin()),
				"an exhausted buffer pool must pass every stereo channel through to its output");
	}

	void exhaustedPoolInPlaceIsHarmless()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(false, true, true, inputs, outputs);
		QVERIFY2(std::none_of(inputs[0].begin(), inputs[0].end(),
			[](float value) { return std::isnan(value) || value == sentinel; }),
			"in-place processing with an exhausted pool must leave the voice buffer untouched");
	}

	void availablePoolWritesEveryOutputSample()
	{
		std::vector<std::vector<float>> inputs;
		std::vector<std::vector<float>> outputs;
		runBlock(true, false, false, inputs, outputs);
		for (unsigned ch = 0; ch < 2; ++ch)
			QVERIFY(std::none_of(outputs[ch].begin(), outputs[ch].end(),
				[](float value) { return value == sentinel; }));
		// A lowpass at 800 Hz cannot leave a Nyquist-rate square wave untouched.
		QVERIFY(!std::equal(outputs[0].begin(), outputs[0].end(), inputs[0].begin()));
	}
};

QTEST_APPLESS_MAIN(FilterPoolTest)
#include "FilterPoolTest.moc"
