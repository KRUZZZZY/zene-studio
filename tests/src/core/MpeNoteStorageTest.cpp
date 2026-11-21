/*
 * MpeNoteStorageTest.cpp - MPE per-note expression storage (task #601)
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
#include "NotePlayHandle.h"

#include "AllocationProbe.h"

#include <cstdint>

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


//! The attribute set a note ELEMENT FROM A DOCUMENT carries when the document
//! was written without this build: the six attributes a pre-MPE build writes.
//! This is the LEGACY set, deliberately without `id` - the fixture projects are
//! hand-written text with no ids, and noteWithoutExpressionIsUnchanged() checks a
//! parsed document against it. What this build WRITES is the other set, below.
QStringList upstreamNoteAttributes()
{
	return { "key", "len", "pan", "pos", "type", "vol" };
}

//! The set this build writes for a note it serialises: upstream + the note's
//! stable id. Note::saveSettings writes `id` UNCONDITIONALLY (src/core/Note.cpp,
//! SPEC-stable-ids.md R2, slice 2: "an identity is not a feature that can be
//! switched off"), so every note this build saves carries it. Still exact: an
//! attribute beyond these still fails.
QStringList savedNoteAttributes()
{
	QStringList names = upstreamNoteAttributes();
	names << QStringLiteral("id");
	names.sort();
	return names;
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

//! The same set as this build WRITES it: the legacy three-attribute addition plus
//! the note's stable id, which every saved note carries (savedNoteAttributes()
//! above says why).
QStringList savedMpeNoteAttributes()
{
	QStringList names = mpeNoteAttributes();
	names << QStringLiteral("id");
	names.sort();
	return names;
}


//! Q050-016: the same set plus the expression-stream point-list attributes.
QStringList streamNoteAttributes()
{
	QStringList names = upstreamNoteAttributes();
	names << "mpepitchpts" << "mpepressurepts" << "mpetimbrepts";
	names.sort();
	return names;
}

QStringList savedStreamNoteAttributes()
{
	QStringList names = streamNoteAttributes();
	names << QStringLiteral("id");
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
		QCOMPARE( attributeNames( element ), savedMpeNoteAttributes() );

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
		QCOMPARE( attributeNames( cleared ), savedNoteAttributes() );
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
		QCOMPARE( attributeNames( element ), savedNoteAttributes() );

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

	//! Q050-016 (a): a note's expression event streams survive save -> load ->
	//! save with their point ORDER and VALUES intact, and a note with no stream
	//! keeps exactly its prior serialized form.
	void expressionStreamsRoundTripAndStayOptional()
	{
		using namespace lmms;

		Note note( TimePos( 384 ), TimePos( 0 ), 60 );
		QVERIFY( !note.hasMpeStream() );

		MpeExpressionStream stream;
		stream.setPitch( { { 0, 0 }, { 96, 1200 }, { 192, -2400 } } );
		stream.setPressure( { { 0, 0 }, { 48, 64 }, { 192, 127 } } );
		stream.setTimbre( { { 96, 32 } } );
		note.setMpeStream( stream );
		QVERIFY( note.hasMpeStream() );

		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = note.saveState( doc, parent );
		// The order the points were written in is the order they are spelled.
		QCOMPARE( element.attribute( "mpepitchpts" ), QStringLiteral( "0:0,96:1200,192:-2400" ) );
		QCOMPARE( element.attribute( "mpepressurepts" ), QStringLiteral( "0:0,48:64,192:127" ) );
		QCOMPARE( element.attribute( "mpetimbrepts" ), QStringLiteral( "96:32" ) );
		QCOMPARE( attributeNames( element ), savedStreamNoteAttributes() );

		const QString first = nodeToString( element );

		Note loaded;
		loaded.restoreState( element );
		QVERIFY( loaded.hasMpeStream() );
		QCOMPARE( static_cast<int>( loaded.mpeStream().pitch().size() ), 3 );
		QCOMPARE( loaded.mpeStream().pitch()[0].tick, tick_t( 0 ) );
		QCOMPARE( loaded.mpeStream().pitch()[0].value, 0 );
		QCOMPARE( loaded.mpeStream().pitch()[1].tick, tick_t( 96 ) );
		QCOMPARE( loaded.mpeStream().pitch()[1].value, 1200 );
		QCOMPARE( loaded.mpeStream().pitch()[2].tick, tick_t( 192 ) );
		QCOMPARE( loaded.mpeStream().pitch()[2].value, -2400 );
		QCOMPARE( static_cast<int>( loaded.mpeStream().pressure().size() ), 3 );
		QCOMPARE( loaded.mpeStream().pressure()[2].value, 127 );
		QCOMPARE( static_cast<int>( loaded.mpeStream().timbre().size() ), 1 );
		QCOMPARE( loaded.mpeStream().timbre()[0].tick, tick_t( 96 ) );
		QCOMPARE( loaded.mpeStream().timbre()[0].value, 32 );

		// The loaded note holds its own snapshot (built by the loader), equal
		// to the original's.
		QVERIFY( &loaded.mpeStream() != &note.mpeStream() );
		QVERIFY( loaded.mpeStream().pitch() == note.mpeStream().pitch() );
		QVERIFY( loaded.mpeStream().pressure() == note.mpeStream().pressure() );

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement( "notes" );
		QDomElement element2 = loaded.saveState( doc2, parent2 );
		QCOMPARE( nodeToString( element2 ), first );

		// The copy constructor (MidiClip::addNote, NotePlayHandle) carries it
		// by sharing the note's immutable snapshot (round 3: no deep copy); an
		// edit of the copy publishes a new snapshot and leaves the original's
		// alone.
		Note copy( note );
		QVERIFY( copy.hasMpeStream() );
		QVERIFY( &copy.mpeStream() == &note.mpeStream() );
		QCOMPARE( copy.mpeStream().pressure()[2].value, 127 );
		MpeExpressionStream edited = copy.mpeStream();
		edited.setPressure( { { 0, 11 } } );
		copy.setMpeStream( edited );
		QCOMPARE( copy.mpeStream().pressure()[0].value, 11 );
		QCOMPARE( note.mpeStream().pressure()[2].value, 127 );

		// Clearing removes the stream and the attributes with it.
		note.clearMpeStream();
		QVERIFY( !note.hasMpeStream() );
		QDomDocument doc3;
		QDomElement parent3 = doc3.createElement( "notes" );
		QDomElement element3 = note.saveState( doc3, parent3 );
		QCOMPARE( attributeNames( element3 ), savedNoteAttributes() );

		// Clamping and ordering are the setter's own job: a hostile value is
		// bounded and out-of-order points are sorted by tick.
		Note hostile;
		MpeExpressionStream hostileStream;
		hostileStream.setPitch( { { 192, 99999 }, { 0, -99999 } } );
		hostile.setMpeStream( hostileStream );
		QCOMPARE( hostile.mpeStream().pitch()[0].tick, tick_t( 0 ) );
		QCOMPARE( hostile.mpeStream().pitch()[0].value, -MpeNoteExpression::MaxPitchCents );
		QCOMPARE( hostile.mpeStream().pitch()[1].tick, tick_t( 192 ) );
		QCOMPARE( hostile.mpeStream().pitch()[1].value, MpeNoteExpression::MaxPitchCents );
		Note hostileLoad;
		QDomDocument doc4;
		QDomElement hostileElement = doc4.createElement( "note" );
		hostileElement.setAttribute( "key", 60 );
		hostileElement.setAttribute( "mpepressurepts", "oops,0:notanumber,48:300" );
		hostileLoad.restoreState( hostileElement );
		QVERIFY( hostileLoad.hasMpeStream() );
		QCOMPARE( static_cast<int>( hostileLoad.mpeStream().pressure().size() ), 1 );
		QCOMPARE( hostileLoad.mpeStream().pressure()[0].tick, tick_t( 48 ) );
		QCOMPARE( hostileLoad.mpeStream().pressure()[0].value, 127 );
	}

	//! Q050-016 (b): a whole project carries the stream attributes and round
	//! trips, while a project whose notes have no stream is byte-identical.
	void expressionStreamsPersistInAProject()
	{
		using namespace lmms;

		const QString version = DataFile( DataFile::Type::SongProject )
			.documentElement().attribute( "version" );

		// An old project (no stream) loads with no stream and re-saves exactly.
		DataFile old( projectWithNotes( version, oldProjectNotes ).toUtf8() );
		const QString oldText = fileToString( old );
		QVERIFY( !oldText.contains( QStringLiteral( "pts" ) ) );
		QDomDocument oldParsed;
		QVERIFY( oldParsed.setContent( oldText.toUtf8() ) );
		QDomElement oldNote = oldParsed.elementsByTagName( QStringLiteral( "note" ) )
			.item( 0 ).toElement();
		Note loadedFromOld;
		loadedFromOld.restoreState( oldNote );
		QVERIFY( !loadedFromOld.hasMpeStream() );

		// This build writes the stream as three optional attributes and nothing
		// else moves.
		const QString streamNotes =
			"            <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\""
			" mpepitchpts=\"0:0,96:1200,192:-2400\" mpepressurepts=\"0:0,48:64\""
			" mpetimbrepts=\"48:32\"/>\n"
			"            <note key=\"67\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"384\" type=\"0\"/>\n";
		DataFile streamed( projectWithNotes( version, streamNotes ).toUtf8() );
		const QString written = fileToString( streamed );
		QDomDocument parsed;
		QVERIFY( parsed.setContent( written.toUtf8() ) );
		const QDomNodeList notes = parsed.elementsByTagName( QStringLiteral( "note" ) );
		QCOMPARE( notes.length(), 2 );

		Note loaded;
		loaded.restoreState( notes.item( 0 ).toElement() );
		QVERIFY( loaded.hasMpeStream() );
		QCOMPARE( static_cast<int>( loaded.mpeStream().pitch().size() ), 3 );
		QCOMPARE( loaded.mpeStream().pitch()[1].tick, tick_t( 96 ) );
		QCOMPARE( loaded.mpeStream().pitch()[1].value, 1200 );
		QCOMPARE( static_cast<int>( loaded.mpeStream().pressure().size() ), 2 );
		QCOMPARE( loaded.mpeStream().pressure()[1].value, 64 );
		QCOMPARE( static_cast<int>( loaded.mpeStream().timbre().size() ), 1 );
		QCOMPARE( loaded.mpeStream().timbre()[0].value, 32 );
		// Upstream fields are untouched.
		QCOMPARE( loaded.key(), loadedFromOld.key() );
		QCOMPARE( loaded.getVolume(), loadedFromOld.getVolume() );
		QVERIFY( loaded.pos() == loadedFromOld.pos() );
		QVERIFY( loaded.length() == loadedFromOld.length() );
		// Every note but the first carries no stream, and no stream attributes.
		Note plain;
		plain.restoreState( notes.item( 1 ).toElement() );
		QVERIFY( !plain.hasMpeStream() );
		QCOMPARE( attributeNames( notes.item( 1 ).toElement() ), upstreamNoteAttributes() );
		// A save -> load -> save of the streamed project is stable.
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
	//! Q050-016 realtime: copying a note that carries a stream allocates
	//! nothing and frees nothing, absolutely (the probe counts both, in
	//! library code too) - the copy takes a counted reference to the note's
	//! immutable snapshot (round 3, include/MpeStreamSnapshot.h), so the
	//! NotePlayHandle the audio thread builds by copying the note costs the
	//! stream no allocation. A plain note copies allocation-free too. The
	//! concurrent half (edits racing copies, the realtime final release) is
	//! MpeStreamSnapshotTest.
	void streamedNoteCopiesAllocateNothing()
	{
		using namespace lmms;

		Note streamed( TimePos( 384 ), TimePos( 0 ), 60 );
		MpeExpressionStream stream;
		stream.setPitch( { { 0, 0 }, { 96, 1200 } } );
		stream.setPressure( { { 0, 64 } } );
		stream.setTimbre( { { 48, 32 } } );
		streamed.setMpeStream( stream );
		QVERIFY( streamed.hasMpeStream() );
		Note plain( TimePos( 384 ), TimePos( 0 ), 60 );

		std::uint64_t allocations = 0;
		std::uint64_t frees = 0;
		{
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			Note copy( streamed );
			Note assigned;
			assigned = streamed;
			Note plainCopy( plain );
			allocations = lmms::test::tlAllocationCount;
			frees = lmms::test::tlFreeCount;
			lmms::test::tlCountAllocations = false;

			QVERIFY( copy.hasMpeStream() );
			QVERIFY( copy.mpeStream().pitch() == streamed.mpeStream().pitch() );
			QVERIFY( assigned.mpeStream().pressure() == streamed.mpeStream().pressure() );
			// ... and an edit of one publishes a new snapshot for it alone.
			MpeExpressionStream edited = copy.mpeStream();
			edited.setPressure( { { 0, 11 } } );
			copy.setMpeStream( edited );
			QCOMPARE( streamed.mpeStream().pressure()[0].value, 64 );
			QCOMPARE( copy.mpeStream().pressure()[0].value, 11 );
		}

		QCOMPARE( allocations, 0ull );
		QCOMPARE( frees, 0ull );
	}

	//! Q050-016 round 2 (LLP64): stream-point frame offsets stay exact past
	//! 2^31 frames, where a 32-bit `long` (Windows) truncates them.
	void streamPointOffsetsStay64BitPast2ToThe31Frames()
	{
		using namespace lmms;

		// tick * framesPerTick overflows 32 bits: 100000 * 24000 = 2.4e9.
		const f_cnt_t at = NotePlayHandle::mpeStreamPointOffset(
			100000, 0, static_cast<f_cnt_t>( 3000000000ULL ), 0, 24000.0 );
		QCOMPARE( at, static_cast<f_cnt_t>( 2400000000ULL ) );
		QVERIFY( at > static_cast<f_cnt_t>( 2147483647ULL ) );

		// ... and so does a period that STARTS past 2^31: the point at tick
		// 208334 names frame 208334 * 24000 = 5000016000, 100 into the period.
		const f_cnt_t late = NotePlayHandle::mpeStreamPointOffset(
			208334, static_cast<f_cnt_t>( 5000015900ULL ), 4096, 7, 24000.0 );
		QCOMPARE( late, static_cast<f_cnt_t>( 107 ) );

		// A point before the period clamps to its start; one past its end is
		// not in it.
		QCOMPARE( NotePlayHandle::mpeStreamPointOffset( 0,
				static_cast<f_cnt_t>( 5000015900ULL ), 4096, 7, 24000.0 ),
			static_cast<f_cnt_t>( 7 ) );
		QCOMPARE( NotePlayHandle::mpeStreamPointOffset( 10, 0, 100, 0, 24000.0 ),
			NotePlayHandle::kNoMpeStreamOffset );
	}
};

QTEST_GUILESS_MAIN(MpeNoteStorageTest)
#include "MpeNoteStorageTest.moc"
