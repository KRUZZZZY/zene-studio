// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p21-156: sfizz's FilterHolder and EQHolder keep a pointer to their description.
//! That pointer must be null before setup() and after reset(), so process() takes the passthrough
//! branch instead of reading an indeterminate value.

#include <QtTest>

#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#include "Config.h"
#include "EQPool.h"
#include "FilterPool.h"
#include "Region.h"
#include "Resources.h"

namespace {

//! Holds a holder object in storage pre-filled with a non-null byte pattern. That is the kind of
//! value an uninitialised pointer member can hold; a constructor that leaves the member unset keeps it.
template <class Holder>
class HolderInPattern
{
public:
	explicit HolderInPattern(sfz::Resources& resources)
	{
		std::memset(storage_, 0xAB, sizeof(storage_));
		holder_ = new (storage_) Holder(resources);
	}
	~HolderInPattern()
	{
		holder_->~Holder();
	}
	HolderInPattern(const HolderInPattern&) = delete;
	HolderInPattern& operator=(const HolderInPattern&) = delete;

	Holder& get() { return *holder_; }

private:
	alignas(Holder) unsigned char storage_[sizeof(Holder)];
	Holder* holder_ { nullptr };
};

//! Alternating +1/-1 sits at the top of the band, so a lowpass changes it far more than rounding could.
std::vector<float> nyquistBlock(size_t frames)
{
	std::vector<float> block(frames);
	for (size_t i = 0; i < frames; ++i)
		block[i] = (i % 2 == 0) ? 1.0f : -1.0f;
	return block;
}

//! A sine at the given frequency, for a peaking EQ centred on that frequency.
std::vector<float> sineBlock(size_t frames, float frequency)
{
	constexpr float twoPi = 6.28318530718f;
	const float sampleRate = static_cast<float>(sfz::config::defaultSampleRate);
	std::vector<float> block(frames);
	for (size_t i = 0; i < frames; ++i)
		block[i] = 0.5f * std::sin(twoPi * frequency * static_cast<float>(i) / sampleRate);
	return block;
}

//! Runs one mono block through process() and returns what reached the output (zero where nothing was written).
template <class Holder>
std::vector<float> runBlock(Holder& holder, const std::vector<float>& input)
{
	std::vector<float> output(input.size(), 0.0f);
	const float* inputs[1] { input.data() };
	float* outputs[1] { output.data() };
	holder.process(inputs, outputs, static_cast<unsigned>(input.size()));
	return output;
}

int countMismatches(const std::vector<float>& actual, const std::vector<float>& expected)
{
	int mismatches = 0;
	for (size_t i = 0; i < expected.size(); ++i)
		mismatches += actual[i] != expected[i];
	return mismatches;
}

} // namespace

class SfzFilterEqDescriptionTest : public QObject
{
	Q_OBJECT

private slots:
	// Before setup() the filter must copy its input. The block is larger than the buffer pool, so a
	// filter path would return before writing anything: only the passthrough branch can match the input.
	void filterBeforeSetupPassesThrough()
	{
		sfz::Resources resources;
		HolderInPattern<sfz::FilterHolder> holder { resources };
		const auto input = nyquistBlock(sfz::config::defaultSamplesPerBlock + 1);
		QCOMPARE(countMismatches(runBlock(holder.get(), input), input), 0);
	}

	// After reset() the stale description must be dropped. Control first: while set up, the lowpass
	// changes the block, so the check after reset() is not vacuous.
	void filterAfterResetPassesThrough()
	{
		sfz::Resources resources;
		sfz::Region region { 1 };
		sfz::FilterDescription lowpass;
		lowpass.cutoff = 200.0f;
		region.filters.push_back(lowpass);

		HolderInPattern<sfz::FilterHolder> holder { resources };
		holder.get().setup(region, 0);
		const auto input = nyquistBlock(256);
		QVERIFY(countMismatches(runBlock(holder.get(), input), input) > 0);

		holder.get().reset();
		QCOMPARE(countMismatches(runBlock(holder.get(), input), input), 0);
	}

	// Same contract for the equalizer, which has the same pointer member and the same process() check.
	void equalizerBeforeSetupPassesThrough()
	{
		sfz::Resources resources;
		HolderInPattern<sfz::EQHolder> holder { resources };
		const auto input = sineBlock(sfz::config::defaultSamplesPerBlock + 1, 1000.0f);
		QCOMPARE(countMismatches(runBlock(holder.get(), input), input), 0);
	}

	void equalizerAfterResetPassesThrough()
	{
		sfz::Resources resources;
		sfz::Region region { 1 };
		sfz::EQDescription peak;
		peak.frequency = 1000.0f;
		peak.gain = 12.0f;
		region.equalizers.push_back(peak);

		HolderInPattern<sfz::EQHolder> holder { resources };
		holder.get().setup(region, 0, 0.0f);
		const auto input = sineBlock(256, 1000.0f);
		QVERIFY(countMismatches(runBlock(holder.get(), input), input) > 0);

		holder.get().reset();
		QCOMPARE(countMismatches(runBlock(holder.get(), input), input), 0);
	}
};

QTEST_APPLESS_MAIN(SfzFilterEqDescriptionTest)

#include "SfzFilterEqDescriptionTest.moc"
