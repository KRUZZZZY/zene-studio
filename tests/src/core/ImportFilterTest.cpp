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
#include <stdexcept>

#include "Engine.h"
#include "ImportFilter.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "Song.h"

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

private:
	bool m_engineStarted = false;
};

QTEST_GUILESS_MAIN(ImportFilterTest)
#include "ImportFilterTest.moc"
