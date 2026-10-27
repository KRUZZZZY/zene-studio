/*
 * ConfigMigrationTest.cpp - adopting pre-rename user state.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio, a derivative work of LMMS (https://lmms.io).
 * It is free software; you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * What this pins: the product renamed its config file (`.lmmsrc.xml` ->
 * `.zenestudio.xml`), its working directory (`~/Documents/lmms/` ->
 * `~/Documents/Zene Studio/`) and its portable workspace directory.  A naive
 * rename of those paths would orphan every existing install's settings and the
 * folder its projects live in.  `ConfigMigration::adoptConfigFile` and
 * `ConfigMigration::adoptWorkingDir` are the whole of that adoption, and these
 * cases seed old-style state in a temporary directory and assert it is honoured.
 */

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#if defined(Q_OS_UNIX)
#include <signal.h>
#include <sys/resource.h>
#endif

#include "ConfigManager.h"

using namespace lmms;

namespace
{

constexpr auto OLD_CONFIG = R"(<?xml version="1.0"?>
<lmms version="1.0.99" configversion="2">
 <mixer audiodev="PulseAudio" samplerate="44100"/>
 <paths workingdir="/home/someone/Documents/lmms/"/>
</lmms>
)";

constexpr auto OLD_WORKING_DIR_MARKER = "<?xml version=\"1.0\"?>\n<lmms-project version=\"31\" creator=\"LMMS\"/>\n";

bool writeFile( const QString & path, const QByteArray & contents )
{
	QFile f( path );
	if( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) { return false; }
	return f.write( contents ) == contents.size();
}

QByteArray readFile( const QString & path )
{
	QFile f( path );
	if( !f.open( QIODevice::ReadOnly ) ) { return QByteArray(); }
	return f.readAll();
}

#if defined(Q_OS_UNIX)
bool saveWithFileSizeLimit( ConfigManager & configManager )
{
	struct rlimit originalLimit;
	if( getrlimit( RLIMIT_FSIZE, &originalLimit ) != 0 ) { return false; }

	struct sigaction originalAction {};
	struct sigaction ignoreAction {};
	ignoreAction.sa_handler = SIG_IGN;
	sigemptyset( &ignoreAction.sa_mask );
	if( sigaction( SIGXFSZ, &ignoreAction, &originalAction ) != 0 ) { return false; }

	struct rlimit limited = originalLimit;
	limited.rlim_cur = 0;
	const bool limitSet = setrlimit( RLIMIT_FSIZE, &limited ) == 0;
	if( limitSet ) { configManager.saveConfigFile(); }

	const bool limitRestored = setrlimit( RLIMIT_FSIZE, &originalLimit ) == 0;
	const bool signalRestored = sigaction( SIGXFSZ, &originalAction, nullptr ) == 0;
	return limitSet && limitRestored && signalRestored;
}
#endif

} // namespace

class ConfigMigrationTest : public QObject
{
	Q_OBJECT

private slots:

	// The config file is adopted: the user's settings survive, and they survive
	// at the NEW path, so the old name is not left behind either.
	void oldConfigFileIsAdoptedNotOrphaned()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString legacy = tmp.path() + "/.lmmsrc.xml";
		const QString fresh  = tmp.path() + "/.zenestudio.xml";
		QVERIFY( writeFile( legacy, OLD_CONFIG ) );

		QCOMPARE( ConfigMigration::adoptConfigFile( fresh, legacy ), fresh );

