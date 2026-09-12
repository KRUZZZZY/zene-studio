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
#include <QTemporaryDir>
#include <QtTest>

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
};

QTEST_GUILESS_MAIN( ConfigMigrationTest )
#include "ConfigMigrationTest.moc"
