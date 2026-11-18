/*
 * RnnoiseHarnessOutputTest.cpp - output failures reported by the RNNoise probe
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

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#ifndef RNNOISE_HARNESS_PATH
#define RNNOISE_HARNESS_PATH ""
#endif

namespace
{
bool writeInputFile(const QString& path)
{
	QFile input(path);
	if (!input.open(QIODevice::WriteOnly)) { return false; }
	const float sample = 0.25f;
	return input.write(reinterpret_cast<const char*>(&sample), sizeof(sample)) == sizeof(sample);
}
}

class RnnoiseHarnessOutputTest : public QObject
{
	Q_OBJECT

private slots:
	void reportsOutputWriteFailure();
	void writesOutputFileSuccessfully();
};

void RnnoiseHarnessOutputTest::reportsOutputWriteFailure()
{
	if (!QFileInfo::exists(QStringLiteral("/dev/full")))
	{
		QSKIP("/dev/full is unavailable on this platform");
	}

	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString inputPath = directory.filePath(QStringLiteral("input.f32"));
	QVERIFY(writeInputFile(inputPath));

	QProcess harness;
	harness.start(QString::fromLocal8Bit(RNNOISE_HARNESS_PATH),
		QStringList{inputPath, QStringLiteral("/dev/full"), QStringLiteral("1")});
	QVERIFY2(harness.waitForStarted(), qPrintable(harness.errorString()));
	QVERIFY2(harness.waitForFinished(10000), qPrintable(harness.errorString()));
	QCOMPARE(harness.exitStatus(), QProcess::NormalExit);
	QVERIFY2(harness.exitCode() != 0, qPrintable(QString::fromLocal8Bit(harness.readAllStandardError())));
}

void RnnoiseHarnessOutputTest::writesOutputFileSuccessfully()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString inputPath = directory.filePath(QStringLiteral("input.f32"));
	const QString outputPath = directory.filePath(QStringLiteral("output.f32"));
	QVERIFY(writeInputFile(inputPath));

	QProcess harness;
	harness.start(QString::fromLocal8Bit(RNNOISE_HARNESS_PATH),
		QStringList{inputPath, outputPath, QStringLiteral("1")});
	QVERIFY2(harness.waitForStarted(), qPrintable(harness.errorString()));
	QVERIFY2(harness.waitForFinished(10000), qPrintable(harness.errorString()));
	QCOMPARE(harness.exitStatus(), QProcess::NormalExit);
	QCOMPARE(harness.exitCode(), 0);
	QCOMPARE(QFileInfo(outputPath).size(), qint64(sizeof(float)));
}

QTEST_GUILESS_MAIN(RnnoiseHarnessOutputTest)

#include "RnnoiseHarnessOutputTest.moc"
