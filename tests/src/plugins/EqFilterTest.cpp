/*
 * EqFilterTest.cpp - EQ filter frequencies stay at or below Nyquist
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <QtTest>

#include <cmath>
#include <limits>

#include "SampleFrame.h"
#include "../../../plugins/Eq/EqFilter.h"

using namespace lmms;

namespace
{

class ProbePeakFilter : public EqPeakFilter
{
public:
	float frequency() const { return m_freq; }
};

class ProbeLowPassFilter : public EqLp12Filter
{
public:
	float frequency() const { return m_freq; }
};

void compareImpulseResponses(EqLp12Filter& actual, EqLp12Filter& expected)
{
	for (int i = 0; i < 32; ++i)
	{
		const float input = i == 0 ? 1.0f : 0.0f;
		QCOMPARE(actual.update(input, 0, 1.0f), expected.update(input, 0, 1.0f));
	}
}

template <typename Filter>
void verifyImpulseResponseIsFinite(Filter& filter)
{
	for (int i = 0; i < 32; ++i)
	{
		const float input = i == 0 ? 1.0f : 0.0f;
		QVERIFY(std::isfinite(filter.update(input, 0, 1.0f)));
	}
}

} // namespace

class EqFilterTest : public QObject
{
	Q_OBJECT

private slots:
	void peakFilterAtNyquistProducesFiniteAudio();
	void bulkFrequencyAboveNyquistUsesNyquist();
	void liveFrequencyAboveNyquistUsesNyquist();
	void sampleRateChangeReclampsRequestedFrequency();
	void peakFilterBulkFrequencyIsClamped();
	void peakFilterSampleRateChangeReclampsFrequency();
	void nonFiniteFrequencyFallsBackToZeroHertz();
	void nonFiniteFrequencyKeepsEveryFilterOutputFinite();
	void liveNonFiniteFrequencyKeepsOutputFinite();
};

void EqFilterTest::peakFilterAtNyquistProducesFiniteAudio()
{
	lmms::EqPeakFilter filter;
	filter.setParameters(32000.0f, 16000.0f, 0.3f, 12.0f);

	QVERIFY(std::isfinite(filter.update(1.0f, 0, 1.0f)));
}

void EqFilterTest::bulkFrequencyAboveNyquistUsesNyquist()
{
	EqLp12Filter aboveNyquist;
	EqLp12Filter atNyquist;
	aboveNyquist.setParameters(22050.0f, 20000.0f, 0.707f, 1.0f);
	atNyquist.setParameters(22050.0f, 11025.0f, 0.707f, 1.0f);
	compareImpulseResponses(aboveNyquist, atNyquist);
}

void EqFilterTest::liveFrequencyAboveNyquistUsesNyquist()
{
	EqLp12Filter liveFrequency;
	EqLp12Filter atNyquist;
	liveFrequency.setParameters(22050.0f, 500.0f, 0.707f, 1.0f);
	liveFrequency.setFrequency(20000.0f);
	atNyquist.setParameters(22050.0f, 11025.0f, 0.707f, 1.0f);
	compareImpulseResponses(liveFrequency, atNyquist);
}

void EqFilterTest::sampleRateChangeReclampsRequestedFrequency()
{
	EqLp12Filter loweredRate;
	EqLp12Filter atNyquist;
	loweredRate.setParameters(44100.0f, 20000.0f, 0.707f, 1.0f);
	loweredRate.setSampleRate(22050);
	atNyquist.setParameters(22050.0f, 11025.0f, 0.707f, 1.0f);
	compareImpulseResponses(loweredRate, atNyquist);
}

void EqFilterTest::peakFilterBulkFrequencyIsClamped()
{
	ProbePeakFilter filter;
	filter.setParameters(22050.0f, 20000.0f, 1.0f, 6.0f);
	QCOMPARE(filter.frequency(), 11025.0f);
	QVERIFY(std::isfinite(filter.update(1.0f, 0, 1.0f)));
}

void EqFilterTest::peakFilterSampleRateChangeReclampsFrequency()
{
	ProbePeakFilter filter;
	filter.setParameters(44100.0f, 20000.0f, 1.0f, 6.0f);
	QCOMPARE(filter.frequency(), 20000.0f);
	filter.setParameters(22050.0f, 20000.0f, 1.0f, 6.0f);
	QCOMPARE(filter.frequency(), 11025.0f);
	QVERIFY(std::isfinite(filter.update(1.0f, 0, 1.0f)));
}

void EqFilterTest::nonFiniteFrequencyFallsBackToZeroHertz()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();

	ProbeLowPassFilter lowPass;
	lowPass.setParameters(44100.0f, nan, 0.707f, 1.0f);
	QCOMPARE(lowPass.frequency(), 0.0f);

	ProbePeakFilter peak;
	peak.setParameters(44100.0f, nan, 1.0f, 6.0f);
	QCOMPARE(peak.frequency(), 0.0f);
}

void EqFilterTest::nonFiniteFrequencyKeepsEveryFilterOutputFinite()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();

	EqHp12Filter highPass;
	highPass.setParameters(44100.0f, nan, 0.707f, 1.0f);
	verifyImpulseResponseIsFinite(highPass);

	EqLp12Filter lowPass;
	lowPass.setParameters(44100.0f, nan, 0.707f, 1.0f);
	verifyImpulseResponseIsFinite(lowPass);

	EqLowShelfFilter lowShelf;
	lowShelf.setParameters(44100.0f, nan, 0.707f, 6.0f);
	verifyImpulseResponseIsFinite(lowShelf);

	EqHighShelfFilter highShelf;
	highShelf.setParameters(44100.0f, nan, 0.707f, 6.0f);
	verifyImpulseResponseIsFinite(highShelf);

	EqPeakFilter peak;
	peak.setParameters(44100.0f, nan, 1.0f, 6.0f);
	verifyImpulseResponseIsFinite(peak);
}

void EqFilterTest::liveNonFiniteFrequencyKeepsOutputFinite()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();

	EqHp12Filter highPass;
	highPass.setParameters(44100.0f, 1000.0f, 0.707f, 1.0f);
	highPass.setFrequency(nan);
	verifyImpulseResponseIsFinite(highPass);

	EqLp12Filter lowPass;
	lowPass.setParameters(44100.0f, 1000.0f, 0.707f, 1.0f);
	lowPass.setFrequency(nan);
	verifyImpulseResponseIsFinite(lowPass);
}

QTEST_GUILESS_MAIN(EqFilterTest)
#include "EqFilterTest.moc"
