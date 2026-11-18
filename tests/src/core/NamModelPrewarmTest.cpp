/*
 * NamModelPrewarmTest.cpp - silence prewarming across NAM block boundaries
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
#include <vector>

#include "NamModel.h"

class NamModelPrewarmTest : public QObject
{
	Q_OBJECT

private slots:
	void multiBlockPrewarmUsesSilence();
};

void NamModelPrewarmTest::multiBlockPrewarmUsesSilence()
{
	QTemporaryDir tempDir;
	QVERIFY(tempDir.isValid());
	QFile modelFile(tempDir.filePath(QStringLiteral("prewarm.nam")));
	QVERIFY(modelFile.open(QIODevice::WriteOnly));
	const QByteArray modelJson = R"json({
		"architecture":"WaveNet",
		"version":"0.5.0",
		"config":{"layers":[{
			"input_size":1,"condition_size":1,"head_size":1,"head_bias":false,
			"channels":1,"bottleneck":1,"dilations":[8192],"kernel_size":2
		}]},
		"weights":[1.0,1.0,0.0,0.1,0.0,0.0,0.0,1.0,1.0]
	})json";
	QCOMPARE(modelFile.write(modelJson), static_cast<qint64>(modelJson.size()));
	modelFile.close();

	std::string error;
	auto prewarmed = lmms::nam::NamModel::loadFromFile(
		modelFile.fileName().toStdString(), &error);
	QVERIFY2(prewarmed != nullptr, error.c_str());
	auto reference = lmms::nam::NamModel::loadFromFile(
		modelFile.fileName().toStdString(), &error);
	QVERIFY2(reference != nullptr, error.c_str());
	QCOMPARE(prewarmed->spec().prewarmSamples, lmms::nam::NamModel::kMaxBlock + 1);

	std::vector<float> silence(static_cast<std::size_t>(prewarmed->spec().prewarmSamples), 0.0f);
	std::vector<float> discarded(silence.size());
	reference->reset();
	reference->process(silence.data(), discarded.data(), static_cast<int>(silence.size()));

	std::vector<float> tail(static_cast<std::size_t>(lmms::nam::NamModel::kMaxBlock), 0.0f);
	std::vector<float> prewarmedOutput(tail.size());
	std::vector<float> referenceOutput(tail.size());
	prewarmed->process(tail.data(), prewarmedOutput.data(), static_cast<int>(tail.size()));
	reference->process(tail.data(), referenceOutput.data(), static_cast<int>(tail.size()));
	QVERIFY(std::abs(referenceOutput.back()) > 0.05f);
	QVERIFY2(std::abs(prewarmedOutput.back() - referenceOutput.back()) < 1.0e-5f,
		qPrintable(QStringLiteral("prewarmed=%1, silence reference=%2")
			.arg(prewarmedOutput.back(), 0, 'g', 9)
			.arg(referenceOutput.back(), 0, 'g', 9)));
}

QTEST_GUILESS_MAIN(NamModelPrewarmTest)

#include "NamModelPrewarmTest.moc"
