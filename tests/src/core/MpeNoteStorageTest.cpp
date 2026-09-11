/*
 * MpeNoteStorageTest.cpp - MPE per-note expression storage (task #601)
 *
 * Copyright (c) 2026 Zene Studio developers
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

//! MPE (task #601) is per-note expression: the controller gives every note its
//! own MIDI channel, and the bend / channel pressure / CC74 arriving on that
//! channel belongs to that note (docs/MPE.md). This half covers what a note carries and what lands in the
//! project file: the optional "mpepitch"/"mpepressure"/"mpetimbre" attributes,
//! an old project that has none of them, and the render fixtures themselves.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include "DataFile.h"
#include "MpeExpression.h"
#include "Note.h"

namespace
{

QString nodeToString( const QDomNode& node )
{
	QString out;
	QTextStream stream( &out );
	node.save( stream, 2 );
	stream.flush();
	return out;
}


QString fileToString( lmms::DataFile& df )
{
	QString out;
	QTextStream stream( &out );
	df.write( stream );
	stream.flush();
	return out;
}


QStringList attributeNames( const QDomElement& element )
{
	QStringList names;
	const QDomNamedNodeMap attributes = element.attributes();
	for ( int i = 0; i < attributes.length(); ++i )
	{
		names << attributes.item( i ).nodeName();
	}
	names.sort();
	return names;
}


//! The attribute set an upstream note has always had, i.e. every attribute a
//! build without this feature would find (and read) on a note.


QStringList upstreamNoteAttributes()
{
	return { "key", "len", "pan", "pos", "type", "vol" };
}


//! The same set plus this feature's three optional attributes, in the sorted
//! order attributeNames() returns.


QStringList mpeNoteAttributes()
{
	QStringList names = upstreamNoteAttributes();
	names << "mpepitch" << "mpepressure" << "mpetimbre";
	names.sort();
	return names;
}


//! A minimal project, byte-for-byte the shape a pre-MPE build writes: a
//! triple-oscillator track with one MIDI clip holding \a notes. Used to load an
//! "old project" and a project written by this build without deriving either
//! from production code.


QString projectWithNotes( const QString& version, const QString& notes )
{
	return QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"        <pattern type=\"0\" name=\"p\" muted=\"0\" steps=\"16\" pos=\"0\" len=\"192\" frozen=\"0\">\n"
		"          <midiclip steps=\"16\" name=\"c\" muted=\"0\" pos=\"0\" len=\"192\" type=\"0\">\n"
		"%2"
		"          </midiclip>\n"
		"        </pattern>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n" ).arg( version, notes );
}


const char* oldProjectNotes =
	"            <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\"/>\n"
	"            <note key=\"67\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"384\" type=\"0\"/>\n";

//! The same two notes as an MPE build writes then: the middle note carries the
//! expression an MPE controller sent on its own channel.


const char* mpeProjectNotes =
	"            <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\""
	" mpepitch=\"2400\" mpepressure=\"100\" mpetimbre=\"30\"/>\n"
	"            <note key=\"67\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"384\" type=\"0\"/>\n";

//! Mirrors the default key mapping of NotePlayHandle::updateFrequency()
//! (SlideNotesTest does the same for slide notes).


} // namespace


class MpeNoteStorageTest : public QObject
{
	Q_OBJECT

private slots:
	//! (d) Storage: a note's expression survives save -> load -> save
	//! identically, and the accessors clamp what they store.
	void noteExpressionRoundTrips()
	{
		using namespace lmms;

		Note original( TimePos( 384 ), TimePos( 192 ), 61 );
		original.setVolume( 80 );
		original.setMpeExpression( MpeNoteExpression{ -1200, 111, 22 } );
		QVERIFY( original.hasMpeExpression() );

		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = original.saveState( doc, parent );
		QVERIFY( !element.isNull() );
		QCOMPARE( element.attribute( "mpepitch" ).toInt(), -1200 );
		QCOMPARE( element.attribute( "mpepressure" ).toInt(), 111 );
		QCOMPARE( element.attribute( "mpetimbre" ).toInt(), 22 );
		QCOMPARE( attributeNames( element ), mpeNoteAttributes() );

		const QString first = nodeToString( element );

		Note loaded;
		loaded.restoreState( element );
		QVERIFY( loaded.hasMpeExpression() );
		QCOMPARE( loaded.mpePitchCents(), -1200 );
		QCOMPARE( loaded.mpePressure(), 111 );
		QCOMPARE( loaded.mpeTimbre(), 22 );
		QVERIFY( loaded.mpeExpression() == original.mpeExpression() );
		QCOMPARE( loaded.key(), 61 );

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement( "notes" );
		QDomElement element2 = loaded.saveState( doc2, parent2 );
		QCOMPARE( nodeToString( element2 ), first );

		// The copy constructor (MidiClip::addNote, NotePlayHandle) and
		// assignment carry it too.
		Note copy( original );
		QVERIFY( copy.mpeExpression() == original.mpeExpression() );
		Note assigned;
		QVERIFY( !assigned.hasMpeExpression() );
		assigned = original;
		QVERIFY( assigned.mpeExpression() == original.mpeExpression() );

		// "Captured but neutral" is not the same as "no expression": the
		// attributes are written with value 0 so the capture survives a reload.
		Note neutral( TimePos( 384 ), TimePos( 0 ), 60 );
		neutral.setMpePressure( 0 );
		QVERIFY( neutral.hasMpeExpression() );
		QVERIFY( neutral.mpeExpression().isNeutral() );
		QDomDocument doc3;
		QDomElement parent3 = doc3.createElement( "notes" );
		QDomElement element3 = neutral.saveState( doc3, parent3 );
		QVERIFY( element3.hasAttribute( "mpepitch" ) );
		Note neutralLoaded;
		neutralLoaded.restoreState( element3 );
		QVERIFY( neutralLoaded.hasMpeExpression() );

		// Clamping: the note cannot hold more than the MPE range or a value
		// outside 0..127.
		Note clamped;
		clamped.setMpePitchCents( 99999 );
		clamped.setMpePressure( 300 );
		clamped.setMpeTimbre( -5 );
		QCOMPARE( clamped.mpePitchCents(), MpeNoteExpression::MaxPitchCents );
		QCOMPARE( clamped.mpePressure(), 127 );
		QCOMPARE( clamped.mpeTimbre(), 0 );
		// ... and neither can a hostile project file.
		QDomDocument doc4;
		QDomElement hostile = doc4.createElement( "note" );
		hostile.setAttribute( "key", 60 );
		hostile.setAttribute( "mpepitch", 123456 );
		hostile.setAttribute( "mpepressure", -1 );
		hostile.setAttribute( "mpetimbre", 4000 );
		Note hostileNote;
		hostileNote.restoreState( hostile );
		QCOMPARE( hostileNote.mpePitchCents(), MpeNoteExpression::MaxPitchCents );
		QCOMPARE( hostileNote.mpePressure(), 0 );
		QCOMPARE( hostileNote.mpeTimbre(), 127 );

		// Clearing removes the expression and its attributes with it.
		original.clearMpeExpression();
		QVERIFY( !original.hasMpeExpression() );
		QDomDocument doc5;
		QDomElement parent5 = doc5.createElement( "notes" );
		QDomElement cleared = original.saveState( doc5, parent5 );
		QCOMPARE( attributeNames( cleared ), upstreamNoteAttributes() );
	}

	//! (e) A note with no expression serializes exactly as it did before this
	//! feature existed.
	void noteWithoutExpressionIsUnchanged()
	{
		using namespace lmms;

		Note plain( TimePos( 384 ), TimePos( 0 ), 60 );
		QVERIFY( !plain.hasMpeExpression() );
		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = plain.saveState( doc, parent );
		QCOMPARE( attributeNames( element ), upstreamNoteAttributes() );

		// ... and so does a whole project: no note in it grows an attribute.
		const QString version = DataFile( DataFile::Type::SongProject )
			.documentElement().attribute( "version" );
		QVERIFY( !version.isEmpty() );
		DataFile project( projectWithNotes( version, oldProjectNotes ).toUtf8() );
		QVERIFY( project.type() == DataFile::Type::SongProject );
		const QString first = fileToString( project );
		QVERIFY( !first.contains( QStringLiteral( "mpe" ) ) );
		QDomDocument parsed;
		QVERIFY( parsed.setContent( first.toUtf8() ) );
		const QDomNodeList notes = parsed.elementsByTagName( QStringLiteral( "note" ) );
		QCOMPARE( notes.length(), 2 );
		for ( int i = 0; i < notes.length(); ++i )
		{
			QCOMPARE( attributeNames( notes.item( i ).toElement() ), upstreamNoteAttributes() );
		}
		// Byte-identical re-save.
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( fileToString( roundTrip ), first );
	}

	//! (f) An old project loads with no expression anywhere, and a project this
	//! build wrote carries exactly the three new attributes - nothing else
	//! changed, which is what an older build reads (and ignores).
	void expressionIsAdditiveOnDisk()
	{
		using namespace lmms;

		const QString version = DataFile( DataFile::Type::SongProject )
			.documentElement().attribute( "version" );

		DataFile old( projectWithNotes( version, oldProjectNotes ).toUtf8() );
		QDomDocument oldParsed;
		QVERIFY( oldParsed.setContent( fileToString( old ).toUtf8() ) );
		QDomElement oldNote = oldParsed.elementsByTagName( QStringLiteral( "note" ) )
			.item( 0 ).toElement();
		Note loadedFromOld;
		loadedFromOld.restoreState( oldNote );
		QVERIFY( !loadedFromOld.hasMpeExpression() );
		QCOMPARE( loadedFromOld.mpePitchCents(), 0 );
		QCOMPARE( loadedFromOld.mpePressure(), 0 );
		QCOMPARE( loadedFromOld.mpeTimbre(), 0 );

		DataFile mpe( projectWithNotes( version, mpeProjectNotes ).toUtf8() );
		const QString written = fileToString( mpe );
		QDomDocument mpeParsed;
		QVERIFY( mpeParsed.setContent( written.toUtf8() ) );
		const QDomNodeList notes = mpeParsed.elementsByTagName( QStringLiteral( "note" ) );
		QCOMPARE( notes.length(), 2 );

		QDomElement expressive = notes.item( 0 ).toElement();
		Note loadedFromMpe;
		loadedFromMpe.restoreState( expressive );
		QVERIFY( loadedFromMpe.hasMpeExpression() );
		QCOMPARE( loadedFromMpe.mpePitchCents(), 2400 );
		QCOMPARE( loadedFromMpe.mpePressure(), 100 );
		QCOMPARE( loadedFromMpe.mpeTimbre(), 30 );
		// Every upstream field is the same as it loads from the old project:
		// the expression is additive, nothing else moved.
		QCOMPARE( loadedFromMpe.key(), loadedFromOld.key() );
		QCOMPARE( loadedFromMpe.getVolume(), loadedFromOld.getVolume() );
		QCOMPARE( loadedFromMpe.getPanning(), loadedFromOld.getPanning() );
		QVERIFY( loadedFromMpe.pos() == loadedFromOld.pos() );
		QVERIFY( loadedFromMpe.length() == loadedFromOld.length() );
		// The extra attributes are exactly the three MPE ones - i.e. all an
		// older build sees and ignores.
		QCOMPARE( attributeNames( expressive ), mpeNoteAttributes() );
		// The note with no capture did not gain anything at all.
		QCOMPARE( attributeNames( notes.item( 1 ).toElement() ), upstreamNoteAttributes() );
		// And a save -> load -> save of the expressive project is stable.
		DataFile rewrite( written.toUtf8() );
		QCOMPARE( fileToString( rewrite ), written );
	}

	//! (i) The render fixtures in tests/data/mpe are real, loadable projects,
	//! and the expression each one carries is the expression it claims to carry
	//! (docs/MPE.md renders exactly these files).
	void renderFixturesLoadWithTheExpressionTheyCarry()
	{
		using namespace lmms;

		struct Fixture
		{
			const char* file;
			bool firstHasExpression;
			bool secondHasExpression;
			int pitch;
			int pressure;
			int timbre;
		};
		const Fixture fixtures[] = {
			{ "mpe-plain.mmp",      false, false,    0,  0,  0 },
			{ "mpe-neutral.mmp",    true,  true,     0,  0,  0 },
			{ "mpe-expression.mmp", false, true,  1200, 64, 32 },
		};

		for( const Fixture& fixture : fixtures )
		{
			QFile file( QStringLiteral( LMMS_TEST_DATA_DIR ) + QStringLiteral( "/mpe/" )
				+ QString::fromLatin1( fixture.file ) );
			QVERIFY2( file.open( QIODevice::ReadOnly ), fixture.file );

			DataFile data( file.readAll() );
			QVERIFY2( data.type() == DataFile::Type::SongProject, fixture.file );

			const QDomNodeList notes = data.content()
				.elementsByTagName( QStringLiteral( "note" ) );
			QCOMPARE( notes.length(), 2 );

			Note first;
			Note second;
			first.restoreState( notes.item( 0 ).toElement() );
			second.restoreState( notes.item( 1 ).toElement() );
			QCOMPARE( first.key(), 57 );
			QCOMPARE( second.key(), 57 );
			QVERIFY( second.pos() == TimePos( 384 ) );

			QCOMPARE( first.hasMpeExpression(), fixture.firstHasExpression );
			QCOMPARE( second.hasMpeExpression(), fixture.secondHasExpression );
			QCOMPARE( second.mpePitchCents(), fixture.pitch );
			QCOMPARE( second.mpePressure(), fixture.pressure );
			QCOMPARE( second.mpeTimbre(), fixture.timbre );
			// The plain fixture is exactly that: no expression attribute
			// anywhere in it (the track is named "mpe-fixture", so look for
			// the attribute names, not the substring "mpe").
			if( !fixture.firstHasExpression && !fixture.secondHasExpression )
			{
				const QString serialized = nodeToString( data.content() );
				QVERIFY( !serialized.contains( QStringLiteral( "mpepitch" ) ) );
				QVERIFY( !serialized.contains( QStringLiteral( "mpepressure" ) ) );
				QVERIFY( !serialized.contains( QStringLiteral( "mpetimbre" ) ) );
			}
		}
	}
};

QTEST_GUILESS_MAIN(MpeNoteStorageTest)
#include "MpeNoteStorageTest.moc"
