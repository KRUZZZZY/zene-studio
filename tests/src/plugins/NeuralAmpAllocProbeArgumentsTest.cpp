/*
 * NeuralAmpAllocProbeArgumentsTest.cpp - rt_alloc_probe CLI argument validation
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

struct ProbeResult
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

class NeuralAmpAllocProbeArgumentsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void rejectsMalformedBlockSize();
	void rejectsNegativeBlockSize();
	void rejectsZeroBlockSize();
	void rejectsTrailingGarbageBlockSize();
	void rejectsOutOfRangeBlockSize();
	void acceptsBlockSizeBounds();
	void acceptsDefaultBlockSize();

private:
	ProbeResult runProbe(const QStringList& arguments) const;
	void expectRejected(const QStringList& arguments);
	void expectAccepted(const QStringList& arguments);

	std::unique_ptr<QTemporaryDir> m_directory;
	QString m_modelPath;
};

void NeuralAmpAllocProbeArgumentsTest::initTestCase()
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
			"head_bias":false,"channels":1,"dilations":[1],
			"kernel_size":1,"layer1x1":{"active":false}
		}]},
		"weights":[0,0,0,0,0,1]
	})json";
	QCOMPARE(model.write(data), qint64(data.size()));
}

ProbeResult NeuralAmpAllocProbeArgumentsTest::runProbe(const QStringList& arguments) const
{
	QProcess process;
	QStringList fullArguments{m_modelPath};
	fullArguments.append(arguments);
	process.start(QStringLiteral(NAM_ALLOC_PROBE_EXECUTABLE), fullArguments);
	ProbeResult result;
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

void NeuralAmpAllocProbeArgumentsTest::expectRejected(const QStringList& arguments)
{
	const ProbeResult result = runProbe(arguments);
	QVERIFY2(result.started, qPrintable(result.error));
	QVERIFY2(result.finished, qPrintable(result.error));
	QCOMPARE(result.status, QProcess::NormalExit);
	QCOMPARE(result.exitCode, 2);
	QVERIFY2(result.standardError.contains("blockSize must be in [1, "), result.standardError.constData());
	// A rejected argument must never reach the measurement and report a result.
	QVERIFY2(!result.standardOutput.contains("RESULT:"), result.standardOutput.constData());
}

void NeuralAmpAllocProbeArgumentsTest::expectAccepted(const QStringList& arguments)
{
	const ProbeResult result = runProbe(arguments);
	QVERIFY2(result.started, qPrintable(result.error));
	QVERIFY2(result.finished, qPrintable(result.error));
	QCOMPARE(result.status, QProcess::NormalExit);
	QCOMPARE(result.exitCode, 0);
	QVERIFY2(result.standardOutput.contains("NO ALLOCATION IN PROCESS PATH"), result.standardOutput.constData());
}

void NeuralAmpAllocProbeArgumentsTest::rejectsMalformedBlockSize()
{
	expectRejected({QStringLiteral("abc")});
}

void NeuralAmpAllocProbeArgumentsTest::rejectsNegativeBlockSize()
{
	expectRejected({QStringLiteral("-1")});
}

void NeuralAmpAllocProbeArgumentsTest::rejectsZeroBlockSize()
{
	expectRejected({QStringLiteral("0")});
}

void NeuralAmpAllocProbeArgumentsTest::rejectsTrailingGarbageBlockSize()
{
	expectRejected({QStringLiteral("512x")});
}

void NeuralAmpAllocProbeArgumentsTest::rejectsOutOfRangeBlockSize()
{
	// 4294967297 = 2^32 + 1 would truncate to 1 in an int, so the range check must run on the wide value.
	expectRejected({QStringLiteral("8193")});
	expectRejected({QStringLiteral("4294967297")});
}

void NeuralAmpAllocProbeArgumentsTest::acceptsBlockSizeBounds()
{
	expectAccepted({QStringLiteral("1")});
	expectAccepted({QStringLiteral("8192")});
}

void NeuralAmpAllocProbeArgumentsTest::acceptsDefaultBlockSize()
{
	expectAccepted({});
}

QTEST_GUILESS_MAIN(NeuralAmpAllocProbeArgumentsTest)
#include "NeuralAmpAllocProbeArgumentsTest.moc"
