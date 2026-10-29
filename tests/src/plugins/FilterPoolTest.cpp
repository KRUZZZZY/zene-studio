/*
 * FilterPoolTest.cpp - a reset filter holder bypasses until its next setup.
 */

#include <QtTest>

#include <array>
#include <algorithm>
#include <cmath>

#include "sfizz/FilterDescription.h"
#include "sfizz/FilterPool.h"
#include "sfizz/Region.h"
#include "sfizz/Resources.h"

class FilterPoolTest : public QObject
{
	Q_OBJECT

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
};

QTEST_APPLESS_MAIN(FilterPoolTest)
#include "FilterPoolTest.moc"
