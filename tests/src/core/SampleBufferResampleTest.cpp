/*
 * SampleBufferResampleTest.cpp - a whole buffer at another rate keeps its length and pitch
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

#include <cmath>
#include <numbers>
#include <vector>

#include "SampleBufferResample.h"

using namespace lmms;

//! Engine audit E27: stem separation resamples a source to 44.1 kHz instead of refusing it, so
//! the resample must keep the audio's length in time and its pitch.
class SampleBufferResampleTest : public QObject
{
	Q_OBJECT

private slots:
	void aToneKeepsItsLengthAndPitch()
	{
		constexpr int InRate = 48000;
		constexpr int OutRate = 44100;
		constexpr double Hz = 1000.0;
		std::vector<SampleFrame> data(InRate / 2);  // half a second
		for (std::size_t i = 0; i < data.size(); ++i)
		{
			const auto v = static_cast<float>(std::sin(2.0 * std::numbers::pi * Hz * static_cast<double>(i) / InRate));
			data[i] = SampleFrame(v, v);
		}
		const auto input = std::make_shared<const SampleBuffer>(std::move(data), InRate);
		const auto output = resampleBuffer(input, OutRate);
		QCOMPARE(static_cast<int>(output->sampleRate()), OutRate);
		QVERIFY(std::abs(static_cast<int>(output->size()) - OutRate / 2) <= 1);
		// Pitch: count rising zero crossings over the settled middle - 1000 Hz for 0.25 s is 250.
		int crossings = 0;
		const std::size_t from = output->size() / 4;
		const std::size_t to = from + OutRate / 4;
		for (std::size_t i = from + 1; i < to; ++i)
		{
			if (output->data()[i - 1][0] < 0.0f && output->data()[i][0] >= 0.0f) { ++crossings; }
		}
		QVERIFY2(std::abs(crossings - 250) <= 1, qPrintable(QString::number(crossings)));
	}

	void aBufferAlreadyAtTheRateIsReturnedAsIs()
	{
		const auto input = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(100), 44100);
		QCOMPARE(resampleBuffer(input, 44100).get(), input.get());
	}

	//! Luna cluster 5285: the zero TARGET rate was rejected but a zero SOURCE rate was not,
	//! so the output/input ratio was an infinity and the output length an enormous (and
	//! meaningless) allocation request. A buffer that cannot state its rate is passed through
	//! untouched, exactly as the target-rate guard already does.
	void aBufferWithNoSourceRateIsPassedThroughUnchanged()
	{
		const auto input = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(4096), 0);
		QCOMPARE(input->sampleRate(), 0U);
		// Without the guard this divides by zero below and asks for a huge allocation.
		QCOMPARE(resampleBuffer(input, 44100).get(), input.get());
	}

	void aResampledBufferDoesNotPointBackToTheUntransformedSourceFile()
	{
		const auto input = std::make_shared<const SampleBuffer>(
			std::vector<SampleFrame>(4096), 48000, QStringLiteral("source.wav"));
		const auto output = resampleBuffer(input, 44100);
		QVERIFY(output.get() != input.get());
		QVERIFY(output->audioFile().isEmpty());
	}
};

QTEST_GUILESS_MAIN(SampleBufferResampleTest)
#include "SampleBufferResampleTest.moc"
