/*
 * NeuralAmpHarnessBlockCount.cpp - the NAM harness's generated-run block counts
 *
 * The headless harness (plugins/NeuralAmp/tests/nam_harness.cpp) adds the
 * generated block count before it widens it. An oversized count must be
 * refused with a usage error (exit code 2) BEFORE a single buffer is
 * allocated: it must not overflow into a huge allocation request, and it must
 * not abort the process either.
 *
 * The test drives the real harness binary, so the test executable depends on
 * the nam_harness target: building this test always rebuilds the harness it
 * exercises, and the harness under test is never a stale binary.
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
#include <QProcess>
#include <QTemporaryDir>

#include <memory>

namespace
{

struct HarnessResult
{
	bool started = false;
	bool finished = false;
	QProcess::ExitStatus status = QProcess::CrashExit;
	int exitCode = -1;
	QString error;
	QByteArray standardOutput;
	QByteArray standardError;
};

} // namespace

class NeuralAmpHarnessBlockCountTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void rejectsTheMaximumBlockCount();
	void rejectsBothCountsAtTheMaximum();
	void rejectsABlockCountBeyondTheNarrowType();
	void rejectsMalformedCounts();
	void rejectsACountBeyondInt64();
	void rejectsAnOutOfRangeFrameCount();
	void keepsTheRangeMessages();
	void runsASmallGeneratedRun();

private:
	HarnessResult runHarness(const QStringList& arguments) const;
	void expectRejected(const QStringList& arguments, const QByteArray& message);

	std::unique_ptr<QTemporaryDir> m_directory;
	QString m_modelPath;
};

void NeuralAmpHarnessBlockCountTest::initTestCase()
{
	m_directory = std::make_unique<QTemporaryDir>();
	QVERIFY(m_directory->isValid());
	m_modelPath = m_directory->filePath(QStringLiteral("minimal.nam"));
	QFile model(m_modelPath);
	QVERIFY(model.open(QIODevice::WriteOnly));
	const QByteArray data = R"json({
		"architecture":"WaveNet",
		"version":"0.5.0",
		"config":{"layers":[{
			"input_size":1,"condition_size":1,"head_size":1,
			"head_bias":true,"channels":1,"bottleneck":1,
			"dilations":[1],"kernel_size":1
		}]},
		"weights":[1.0,1.0,0.0,0.0,1.0,0.0,1.0,0.0,1.0]
	})json";
	QCOMPARE(model.write(data), qint64(data.size()));
}

HarnessResult NeuralAmpHarnessBlockCountTest::runHarness(const QStringList& arguments) const
{
	QProcess process;
	QStringList fullArguments{m_modelPath};
	fullArguments.append(arguments);
	process.start(QStringLiteral(NAM_HARNESS_EXECUTABLE), fullArguments);
	HarnessResult result;
	result.started = process.waitForStarted();
	if (result.started)
	{
		result.finished = process.waitForFinished();
	}
	result.status = process.exitStatus();
	result.exitCode = process.exitCode();
	result.error = process.errorString();
	result.standardOutput = process.readAllStandardOutput();
	result.standardError = process.readAllStandardError();
	return result;
}

void NeuralAmpHarnessBlockCountTest::expectRejected(const QStringList& arguments, const QByteArray& message)
{
	const HarnessResult result = runHarness(arguments);
	QVERIFY2(result.started, qPrintable(result.error));
	QVERIFY2(result.finished, qPrintable(result.error));
	const QString details = QStringLiteral("args=%1 exit=%2 status=%3 stderr=%4")
		.arg(arguments.join(' '))
		.arg(result.exitCode)
		.arg(result.status == QProcess::NormalExit ? QStringLiteral("normal")
						     : QStringLiteral("crashed"))
		.arg(QString::fromLatin1(result.standardError));
	// A usage error, not a crash: the harness must exit normally with code 2.
	QVERIFY2(result.status == QProcess::NormalExit, qPrintable(details));
	QVERIFY2(result.exitCode == 2, qPrintable(details));
	QVERIFY2(result.standardError.contains(message), qPrintable(details));
}

void NeuralAmpHarnessBlockCountTest::rejectsTheMaximumBlockCount()
{
	// The reported case: the block count plus the warmup overflowed a 32-bit sum.
	expectRejected({QStringLiteral("--blocks"), QStringLiteral("2147483647")},
		"generated run exceeds 4194304 samples");
}

void NeuralAmpHarnessBlockCountTest::rejectsBothCountsAtTheMaximum()
{
	// Both counts at INT_MAX: the sum alone overflows the narrow type.
	expectRejected({QStringLiteral("--blocks"), QStringLiteral("2147483647"),
			QStringLiteral("--warmup"), QStringLiteral("2147483647")},
		"generated run exceeds 4194304 samples");
}

void NeuralAmpHarnessBlockCountTest::rejectsABlockCountBeyondTheNarrowType()
{
	// A count that no longer fits the narrow type must be refused, never wrapped.
	expectRejected({QStringLiteral("--blocks"), QStringLiteral("2147483648")},
		"generated run exceeds 4194304 samples");
}

void NeuralAmpHarnessBlockCountTest::rejectsMalformedCounts()
{
	expectRejected({QStringLiteral("--blocks"), QStringLiteral("not-a-number")},
		"--blocks must be an integer");
	expectRejected({QStringLiteral("--warmup"), QStringLiteral("many")},
		"--warmup must be an integer");
}

void NeuralAmpHarnessBlockCountTest::rejectsACountBeyondInt64()
{
	expectRejected({QStringLiteral("--blocks"), QStringLiteral("99999999999999999999")},
		"--blocks must be an integer");
}

void NeuralAmpHarnessBlockCountTest::rejectsAnOutOfRangeFrameCount()
{
	expectRejected({QStringLiteral("--block"), QStringLiteral("2147483648")},
		"--block must be an integer");
}

void NeuralAmpHarnessBlockCountTest::keepsTheRangeMessages()
{
	// Counts that are numbers but out of range keep their own message.
	expectRejected({QStringLiteral("--blocks"), QStringLiteral("0")}, "--blocks must be >= 1");
	expectRejected({QStringLiteral("--warmup"), QStringLiteral("-1")}, "--warmup must be >= 0");
	expectRejected({QStringLiteral("--block"), QStringLiteral("99999")}, "--block must be in [1, 8192]");
}

void NeuralAmpHarnessBlockCountTest::runsASmallGeneratedRun()
{
	// The legitimate path still works: a small generated run loads the model and
	// reports the signal the harness measures.
	const HarnessResult result = runHarness({QStringLiteral("--block"), QStringLiteral("64"),
		QStringLiteral("--blocks"), QStringLiteral("1"), QStringLiteral("--warmup"), QStringLiteral("0")});
	QVERIFY2(result.started, qPrintable(result.error));
	QVERIFY2(result.finished, qPrintable(result.error));
	QCOMPARE(result.status, QProcess::NormalExit);
	// The tiny fixture model can trip the quality thresholds, so both a clean run
	// (0) and a reported quality failure (1) are acceptable here.
	QVERIFY(result.exitCode == 0 || result.exitCode == 1);
	QVERIFY2(result.standardOutput.contains("architecture: WaveNet"), result.standardOutput.constData());
	QVERIFY2(result.standardOutput.contains("block size: 64  blocks: 1"), result.standardOutput.constData());
	QVERIFY2(result.standardOutput.contains("finite=1 nonzero=1"), result.standardOutput.constData());
}

QTEST_GUILESS_MAIN(NeuralAmpHarnessBlockCountTest)
#include "NeuralAmpHarnessBlockCount.moc"
