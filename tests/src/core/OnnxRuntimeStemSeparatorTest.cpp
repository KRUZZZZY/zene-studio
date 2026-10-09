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
		const auto status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384,
			[](float) { return true; }, stems, error);
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
			status = separator.separate(*makeSineMix(1), StemModelSampleRate, 16384,
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
		bool progressInRange = true;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384,
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
