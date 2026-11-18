/*
 * NeuralAmpProfileArgumentsTest.cpp - NAM profiler CLI argument validation
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

struct ProfileResult
{
	bool started = false;
	bool finished = false;
	QProcess::ExitStatus status = QProcess::CrashExit;
	int exitCode = -1;
	QString error;
	QByteArray standardError;
};

} // namespace

class NeuralAmpProfileArgumentsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void rejectsZeroBlocks();
	void rejectsNegativeBlocks();
	void rejectsNegativeWarmup();
	void rejectsMalformedWarmup();
	void rejectsZeroSampleRate();
	void rejectsNegativeSampleRate();
	void rejectsNonFiniteSampleRate();
	void acceptsValidArguments();

private:
	ProfileResult runProfile(const QStringList& arguments) const;
	void expectRejected(const QStringList& arguments, const QByteArray& message);

	std::unique_ptr<QTemporaryDir> m_directory;
	QString m_modelPath;
};

void NeuralAmpProfileArgumentsTest::initTestCase()
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

ProfileResult NeuralAmpProfileArgumentsTest::runProfile(const QStringList& arguments) const
{
	QProcess process;
	QStringList fullArguments{m_modelPath};
	fullArguments.append(arguments);
	process.start(QStringLiteral(NAM_PROFILE_EXECUTABLE), fullArguments);
	ProfileResult result;
	result.started = process.waitForStarted();
	if (result.started)
	{
		result.finished = process.waitForFinished();
	}
	result.status = process.exitStatus();
	result.exitCode = process.exitCode();
	result.error = process.errorString();
	result.standardError = process.readAllStandardError();
	return result;
}

void NeuralAmpProfileArgumentsTest::expectRejected(const QStringList& arguments, const QByteArray& message)
{
	const ProfileResult result = runProfile(arguments);
	QVERIFY2(result.started, qPrintable(result.error));
	QVERIFY2(result.finished, qPrintable(result.error));
	QCOMPARE(result.status, QProcess::NormalExit);
	QCOMPARE(result.exitCode, 2);
	QVERIFY2(result.standardError.contains(message), result.standardError.constData());
}

void NeuralAmpProfileArgumentsTest::rejectsZeroBlocks()
{
	expectRejected({QStringLiteral("0")}, "blocks must be a positive integer");
}

void NeuralAmpProfileArgumentsTest::rejectsNegativeBlocks()
{
	expectRejected({QStringLiteral("-1")}, "blocks must be a positive integer");
}

void NeuralAmpProfileArgumentsTest::rejectsNegativeWarmup()
{
	expectRejected({QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("-1")},
		"warmup must be a non-negative integer");
}

void NeuralAmpProfileArgumentsTest::rejectsMalformedWarmup()
{
	expectRejected({QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("many")},
		"warmup must be a non-negative integer");
}

void NeuralAmpProfileArgumentsTest::rejectsZeroSampleRate()
{
	expectRejected({QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("0")},
		"sampleRate must be a positive finite number");
}

void NeuralAmpProfileArgumentsTest::rejectsNegativeSampleRate()
{
	expectRejected({QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("-48000")},
		"sampleRate must be a positive finite number");
}

void NeuralAmpProfileArgumentsTest::rejectsNonFiniteSampleRate()
{
	expectRejected({QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("nan")},
		"sampleRate must be a positive finite number");
}

void NeuralAmpProfileArgumentsTest::acceptsValidArguments()
{
	const ProfileResult result = runProfile({QStringLiteral("1"), QStringLiteral("1"),
		QStringLiteral("0"), QStringLiteral("48000")});
	QVERIFY2(result.started, qPrintable(result.error));
	QVERIFY2(result.finished, qPrintable(result.error));
	QCOMPARE(result.status, QProcess::NormalExit);
	QCOMPARE(result.exitCode, 0);
}

QTEST_GUILESS_MAIN(NeuralAmpProfileArgumentsTest)
#include "NeuralAmpProfileArgumentsTest.moc"
