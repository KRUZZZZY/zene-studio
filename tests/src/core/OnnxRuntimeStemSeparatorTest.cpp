/*
 * OnnxRuntimeStemSeparatorTest.cpp - in-process ORT backend (only when the
 * ONNX Runtime SDK was found at configure time)
 *
 * Copyright (c) 2026 LMMS Developers
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
 */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "StemSeparation/OnnxRuntimeStemSeparator.h"

using namespace lmms;

namespace
{

std::shared_ptr<const SampleBuffer> makeSineMix(int seconds)
{
	const int sampleRate = StemModelSampleRate;
	const int frames = sampleRate * seconds;
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	for (int i = 0; i < frames; ++i)
	{
		const auto t = static_cast<float>(i) / static_cast<float>(sampleRate);
		data[static_cast<std::size_t>(i)] =
			SampleFrame(0.4f * std::sin(2.0f * float(M_PI) * 220.0f * t),
				0.4f * std::sin(2.0f * float(M_PI) * 330.0f * t));
	}
	return std::make_shared<const SampleBuffer>(std::move(data), sampleRate);
}

} // namespace

class OnnxRuntimeStemSeparatorTest : public QObject
{
	Q_OBJECT
private slots:
	void testRuntimeReportsItself()
	{
		QVERIFY(OnnxRuntimeStemSeparator::isRuntimeAvailable());
		QVERIFY(!OnnxRuntimeStemSeparator::runtimeVersion().isEmpty());
		qInfo("ONNX Runtime %s", qPrintable(OnnxRuntimeStemSeparator::runtimeVersion()));
	}

	void testMissingModelFails()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral("/nonexistent/model.onnx"));
		StemSet stems;
		QString error;
		const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384,
			[](float) { return true; }, stems, error);
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY(!error.isEmpty());
	}

	void testWrongSampleRateFails()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		StemSet stems;
		QString error;
		const auto status = separator.separate(*makeSineMix(1), 48000, 16384,
			[](float) { return true; }, stems, error);
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY(error.contains(QStringLiteral("44100")));
	}

	void testSeparatesWithTheStubModel()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		const auto mix = makeSineMix(1);
		StemSet stems;
		QString error;
		int progressCalls = 0;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384,
			[&](float fraction) {
				QVERIFY(fraction >= 0.0f && fraction <= 1.0f);
				++progressCalls;
				return true;
			},
			stems, error);

		QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));
		QVERIFY(progressCalls >= 2);
		for (int s = 0; s < NumStems; ++s)
		{
			QVERIFY(stems[static_cast<std::size_t>(s)] != nullptr);
			QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
		}

		float maxError = 0.0f;
		for (std::size_t i = 0; i < mix->size(); i += 97)
		{
			float sum = 0.0f;
			for (int s = 0; s < NumStems; ++s)
			{
				sum += stems[static_cast<std::size_t>(s)]->data()[i].left();
			}
			maxError = std::max(maxError, std::abs(sum - mix->data()[i].left()));
		}
		QVERIFY(maxError < 1.0e-4f);
	}

	void testCancelIsObserved()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		StemSet stems;
		QString error;
		int progressCalls = 0;
		const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384,
			[&](float) { return ++progressCalls < 2; }, stems, error);
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Cancelled));
	}
};

QTEST_GUILESS_MAIN(OnnxRuntimeStemSeparatorTest)
#include "OnnxRuntimeStemSeparatorTest.moc"
