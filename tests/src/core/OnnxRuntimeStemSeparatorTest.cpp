/*
 * OnnxRuntimeStemSeparatorTest.cpp - in-process ORT backend (only when the
 * ONNX Runtime SDK was found at configure time)
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
 */

#include <QtTest>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <QSignalSpy>
#include <QTemporaryFile>

#include <onnxruntime_cxx_api.h>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "StemSeparation/OnnxRuntimeStemSeparator.h"
#include "StemSeparation/StemJobManager.h"

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

void throwFromPostRunValidation()
{
	throw Ort::Exception("injected post-Run output validation failure", ORT_FAIL);
}

// Runs one of the broken four-output stubs (same gains and element count as the good one):
// the loader must refuse its outputs instead of reading them as planar float stereo blocks.
void expectOutputLayoutRefused(const QString& fixture)
{
	OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR) + QStringLiteral("/") + fixture);
	StemSet stems;
	QString error;
	const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384, StemQuality::Standard,
		[](float) { return true; }, stems, error);
	QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
	QVERIFY2(error.contains(QStringLiteral("float32 stereo block")), qPrintable(error));
}

} // namespace

class OnnxRuntimeStemSeparatorTest : public QObject
{
	Q_OBJECT
private slots:
	void testRuntimeReportsItself()
	{
		const QString version = OnnxRuntimeStemSeparator::runtimeVersion();
		QVERIFY(!version.isEmpty());
		qInfo("ONNX Runtime %s", qPrintable(version));
	}

