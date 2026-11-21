/*
 * ImportFilterTest.cpp - exception safety for the import journal state.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <stdexcept>

#include "ConfigManager.h"
#include "Engine.h"
#include "ImportFilter.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "Song.h"

#ifndef Q_OS_WIN
#include <sys/resource.h>
#endif

using namespace lmms;

class ImportFilterTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
	#ifdef Q_OS_WIN
		QSKIP("The Windows test host cannot load plugin MODULE libraries linked to zene.exe");
	#else
		Engine::init(true);

		QDir::addSearchPath(QStringLiteral("plugins"),
			QStringLiteral(LMMS_TEST_THROWING_IMPORT_FILTER_PLUGIN_DIR));
		PluginFactory* factory = getPluginFactory();
		QDir::setSearchPaths(QStringLiteral("plugins"),
			QStringList{QStringLiteral(LMMS_TEST_THROWING_IMPORT_FILTER_PLUGIN_DIR)});
		factory->discoverPlugins();

		QVERIFY(factory->pluginInfo("throwingimportfilter").descriptor != nullptr);
		m_engineStarted = true;
#endif
	}

	void cleanupTestCase()
	{
		if (m_engineStarted) { Engine::destroy(); }
	}

	void anImporterExceptionRestoresJournalling()
	{
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal->isJournalling());

		QVERIFY_EXCEPTION_THROWN(
			ImportFilter::tryImportFile(QStringLiteral("throw-on-import"), Engine::getSong()),
			std::runtime_error);

		QVERIFY(journal->isJournalling());
	}

	void aBrokenLaunchTemplateCannotReenterFallback()
	{
		if (qEnvironmentVariableIsSet("ZENE_TEST_FALLBACK_CHILD"))
		{
			QTemporaryDir workingDir;
			QVERIFY(workingDir.isValid());

			ConfigManager* config = ConfigManager::inst();
			config->setWorkingDir(workingDir.path());
			QVERIFY(QDir().mkpath(config->userTemplateDir()));

			QFile fallbackTemplate(config->userTemplateDir() + QStringLiteral("default.mpt"));
			QVERIFY(fallbackTemplate.open(QIODevice::WriteOnly | QIODevice::Text));
			QVERIFY(fallbackTemplate.write("<mmp") > 0);
			fallbackTemplate.close();

		#ifndef Q_OS_WIN
			rlimit stackLimit{};
			QVERIFY(getrlimit(RLIMIT_STACK, &stackLimit) == 0);
			const rlim_t testStackLimit = 512 * 1024;
			if (stackLimit.rlim_cur == RLIM_INFINITY || stackLimit.rlim_cur > testStackLimit)
			{
				stackLimit.rlim_cur = testStackLimit;
			}
			QVERIFY(setrlimit(RLIMIT_STACK, &stackLimit) == 0);
		#endif

			Song* song = Engine::getSong();
			song->setLoadOnLaunch(true);
			song->loadProject(workingDir.filePath(QStringLiteral("missing-project.mpt")));

			QVERIFY(!song->isLoadingProject());
			QVERIFY(!song->loadRefusal().isEmpty());
			return;
		}

		QProcess child;
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QStringLiteral("ZENE_TEST_FALLBACK_CHILD"), QStringLiteral("1"));
		child.setProcessEnvironment(environment);
		child.setStandardOutputFile(QProcess::nullDevice());
		child.setStandardErrorFile(QProcess::nullDevice());
		child.start(QCoreApplication::applicationFilePath(),
			{QStringLiteral("aBrokenLaunchTemplateCannotReenterFallback")});
		QVERIFY(child.waitForStarted());

		const bool completed = child.waitForFinished(5000);
		if (!completed)
		{
			child.kill();
			child.waitForFinished();
		}
		QVERIFY2(completed, qPrintable(child.errorString()));
		QCOMPARE(child.exitStatus(), QProcess::NormalExit);
		QCOMPARE(child.exitCode(), 0);
	}

private:
	bool m_engineStarted = false;
};

QTEST_GUILESS_MAIN(ImportFilterTest)
#include "ImportFilterTest.moc"