		QVERIFY2( QFileInfo::exists( fresh ), "the adopted config must exist at the new path" );
		QVERIFY2( !QFileInfo::exists( legacy ), "the old config name must not be left behind" );
		QCOMPARE( readFile( fresh ), QByteArray( OLD_CONFIG ) );  // adopted, not rewritten
	}

	// The working directory is adopted with its contents: the projects the user
	// already has stay found.
	void oldWorkingDirWithProjectsIsAdopted()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString legacyDir = tmp.path() + "/lmms";
		const QString newDir    = tmp.path() + "/Zene Studio";
		QVERIFY( QDir().mkpath( legacyDir + "/projects" ) );
		QVERIFY( writeFile( legacyDir + "/projects/tune.mmpz", OLD_WORKING_DIR_MARKER ) );

		QCOMPARE( ConfigMigration::adoptWorkingDir( newDir, legacyDir ), newDir );

		QVERIFY2( QDir( newDir ).exists(), "the adopted working directory must exist" );
		QVERIFY2( QFileInfo::exists( newDir + "/projects/tune.mmpz" ),
			"the user's project must move with the adopted directory" );
		QVERIFY2( !QDir( legacyDir ).exists(), "the old working directory name must not be left behind" );
	}

	// When both locations exist the user (or a previous run) already decided
	// which one holds what; the legacy item is left strictly alone.
	void newStateWinsWhenBothExist()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString legacy    = tmp.path() + "/.lmmsrc.xml";
		const QString fresh     = tmp.path() + "/.zenestudio.xml";
		const QString legacyDir = tmp.path() + "/lmms";
		const QString newDir    = tmp.path() + "/Zene Studio";
		QVERIFY( writeFile( legacy, "<lmms/>\n" ) );
		QVERIFY( writeFile( fresh, "<zene/>\n" ) );
		QVERIFY( QDir().mkpath( legacyDir + "/projects" ) );
		QVERIFY( QDir().mkpath( newDir + "/projects" ) );

		QCOMPARE( ConfigMigration::adoptConfigFile( fresh, legacy ), fresh );
		QCOMPARE( ConfigMigration::adoptWorkingDir( newDir, legacyDir ), newDir );

		QCOMPARE( readFile( fresh ), QByteArray( "<zene/>\n" ) );   // untouched
		QVERIFY( QFileInfo::exists( legacy ) );                     // not deleted
		QVERIFY( QDir( legacyDir ).exists() );                      // not deleted
	}

	// A fresh install must not have directories invented for it.
	void freshInstallCreatesNothing()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString fresh     = tmp.path() + "/.zenestudio.xml";
		const QString newDir    = tmp.path() + "/Zene Studio";
		const QString noLegacy  = tmp.path() + "/.lmmsrc.xml";
		const QString noLegacyDir = tmp.path() + "/lmms";

		QCOMPARE( ConfigMigration::adoptConfigFile( fresh, noLegacy ), fresh );
		QCOMPARE( ConfigMigration::adoptWorkingDir( newDir, noLegacyDir ), newDir );

		QVERIFY( !QFileInfo::exists( fresh ) );
		QVERIFY( !QDir( newDir ).exists() );
	}

	// If the legacy directory cannot be moved (here: it lives under a different
	// parent, as with a `~/lmms` that predates the Documents layout), the
	// product keeps using the directory the projects are actually in.  Pointing
	// at an empty new folder would make every project appear to vanish.
	void unrenamableWorkingDirFallsBackToTheLegacyLocation()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString legacyDir = tmp.path() + "/home/lmms";
		const QString newDir    = tmp.path() + "/elsewhere/Zene Studio";
		QVERIFY( QDir().mkpath( legacyDir + "/projects" ) );
		QVERIFY( writeFile( legacyDir + "/projects/tune.mmpz", OLD_WORKING_DIR_MARKER ) );

		QCOMPARE( ConfigMigration::adoptWorkingDir( newDir, legacyDir ), legacyDir );

		QVERIFY( QFileInfo::exists( legacyDir + "/projects/tune.mmpz" ) );
		QVERIFY( !QDir( newDir ).exists() );
	}

	// Read-both: the config root was renamed to <zene>, but an adopted file that
	// still says <lmms> must still parse and still yield the settings the reader
	// walks by child element and attribute -- neither of which the rename touches.
	void adoptedConfigIsStillReadable()
	{
		QDomDocument doc;
		QVERIFY( doc.setContent( QByteArray( OLD_CONFIG ) ) );

		const QDomElement root = doc.documentElement();
		QCOMPARE( root.tagName(), QStringLiteral( "lmms" ) );
		QCOMPARE( root.attribute( "version" ), QStringLiteral( "1.0.99" ) );
		QCOMPARE( root.attribute( "configversion" ), QStringLiteral( "2" ) );

		const QDomElement mixer = root.firstChildElement( QStringLiteral( "mixer" ) );
		QVERIFY( !mixer.isNull() );
		QCOMPARE( mixer.attribute( "audiodev" ), QStringLiteral( "PulseAudio" ) );
		QCOMPARE( mixer.attribute( "samplerate" ), QStringLiteral( "44100" ) );
	}

	// A config that does not parse must be reported, and the shutdown save must
	// not replace it with defaults: the file keeps the settings the user has.
	void malformedConfigIsReportedAndNotOverwritten()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString configPath = tmp.path() + "/.zenestudio.xml";
		const QByteArray malformed = "<zene version=\"1.0.99\" configversion=\"3\"><mixer audiodev=\"saved\"\n";
		QVERIFY( writeFile( configPath, malformed ) );

		auto * configManager = ConfigManager::inst();
		QTest::ignoreMessage( QtWarningMsg, QRegularExpression( QStringLiteral( "Error while parsing configuration file" ) ) );
		configManager->loadConfigFile( configPath );
		configManager->saveConfigFile();

		QCOMPARE( readFile( configPath ), malformed );
	}

	// Loading a readable config again clears the unreadable mark, so its next save
	// is written as usual.
	void readableReloadClearsTheUnreadableMark()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString badPath = tmp.path() + "/bad.xml";
		const QString goodPath = tmp.path() + "/zene.xml";
		const QByteArray malformed = "<zene version=\"1.0.99\" configversion=\"3\"><mixer audiodev=\"saved\"\n";
		const QByteArray good = "<zene version=\"1.0.99\" configversion=\"3\"/>\n";
		QVERIFY( writeFile( badPath, malformed ) );
		QVERIFY( writeFile( goodPath, good ) );

		auto * configManager = ConfigManager::inst();
		QTest::ignoreMessage( QtWarningMsg, QRegularExpression( QStringLiteral( "Error while parsing configuration file" ) ) );
		configManager->loadConfigFile( badPath );
		configManager->loadConfigFile( goodPath );
		configManager->saveConfigFile();

		QVERIFY2( readFile( goodPath ).startsWith( "<?xml" ), "a readable config must be saved again" );
	}

	void failedConfigWritePreservesExistingFile()
	{
#if defined(Q_OS_UNIX)
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString configPath = tmp.path() + "/.zenestudio.xml";
		const QString workingDir = tmp.path() + "/working";
		QVERIFY( QDir().mkpath( workingDir ) );
		const QByteArray original = ( QStringLiteral(
			"<zene version=\"1.0.99\" configversion=\"3\"><mixer audiodev=\"saved\"/>"
			"<paths workingdir=\"") + workingDir + QStringLiteral( "\"/></zene>\n" ) ).toUtf8();
		QVERIFY( writeFile( configPath, original ) );

		auto * configManager = ConfigManager::inst();
		configManager->loadConfigFile( configPath );
		QVERIFY( saveWithFileSizeLimit( *configManager ) );
		QCOMPARE( readFile( configPath ), original );
#else
		QSKIP( "RLIMIT_FSIZE is needed to induce a deterministic write failure" );
#endif
	}
};

QTEST_GUILESS_MAIN( ConfigMigrationTest )
#include "ConfigMigrationTest.moc"
