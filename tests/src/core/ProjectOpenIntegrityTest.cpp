/*
 * ProjectOpenIntegrityTest.cpp - save/load integrity defects that need a real
 * Song: the <session> block a build without the Session View reader used to
 * drop silently, the state a failed open left behind, and the routing
 * elements this build round-trips for a reader that cannot.
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
 *
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QTemporaryDir>

#include "DataFile.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

bool writeText( const QString& path, const QString& text )
{
	QFile file( path );
	if( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
	{
		return false;
	}
	return file.write( text.toUtf8() ) >= 0;
}


QString readText( const QString& path )
{
	QFile file( path );
	if( !file.open( QIODevice::ReadOnly ) )
	{
		return QString();
	}
	return QString::fromUtf8( file.readAll() );
}


//! The <session> ... </session> substring of a project file, for comparing one
//! save against the next (session blocks never nest).
QString sessionBlock( const QString& project )
{
	const int start = project.indexOf( QStringLiteral( "<session " ) );
	const int end = project.indexOf( QStringLiteral( "</session>" ) );
	if( start < 0 || end < 0 )
	{
		return QString();
	}
	return project.mid( start, end + 10 - start );
}


//! The <session> element of a saved project, or a null element.
QDomElement savedSession( const QString& project )
{
	QDomDocument parsed;
	if( !parsed.setContent( project.toUtf8() ) )
	{
		return QDomElement();
	}
	return parsed.documentElement()
		.firstChildElement( QStringLiteral( "song" ) )
		.firstChildElement( QStringLiteral( "session" ) );
}


//! A song project whose only interesting content is a version-1 <session>
//! block - what a session-aware build writes, and what no default build may
//! drop. Everything else is the minimal shape a project needs to load.
QString projectWithSessionBlock()
{
	DataFile fresh( DataFile::Type::SongProject );
	const QString version = fresh.documentElement().attribute( QStringLiteral( "version" ) );
	return QString(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"Zene Studio\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"        <pattern type=\"0\" name=\"p\" muted=\"0\" steps=\"16\" pos=\"0\" len=\"192\" frozen=\"0\">\n"
		"          <midiclip steps=\"16\" name=\"c\" muted=\"0\" pos=\"0\" len=\"192\" type=\"0\">\n"
		"            <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\"/>\n"
		"          </midiclip>\n"
		"        </pattern>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"    <session version=\"1\" tracks=\"2\" scenes=\"2\" launchquantisation=\"1\">\n"
		"      <scenes>\n"
		"        <scene index=\"0\" name=\"Verse\"/>\n"
		"      </scenes>\n"
		"      <clips>\n"
		"        <clip track=\"0\" scene=\"0\" type=\"1\" pattern=\"11\"/>\n"
		"      </clips>\n"
		"    </session>\n"
		"  </song>\n"
		"</lmms-project>\n" ).arg( version );
}

} // namespace


class ProjectOpenIntegrityTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init( true ); }

	void cleanupTestCase() { Engine::destroy(); }

	//! The deliverable: a project carrying a <session> block must not lose it
	//! when a build whose reader for that block is compiled out opens and
	//! re-saves it. WANT_SESSION_VIEW is OFF in the default configuration, so
	//! this is the configuration the shipped product runs.
	void sessionBlockSurvivesARoundTripThroughASessionBlindBuild()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		const QString project = dir.filePath( QStringLiteral( "with-session.mmp" ) );
		QVERIFY( writeText( project, projectWithSessionBlock() ) );

		Song* song = Engine::getSong();
		song->clearProject();
		song->setLoadOnLaunch( false );
		song->loadProject( project );

		const QString firstSave = dir.filePath( QStringLiteral( "round1.mmp" ) );
		QVERIFY( song->saveProjectFile( firstSave ) );
		const QString firstText = readText( firstSave );

		const QDomElement firstSession = savedSession( firstText );
		QVERIFY2( !firstSession.isNull(),
			"the <session> block was dropped by a load -> save round trip: a build "
			"without the Session View reader silently destroyed a feature's data" );
		QCOMPARE( firstSession.attribute( QStringLiteral( "version" ) ).toInt(), 1 );
		QCOMPARE( firstSession.attribute( QStringLiteral( "tracks" ) ).toInt(), 2 );
		QCOMPARE( firstSession.attribute( QStringLiteral( "scenes" ) ).toInt(), 2 );

		const QDomNodeList clips = firstSession.elementsByTagName( QStringLiteral( "clip" ) );
		QCOMPARE( clips.length(), 1 );
		QCOMPARE( clips.at( 0 ).toElement().attribute( QStringLiteral( "pattern" ) ).toInt(), 11 );
		const QDomNodeList scenes = firstSession.elementsByTagName( QStringLiteral( "scene" ) );
		QCOMPARE( scenes.length(), 1 );
		QCOMPARE( scenes.at( 0 ).toElement().attribute( QStringLiteral( "name" ) ),
			QStringLiteral( "Verse" ) );

		// ...and it is stable: the second round trip is byte-identical to the
		// first, so repeated opening and saving cannot degrade the block.
		song->loadProject( firstSave );
		const QString secondSave = dir.filePath( QStringLiteral( "round2.mmp" ) );
		QVERIFY( song->saveProjectFile( secondSave ) );
		QCOMPARE( sessionBlock( readText( secondSave ) ), sessionBlock( firstText ) );
	}

	//! Behaviour preservation: a project that never used the session view is
	//! untouched by the fix - load and save leave the file byte-identical.
	//! (With WANT_SESSION_VIEW=ON the same guarantee is held by
	//! SessionModelTest::preSessionProjectLoadsAndSavesUnchanged.)
	void projectWithoutASessionBlockIsUnchanged()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		Song* song = Engine::getSong();
		song->clearProject();
		song->setLoadOnLaunch( false );

		const QString firstSave = dir.filePath( QStringLiteral( "plain1.mmp" ) );
		QVERIFY( song->saveProjectFile( firstSave ) );
		const QString firstText = readText( firstSave );
		QVERIFY( !firstText.isEmpty() );
		QVERIFY( !firstText.contains( QStringLiteral( "session" ) ) );

		song->loadProject( firstSave );
		const QString secondSave = dir.filePath( QStringLiteral( "plain2.mmp" ) );
		QVERIFY( song->saveProjectFile( secondSave ) );
		QVERIFY( !readText( secondSave ).contains( QStringLiteral( "session" ) ) );
		QCOMPARE( readText( secondSave ), firstText );
	}

	//! D2: a failed open used to return before the success path re-enabled
	//! them, leaving modified-tracking, undo journalling and autosave off for
	//! the rest of the session. The user who opened one bad file and carried
	//! on had no autosave and no undo, with nothing said.
	void failedOpenLeavesTheDocumentUsable()
	{
		QTemporaryDir dir;
		QVERIFY( dir.isValid() );

		Song* song = Engine::getSong();
		song->clearProject();
		// The defect is on the path taken when a file is opened while the
		// application is already running; with m_loadOnLaunch set,
		// createNewProject() happens to reset both flags and hides it.
		song->setLoadOnLaunch( false );

		const QString bad = dir.filePath( QStringLiteral( "not-a-project.mmp" ) );
		QVERIFY( writeText( bad, QStringLiteral( "this is not a project file\n" ) ) );
		song->loadProject( bad );

		QVERIFY2( !song->isLoadingProject(),
			"a failed open left the loading flag set: MainWindow::autoSave() refuses "
			"to run while it is, so autosave stayed off until a restart" );
		QVERIFY2( Engine::projectJournal()->isJournalling(),
			"a failed open left undo journalling off for the rest of the session" );
		QVERIFY( !song->isExporting() );

		// Song::setModified(bool) is private, and it is a no-op while the
		// loading flag is set (Song.cpp:457). The document is unmodified at
		// this point (the previous load ended with setModified(false)), so the
		// public setModified() slot is the assertion that pins the defect:
		// before the fix it did nothing at all and the document could never
		// become modified again.
		QVERIFY( !song->isModified() );
		song->setModified();
		QVERIFY2( song->isModified(), "modified-tracking is disabled after a failed open" );

		// The documented advice after a failed open is "save and restart"; the
		// save has to work.
		const QString after = dir.filePath( QStringLiteral( "after-failed-open.mmp" ) );
		QVERIFY( song->saveProjectFile( after ) );
		QVERIFY( QFile::exists( after ) );
	}

	//! D6, the half that is this build's to keep: the elements an older build
	//! silently drops are written and read back losslessly here. The loss
	//! happens in the older reader, which no change in this tree can reach;
	//! the slide-note half is pinned by SlideNotesTest.
	//! See docs/SAVELOAD-INTEGRITY.md.
	void currentBuildRoundTripsBusSidechainAndPrefaderSends()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY( mixer != nullptr );
		mixer->clear();
		while( mixer->numChannels() < 3 )
		{
			mixer->createChannel();
		}
		const int bus = mixer->createBusChannel();
		mixer->deleteChannelSend( 1, 0 );
		mixer->createChannelSend( 1, bus, 0.8f, true ); // pre-fader send
		mixer->createSidechainSend( bus, 1, 1.0f, SidechainTapPoint::PreFader );

		QDomDocument saved;
		QDomElement savedRoot = saved.createElement( QStringLiteral( "root" ) );
		saved.appendChild( savedRoot );
		mixer->saveSettings( saved, savedRoot );
		const QString savedXml = saved.toString();

		QVERIFY2( savedXml.contains( QStringLiteral( "<bus" ) ),
			"this build did not write the parallel-bus marker" );
		QVERIFY2( savedXml.contains( QStringLiteral( "sidechain-send" ) ),
			"this build did not write the sidechain send" );
		QVERIFY2( savedXml.contains( QStringLiteral( "prefader=\"1\"" ) ),
			"this build did not write the pre-fader flag" );

		QDomDocument reloaded;
		QVERIFY( reloaded.setContent( savedXml.toUtf8() ) );
		mixer->loadSettings( reloaded.documentElement() );

		QVERIFY2( mixer->mixerChannel( bus )->isBus(),
			"the bus flag did not survive this build's own round trip" );
		QVERIFY2( mixer->channelSidechainSend( bus, 1 ) != nullptr,
			"the sidechain send did not survive this build's own round trip" );
	}
};

QTEST_GUILESS_MAIN( ProjectOpenIntegrityTest )
#include "ProjectOpenIntegrityTest.moc"