	void testMissingModelFails()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral("/nonexistent/model.onnx"));
		StemSet stems;
		QString error;
		const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384, StemQuality::Standard,
			[](float) { return true; }, stems, error);
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY(!error.isEmpty());
	}

	void testMalformedModelExceptionIsReturnedBySeparator()
	{
		QTemporaryFile model;
		QVERIFY(model.open());
		QVERIFY(model.write("not an ONNX model") > 0);
		QVERIFY(model.flush());

		OnnxRuntimeStemSeparator separator(model.fileName());
		StemSet stems;
		QString error;
		StemSeparator::Status status = StemSeparator::Status::Success;
		bool exceptionEscaped = false;
		try
		{
			status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384, StemQuality::Standard,
				[](float) { return true; }, stems, error);
		}
		catch (...)
		{
			exceptionEscaped = true;
		}

		QVERIFY2(!exceptionEscaped, "a malformed model exception escaped separate()");
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY(!error.isEmpty());
	}

	void testMalformedModelFailureIsReportedByStemJob()
	{
		QTemporaryFile model;
		QVERIFY(model.open());
		QVERIFY(model.write("not an ONNX model") > 0);
		QVERIFY(model.flush());

		StemJobManager manager;
		manager.setSeparator(std::make_unique<OnnxRuntimeStemSeparator>(model.fileName()));
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);
		const int id = manager.submit(makeSineMix(1), StemModelSampleRate, 16384);
		QVERIFY(id > 0);

		QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 10000);
		QCOMPARE(static_cast<int>(manager.state(id)), static_cast<int>(StemJobManager::State::Failed));
		const QString error = manager.error(id);
		QVERIFY(!error.isEmpty());
		QCOMPARE(failedSpy.last().at(0).toInt(), id);
		QVERIFY(!failedSpy.last().at(1).toString().isEmpty());
	}

	void testPostRunOrtExceptionIsReturnedAsFailure()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		separator.m_postRunTestHook = &throwFromPostRunValidation;
		StemSet stems;
		QString error;
		StemSeparator::Status status = StemSeparator::Status::Success;
		bool exceptionEscaped = false;
		try
		{
			status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384, StemQuality::Standard,
				[](float) { return true; }, stems, error);
		}
		catch (...)
		{
			exceptionEscaped = true;
		}

		QVERIFY2(!exceptionEscaped, "a post-Run ONNX Runtime exception escaped separate()");
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY(error.contains(QStringLiteral("injected post-Run output validation failure")));
	}

	void testWrongSampleRateFails()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		StemSet stems;
		QString error;
		const auto status = separator.separate(*makeSineMix(1), 48000, 16384, StemQuality::Standard,
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
		bool progressInRange = true;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384, StemQuality::Standard,
			[&](float fraction) -> bool {
				progressInRange = progressInRange && fraction >= 0.0f && fraction <= 1.0f;
				++progressCalls;
				return true;
			},
			stems, error);

		QVERIFY(progressInRange);
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

	/*! Q050-062.1: the quality sets how many overlapping segments the model runs. A one-second mix
	 *  at a 16384-frame segment is padded to 44100 + 8192 + 16384 = 68676 frames; at a hop of
	 *  12288 / 8192 / 4096 (25% / 50% / 75% overlap) that is 6 / 9 / 17 runs. separate() reports
	 *  progress once before each run and once at the end, so the call count is runs + 1, and the
	 *  stems still add up to the mix at every setting (the stub graph's gains sum to one). */
	void testQualitySetsTheSegmentCount()
	{
		const struct { StemQuality quality; int runs; } cases[] = {
			{StemQuality::Fast, 6}, {StemQuality::Standard, 9}, {StemQuality::High, 17},
		};
		const auto mix = makeSineMix(1);
		for (const auto& c : cases)
		{
			OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
				+ QStringLiteral("/stub-4stem-linear.onnx"));
			StemSet stems;
			QString error;
			int progressCalls = 0;
			const auto status = separator.separate(*mix, StemModelSampleRate, 16384, c.quality,
				[&](float) { ++progressCalls; return true; }, stems, error);
			QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));
			QVERIFY2(progressCalls == c.runs + 1, qPrintable(QStringLiteral("%1: %2 progress calls, expected %3")
				.arg(QString::fromLatin1(stemQualityName(c.quality))).arg(progressCalls).arg(c.runs + 1)));
			float maxError = 0.0f;
			for (std::size_t i = 0; i < mix->size(); i += 97)
			{
				float sum = 0.0f;
				for (int s = 0; s < NumStems; ++s) { sum += stems[static_cast<std::size_t>(s)]->data()[i].left(); }
				maxError = std::max(maxError, std::abs(sum - mix->data()[i].left()));
			}
			QVERIFY(maxError < 1.0e-4f);
		}
	}

	void testTransposedOutputsAreRefused()
	{
		// [1, T, 2] per stem: the element count is right, the channel axis is not.
		expectOutputLayoutRefused(QStringLiteral("stub-4stem-transposed.onnx"));
	}

	void testNonFloatOutputsAreRefused()
	{
		// [1, 2, T] per stem as int64: the shape is right, the element type is not.
		expectOutputLayoutRefused(QStringLiteral("stub-4stem-int64.onnx"));
	}

	void testCancelIsObserved()
	{
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		StemSet stems;
		QString error;
		int progressCalls = 0;
		const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384, StemQuality::Standard,
			[&](float) { return ++progressCalls < 2; }, stems, error);
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Cancelled));
	}

	void testNearIntMaxFramesWithSchemaSegmentIsRefused()
	{
		// Reachable from stem.job_start: fitsAtModelRate admits frame counts up to INT_MAX, and both
		// HTDemucsSegmentFrames and the schema maximum (44100 * 600) pass the segment_frames range.
		const int intMax = std::numeric_limits<int>::max();
		QString error;
		QVERIFY(!OnnxRuntimeStemSeparator::segmentFits(intMax, HTDemucsSegmentFrames, &error));
		QVERIFY(error.contains(QStringLiteral("too long")));
		QVERIFY(!OnnxRuntimeStemSeparator::segmentFits(intMax, 44100 * 600, &error));
	}

	void testSegmentIndexArithmeticIsBounded()
	{
		// The largest index separate() forms is below frames + segment / 2 + 2 * segment; that sum
		// must not exceed INT_MAX, so the sum being exactly INT_MAX is still accepted.
		const int intMax = std::numeric_limits<int>::max();
		QString error;
		QVERIFY(OnnxRuntimeStemSeparator::segmentFits(44100, HTDemucsSegmentFrames, &error));

		const int htdemucsTail = HTDemucsSegmentFrames / 2 + 2 * HTDemucsSegmentFrames;
		QVERIFY(OnnxRuntimeStemSeparator::segmentFits(intMax - htdemucsTail, HTDemucsSegmentFrames, &error));
		QVERIFY(!OnnxRuntimeStemSeparator::segmentFits(intMax - htdemucsTail + 1, HTDemucsSegmentFrames, &error));

		const int capSegment = 44100 * 600;
		const int capTail = capSegment / 2 + 2 * capSegment;
		QVERIFY(OnnxRuntimeStemSeparator::segmentFits(intMax - capTail, capSegment, &error));
		QVERIFY(!OnnxRuntimeStemSeparator::segmentFits(intMax - capTail + 1, capSegment, &error));

		// One frame of input: the sum is INT_MAX - 1 at segment 858993458 and INT_MAX + 1 at 858993459.
		QVERIFY(OnnxRuntimeStemSeparator::segmentFits(1, 858993458, &error));
		QVERIFY(!OnnxRuntimeStemSeparator::segmentFits(1, 858993459, &error));
		QVERIFY(!error.isEmpty());
	}

	void testOverlongSegmentIsRefusedBeforeAllocation()
	{
		// Direct API only: 1.5e9 is outside the control schema range. A 1.5e9-frame window alone is 6 GB,
		// so the refusal must come before any allocation.
		OnnxRuntimeStemSeparator separator(QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/stub-4stem-linear.onnx"));
		StemSet stems;
		QString error;
		const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 1500000000, StemQuality::Standard,
			[](float) { return true; }, stems, error);
		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY(error.contains(QStringLiteral("too long")));
	}
};

QTEST_GUILESS_MAIN(OnnxRuntimeStemSeparatorTest)
#include "OnnxRuntimeStemSeparatorTest.moc"
