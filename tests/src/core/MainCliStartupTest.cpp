/*
 * MainCliStartupTest.cpp - startup CLI failures and session-marker cleanup
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
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdlib>

namespace
{

struct CliRun
{
	QProcess::ExitStatus status = QProcess::CrashExit;
	int exitCode = -1;
	QByteArray standardOutput;
	QByteArray standardError;
};

QString zeneBinary()
{
#ifdef ZENE_APP_BINARY
	return QStringLiteral(ZENE_APP_BINARY);
#else
	QString path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
		QStringLiteral("../zene"));
#ifdef Q_OS_WIN
	path += QStringLiteral(".exe");
#endif
	return path;
#endif
}

QProcessEnvironment cliEnvironment(const QString& home)
{
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	environment.insert(QStringLiteral("HOME"), home);
	environment.insert(QStringLiteral("XDG_CONFIG_HOME"), QDir(home).filePath(QStringLiteral("config")));
	environment.insert(QStringLiteral("XDG_DATA_HOME"), QDir(home).filePath(QStringLiteral("data")));
	environment.remove(QStringLiteral("QT_SCALE_FACTOR"));
	return environment;
}

CliRun runCli(const QString& home, const QStringList& arguments)
{
	QProcess process;
	process.setProcessEnvironment(cliEnvironment(home));
	process.start(zeneBinary(), arguments);
	if (!process.waitForStarted(30000))
	{
		CliRun failed;
		failed.standardError = process.errorString().toUtf8();
		return failed;
	}
	if (!process.waitForFinished(120000))
	{
		process.kill();
		process.waitForFinished();
	}
	return {process.exitStatus(), process.exitCode(), process.readAllStandardOutput(),
		process.readAllStandardError()};
}

bool writeFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		&& file.write(contents) == contents.size();
}

QString configForWorkingDirectory(const QString& root)
{
	const QString workingDirectory = QDir(root).filePath(QStringLiteral("working"));
	QDir().mkpath(workingDirectory);
	const QString path = QDir(root).filePath(QStringLiteral("alternate.xml"));
	return writeFile(path, QStringLiteral("<lmms><paths workingdir=\"%1/\"/></lmms>")
		.arg(workingDirectory.toHtmlEscaped()).toUtf8()) ? path : QString();
}

} // namespace

class MainCliStartupTest : public QObject
{
	Q_OBJECT

private slots:
	void dumpRejectsMissingAndInvalidInputs()
	{
		QTemporaryDir home;
		QVERIFY(home.isValid());
		const CliRun missing = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("dump"),
			QDir(home.path()).filePath(QStringLiteral("missing.mmpz"))});
		QCOMPARE(missing.status, QProcess::NormalExit);
		QCOMPARE(missing.exitCode, EXIT_FAILURE);
		QVERIFY(missing.standardError.contains("dump:"));

		const QString invalidPath = QDir(home.path()).filePath(QStringLiteral("invalid.mmpz"));
		QVERIFY(writeFile(invalidPath, QByteArrayLiteral("not compressed")));
		const CliRun invalid = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("dump"), invalidPath});
		QCOMPARE(invalid.status, QProcess::NormalExit);
		QCOMPARE(invalid.exitCode, EXIT_FAILURE);
		QVERIFY(invalid.standardError.contains("not a compressed project document"));
	}

	void compressRejectsMissingInputAndRoundTripsValidBytes()
	{
		QTemporaryDir home;
		QVERIFY(home.isValid());
		const CliRun missing = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("compress"),
			QDir(home.path()).filePath(QStringLiteral("missing.mmp"))});
		QCOMPARE(missing.status, QProcess::NormalExit);
		QCOMPARE(missing.exitCode, EXIT_FAILURE);
		QVERIFY(missing.standardError.contains("compress:"));

		const QByteArray source = QByteArrayLiteral("<multimedia-project version=\"1.0\"/>\n");
		const QString sourcePath = QDir(home.path()).filePath(QStringLiteral("valid.mmp"));
		QVERIFY(writeFile(sourcePath, source));
		const CliRun compressed = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("compress"), sourcePath});
		QCOMPARE(compressed.status, QProcess::NormalExit);
		QCOMPARE(compressed.exitCode, EXIT_SUCCESS);
		QVERIFY(!compressed.standardOutput.isEmpty());
		const QString compressedPath = QDir(home.path()).filePath(QStringLiteral("valid.mmpz"));
		QVERIFY(writeFile(compressedPath, compressed.standardOutput));
		const CliRun dumped = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("dump"), compressedPath});
		QCOMPARE(dumped.status, QProcess::NormalExit);
		QCOMPARE(dumped.exitCode, EXIT_SUCCESS);
		QCOMPARE(dumped.standardOutput, source + '\n');
	}

	void failedControlSocketStartupClearsBothSessionMarkers()
	{
		QTemporaryDir home;
		QVERIFY(home.isValid());
		const QString config = configForWorkingDirectory(home.path());
		QVERIFY(!config.isEmpty());
		const QString workingDirectory = QDir(home.path()).filePath(QStringLiteral("working"));
		const QString socketName = QStringLiteral("zene-qwen-5110-")
			+ QUuid::createUuid().toString(QUuid::WithoutBraces);
		QLocalServer occupyingServer;
		QVERIFY2(occupyingServer.listen(socketName), qPrintable(occupyingServer.errorString()));

		const CliRun failed = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("--config"), config,
			QStringLiteral("--run-script"), QStringLiteral("never-run.lua"), QStringLiteral("--control-socket"), socketName});
		QCOMPARE(failed.status, QProcess::NormalExit);
		QCOMPARE(failed.exitCode, EXIT_FAILURE);
		QVERIFY(failed.standardError.contains("control socket:"));
		QVERIFY2(!QFile::exists(QDir(workingDirectory).filePath(QStringLiteral("zene-session-open.marker"))),
			"a failed socket listen left the crash session marker armed");
		QVERIFY2(!QFile::exists(QDir(workingDirectory).filePath(QStringLiteral("zene-safe-start.marker"))),
			"a failed socket listen left the safe-start marker armed");
	}

	void missingStemOutputIsRejectedBeforeSessionStartup()
	{
		QTemporaryDir home;
		QVERIFY(home.isValid());
		const QString config = configForWorkingDirectory(home.path());
		QVERIFY(!config.isEmpty());
		const QString workingDirectory = QDir(home.path()).filePath(QStringLiteral("working"));
#ifdef ZENE_SOURCE_DIR
		const QString project = QDir(QStringLiteral(ZENE_SOURCE_DIR)).filePath(QStringLiteral("tests/emptyproject.mmp"));
#else
		const QString project = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../tests/emptyproject.mmp"));
#endif

		const CliRun missingOutput = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("--config"), config,
			QStringLiteral("exportstems"), project});
		QVERIFY2(!missingOutput.standardOutput.contains("Loading project..."),
			"the missing-output refusal ran after project loading");
		QCOMPARE(missingOutput.status, QProcess::NormalExit);
		QCOMPARE(missingOutput.exitCode, EXIT_FAILURE);
		QVERIFY(!QFile::exists(QDir(workingDirectory).filePath(QStringLiteral("zene-session-open.marker"))));
		QVERIFY(!QFile::exists(QDir(workingDirectory).filePath(QStringLiteral("zene-safe-start.marker"))));
	}

	void uncreatableStemOutputIsRejectedBeforeSessionStartup()
	{
		QTemporaryDir home;
		QVERIFY(home.isValid());
		const QString config = configForWorkingDirectory(home.path());
		QVERIFY(!config.isEmpty());
		const QString workingDirectory = QDir(home.path()).filePath(QStringLiteral("working"));
#ifdef ZENE_SOURCE_DIR
		const QString project = QDir(QStringLiteral(ZENE_SOURCE_DIR)).filePath(QStringLiteral("tests/emptyproject.mmp"));
#else
		const QString project = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../tests/emptyproject.mmp"));
#endif
		QTemporaryDir failedOutput;
		QVERIFY(failedOutput.isValid());
		const QString outputFile = QDir(failedOutput.path()).filePath(QStringLiteral("is-a-file"));
		QVERIFY(writeFile(outputFile, QByteArrayLiteral("occupied")));
		const CliRun mkdirFailure = runCli(home.path(), {QStringLiteral("--allowroot"), QStringLiteral("--config"), config,
			QStringLiteral("exportstems"), project, QStringLiteral("-o"), outputFile});
		QVERIFY2(!mkdirFailure.standardOutput.contains("Loading project..."),
			"the output-directory refusal ran after project loading");
		QCOMPARE(mkdirFailure.status, QProcess::NormalExit);
		QCOMPARE(mkdirFailure.exitCode, EXIT_FAILURE);
		QVERIFY(!QFile::exists(QDir(workingDirectory).filePath(QStringLiteral("zene-session-open.marker"))));
		QVERIFY(!QFile::exists(QDir(workingDirectory).filePath(QStringLiteral("zene-safe-start.marker"))));
	}
};

QTEST_GUILESS_MAIN(MainCliStartupTest)
#include "MainCliStartupTest.moc"
