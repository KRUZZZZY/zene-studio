/*
 * DataFileFormatTest.cpp - the project/preset format across the rename.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio, a derivative work of LMMS (https://lmms.io).
 * It is free software; you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * What this pins: the rename of the project/preset root element is
 * read-both / write-new.
 *
 *   read-both  - a file whose root still says the pre-rename name loads exactly
 *                as before (the loader has always keyed off documentElement(),
 *                never the root's tag name -- the one place that did match the
 *                name literally now accepts both).
 *   write-new  - the document this build constructs, and any re-save, carries
 *                the new root and the new creator.
 */

#include <QDomDocument>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

#include "DataFile.h"

using namespace lmms;

namespace
{

// A pre-rename project, as the published alpha wrote it (version 31 is the
// current format version, so no upgrade routine runs on it).
constexpr auto OLD_PROJECT = R"(<?xml version="1.0"?>
<!DOCTYPE lmms-project>
<lmms-project version="31" type="song" creator="LMMS" creatorversion="0.1.0" creatorplatform="linux" creatorplatformtype="ubuntu">
  <head timesig_numerator="4" timesig_denominator="4"/>
  <song>
    <trackcontainer type="song" width="5" height="10"/>
  </song>
</lmms-project>
)";

constexpr auto NEW_PROJECT = R"(<?xml version="1.0"?>
<!DOCTYPE zene-project>
<zene-project version="31" type="song" creator="Zene Studio" creatorversion="0.2.0">
  <head/>
  <song>
    <trackcontainer type="song" width="5" height="10"/>
  </song>
</zene-project>
)";

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

QDomElement rootOf( const QString & path )
{
	QFile f( path );
	if( !f.open( QIODevice::ReadOnly ) ) { return QDomElement(); }
	QDomDocument doc;
	if( !doc.setContent( f.readAll() ) ) { return QDomElement(); }
	return doc.documentElement();
}

} // namespace

class DataFileFormatTest : public QObject
{
	Q_OBJECT

private slots:

	// write-new: a document built by this code carries the new root, and says
	// that Zene Studio wrote it.
	void newDocumentWritesTheNewRoot()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString path = tmp.path() + "/fresh.mmp";

		DataFile doc( DataFile::Type::SongProject );
		QVERIFY( doc.writeFile( path, false ) );

		const QDomElement root = rootOf( path );
		QVERIFY2( !root.isNull(), "the written file must parse" );
		QCOMPARE( root.tagName(), QStringLiteral( "zene-project" ) );
		QCOMPARE( root.attribute( "creator" ), QStringLiteral( "Zene Studio" ) );
		QCOMPARE( root.attribute( "type" ), QStringLiteral( "song" ) );
	}

	// read-both: a pre-rename file still loads, and it loads as the type it says
	// it is with its head and content resolved.
	void preRenameFileStillLoads()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString path = tmp.path() + "/from-the-alpha.mmp";
		QVERIFY( writeFile( path, OLD_PROJECT ) );

		DataFile old( path );
		QCOMPARE( static_cast<int>( old.type() ),
			static_cast<int>( DataFile::Type::SongProject ) );
		QVERIFY2( !old.head().isNull(), "the <head> of a pre-rename file must resolve" );
		QVERIFY2( !old.content().isNull(), "the <song> content must resolve from the old root" );
		QCOMPARE( old.head().attribute( "timesig_numerator" ), QStringLiteral( "4" ) );
	}

	// read-both then write-new: opening an old file and saving it produces a file
	// with the new root -- the upgrade path a user actually takes.
	void reSavingAnOldFileWritesTheNewRoot()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString in = tmp.path() + "/old.mmp";
		const QString out = tmp.path() + "/re-saved.mmp";
		QVERIFY( writeFile( in, OLD_PROJECT ) );

		DataFile old( in );
		QVERIFY( old.writeFile( out, false ) );

		const QDomElement root = rootOf( out );
		QVERIFY( !root.isNull() );
		QCOMPARE( root.tagName(), QStringLiteral( "zene-project" ) );
		QCOMPARE( root.attribute( "creator" ), QStringLiteral( "Zene Studio" ) );
		// the pre-rename DOCTYPE is not carried into the new file
		QVERIFY2( !readFile( out ).contains( "DOCTYPE" ),
			"a file this build writes must not keep a pre-rename DOCTYPE" );
		// the file's own content survived the re-save
		QCOMPARE( root.firstChildElement( "head" ).attribute( "timesig_numerator" ),
			QStringLiteral( "4" ) );
		QVERIFY( !root.firstChildElement( "head" ).isNull() );
		QVERIFY( !root.firstChildElement( "song" ).isNull() );
	}

	// And the new root reads back in this build (both directions of read-both).
	void newRootFileLoads()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString path = tmp.path() + "/new.mmp";
		QVERIFY( writeFile( path, NEW_PROJECT ) );

		DataFile doc( path );
		QCOMPARE( static_cast<int>( doc.type() ),
			static_cast<int>( DataFile::Type::SongProject ) );
		QVERIFY( !doc.content().isNull() );
	}

	// The legacy `multimedia-project` root (pre-LMMS ZynAddSubFX era) is a
	// different format's identifier and must keep loading untouched.
	void legacyMultimediaRootStillLoads()
	{
		QTemporaryDir tmp;
		QVERIFY( tmp.isValid() );
		const QString path = tmp.path() + "/legacy.xpf";
		QVERIFY( writeFile( path, "<?xml version=\"1.0\"?>\n"
			"<!DOCTYPE multimedia-project>\n"
			"<multimedia-project version=\"1.0\" creator=\"LMMS\" "
			"type=\"instrumenttracksettings\">\n<head/>\n"
			"<instrumenttracksettings/>\n</multimedia-project>\n" ) );

		DataFile doc( path );
		QCOMPARE( static_cast<int>( doc.type() ),
			static_cast<int>( DataFile::Type::InstrumentTrackSettings ) );
		QVERIFY( !doc.content().isNull() );
	}
};

QTEST_GUILESS_MAIN( DataFileFormatTest )
#include "DataFileFormatTest.moc"
