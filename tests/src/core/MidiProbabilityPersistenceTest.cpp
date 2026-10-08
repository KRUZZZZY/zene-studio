/*
 * MidiProbabilityPersistenceTest.cpp - note probability / velocity jitter
 * survive save and reload, and a project that does not use them is unchanged
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

//! This mirrors the slide-notes persistence proof deliberately, because the
//! serialisation rule is the same one: an optional attribute written only when
//! set, so an old project loads with the old behaviour and re-saves its content
//! unchanged. Not its bytes: see savedDocumentDigest for why a document re-save
//! is compared through the index's canonical form and not byte for byte.

#include <QtTest>

#include <cmath>
#include <limits>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QStringList>
#include <QTextStream>

#include "DataFile.h"
#include "DocumentIndex.h"
#include "Note.h"
#include "NoteRandom.h"

using namespace lmms;

namespace
{

QString fileToString( DataFile& df )
{
	QString out;
	QTextStream stream( &out );
	df.write( stream );
	stream.flush();
	return out;
}

QString nodeToString( const QDomNode& node )
{
	QString out;
	QTextStream stream( &out );
	node.save( stream, 2 );
	stream.flush();
	return out;
}

//! The digest of a saved document in the form the document index canonicalises
//! a tree to: attributes sorted by name, insignificant whitespace and comments
//! dropped. tests/src/core/DocumentIndexTest.cpp pins that form, including that
//! two spellings of one element disagreeing only in the order its attributes
//! were written digest identically.
//!
//! A byte comparison cannot be asked for what this answers. QDomElement::save()
//! emits an element's attributes in Qt's attribute QHash order, and Qt seeds
//! that hash PER PROCESS, so two saves of one unchanged DOCUMENT differ in
//! attribute order a fraction of the time - measured 2 failures / 300 runs
//! (card #735). The two sides here are independently PARSED documents, which is
//! what makes their hash states differ; two elements built by the same setter
//! path, as in probabilityRoundTripsThroughSerialisation below, keep one order
//! and can still be compared byte for byte. Everything else is still compared:
//! every attribute name and value, the child order, and the character data.
//! Only the seed is not.
//!
//! Parsed through the one-argument QDomDocument::setContent this file already
//! uses, which leaves namespace processing off exactly as the project's own
//! reader does (DocumentIndex.h documents the rule). A document that does not
//! parse answers "<unparseable>", which no `sha256:` digest can equal, so a
//! broken side fails the comparison instead of matching an empty one.
QString savedDocumentDigest( const QString& saved )
{
	QDomDocument parsed;
	if( !parsed.setContent( saved.toUtf8() ) )
	{
		return QStringLiteral( "<unparseable>" );
	}
	return sectionDigest( parsed.documentElement() );
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

//! A song project with the upstream note attribute set only - the shape every
//! project saved before MIDI depth has.
QString legacyProject( const QString& version )
{
	return QString(
		"<?xml version=\"1.0\"?>\n"
		"<lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\">\n"
		"  <head/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track type=\"0\" name=\"t\" muted=\"0\">\n"
		"        <instrumenttrack/>\n"
		"        <pattern type=\"0\" name=\"p\" muted=\"0\" steps=\"16\" pos=\"0\" len=\"192\" frozen=\"0\">\n"
		"          <midiclip steps=\"16\" name=\"c\" muted=\"0\" pos=\"0\" len=\"192\" type=\"0\">\n"
		"            <note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\"/>\n"
		"            <note key=\"67\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"384\" type=\"0\"/>\n"
		"          </midiclip>\n"
		"        </pattern>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n" ).arg( version );
}

//! A <note> element carrying the given raw prob/veljit ATTRIBUTE TEXT, exactly
//! as a hand-edited or corrupt project file would spell it. Built through
//! QDomElement::setAttribute so the string reaches Note::loadSettings
//! untouched - QString::toFloat has to see the same bytes the file has.
QDomElement noteElementWith( QDomDocument& doc, QDomElement& parent, const char* prob,
	const char* velJitter )
{
	QDomElement note = doc.createElement( QStringLiteral( "note" ) );
	note.setAttribute( QStringLiteral( "key" ), QStringLiteral( "60" ) );
	note.setAttribute( QStringLiteral( "vol" ), QStringLiteral( "100" ) );
	note.setAttribute( QStringLiteral( "pan" ), QStringLiteral( "0" ) );
	note.setAttribute( QStringLiteral( "len" ), QStringLiteral( "384" ) );
	note.setAttribute( QStringLiteral( "pos" ), QStringLiteral( "0" ) );
	note.setAttribute( QStringLiteral( "type" ), QStringLiteral( "0" ) );
	note.setAttribute( QStringLiteral( "prob" ), QLatin1String( prob ) );
	note.setAttribute( QStringLiteral( "veljit" ), QLatin1String( velJitter ) );
	parent.appendChild( note );
	return note;
}

} // namespace


class MidiProbabilityPersistenceTest : public QObject
{
	Q_OBJECT
private slots:
	//! (a) A default note neither carries nor writes the new attributes.
	void defaultNoteHasNoMidiDepthAttributes()
	{
		Note plain( TimePos( 384 ), TimePos( 0 ), 60 );
		QCOMPARE( plain.probability(), 1.f );
		QCOMPARE( plain.velocityJitter(), 0.f );

		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = plain.saveState( doc, parent );
		QVERIFY( !element.isNull() );
		QVERIFY( !element.hasAttribute( "prob" ) );
		QVERIFY( !element.hasAttribute( "veljit" ) );

		// The upstream set plus the one attribute every note element has carried
		// since slice 2: the note's stable id, which Note::saveSettings writes
		// UNCONDITIONALLY (src/core/Note.cpp, SPEC-stable-ids.md R2 - "an
		// identity is not a feature that can be switched off"). The list is
		// still exact: the two new OPTIONAL attributes must be absent, and any
		// further addition fails here.
		const QStringList upstreamAttributes{ "id", "key", "len", "pan", "pos", "type", "vol" };
		QCOMPARE( attributeNames( element ), upstreamAttributes );

		// loading such an element gives the old behaviour back
		Note loaded;
		loaded.restoreState( element );
		QCOMPARE( loaded.probability(), 1.f );
		QCOMPARE( loaded.velocityJitter(), 0.f );
	}

	//! Invalid stored probabilities retain the documented default: the note plays.
	void invalidProbabilityFallsBackToDefault_data()
	{
		QTest::addColumn<QString>( "value" );
		QTest::newRow( "malformed" ) << QStringLiteral( "oops" );
		QTest::newRow( "non-finite" ) << QStringLiteral( "nan" );
	}

	void invalidProbabilityFallsBackToDefault()
	{
		QFETCH( QString, value );
		Note source;
		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = source.saveState( doc, parent );
		element.setAttribute( "prob", value );

		Note loaded;
		loaded.restoreState( element );
		QCOMPARE( loaded.probability(), 1.f );
	}

	//! (b) A note with probability and jitter set survives save -> load ->
	//! save with identical bytes, and the values read back exactly.
	void probabilityRoundTripsThroughSerialisation()
	{
		Note original( TimePos( 384 ), TimePos( 192 ), 61 );
		original.setVolume( 80 );
		original.setPanning( -25 );
		original.setProbability( 0.5f );
		original.setVelocityJitter( 0.25f );

		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = original.saveState( doc, parent );
		QVERIFY( !element.isNull() );

		QCOMPARE( element.attribute( "prob" ), QString( "0.5" ) );
		QCOMPARE( element.attribute( "veljit" ), QString( "0.25" ) );
		QCOMPARE( element.attribute( "key" ).toInt(), 61 );
		QCOMPARE( element.attribute( "vol" ).toInt(), 80 );

		// ...and the id, which every note element carries (Note::saveSettings,
		// SPEC-stable-ids.md R2): the exact set, sorted.
		const QStringList expectedAttributes{ "id", "key", "len", "pan", "pos", "prob", "type",
			"veljit", "vol" };
		QCOMPARE( attributeNames( element ), expectedAttributes );

		const QString first = nodeToString( element );

		Note loaded;
		loaded.restoreState( element );
		QCOMPARE( loaded.probability(), 0.5f );
		QCOMPARE( loaded.velocityJitter(), 0.25f );
		QCOMPARE( loaded.key(), 61 );
		QCOMPARE( static_cast<int>( loaded.getVolume() ), 80 );

		// load and re-save: identical bytes
		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement( "notes" );
		QDomElement element2 = loaded.saveState( doc2, parent2 );
		QCOMPARE( nodeToString( element2 ), first );

		// the copy constructor and the assignment operator (both used on the
		// playback path - MidiClip::addNote, NotePlayHandle) carry the values
		Note copy( original );
		QCOMPARE( copy.probability(), 0.5f );
		QCOMPARE( copy.velocityJitter(), 0.25f );
		Note assigned;
		assigned = original;
		QCOMPARE( assigned.probability(), 0.5f );
		QCOMPARE( assigned.velocityJitter(), 0.25f );

		// and the setters clamp into the documented range
		Note clamped;
		clamped.setProbability( 7.f );
		QCOMPARE( clamped.probability(), 1.f );
		clamped.setProbability( -3.f );
		QCOMPARE( clamped.probability(), 0.f );
		clamped.setVelocityJitter( 2.f );
		QCOMPARE( clamped.velocityJitter(), 1.f );
		clamped.setVelocityJitter( -1.f );
		QCOMPARE( clamped.velocityJitter(), 0.f );
	}

	//! (c) A project that predates this feature loads with the documented
	//! defaults and re-saves the same content, growing no new attribute.
	void legacyProjectLoadsUnchanged()
	{
		DataFile fresh( DataFile::Type::SongProject );
		const QString version = fresh.documentElement().attribute( "version" );
		QVERIFY( !version.isEmpty() );

		DataFile old( legacyProject( version ).toUtf8() );
		QVERIFY( old.type() == DataFile::Type::SongProject );
		QVERIFY( !old.content().isNull() );
		QVERIFY( !old.head().isNull() );

		const QString first = fileToString( old );

		QVERIFY( !first.contains( QStringLiteral( "prob" ) ) );
		QVERIFY( !first.contains( QStringLiteral( "veljit" ) ) );
		QVERIFY( !first.contains( QStringLiteral( "midiseed" ) ) );

		// no seed in the header either
		QCOMPARE( NoteRandom::readProjectSeed( old.head() ), 0u );

		// both notes survived with exactly the upstream attribute set
		QDomDocument parsed;
		QVERIFY( parsed.setContent( first.toUtf8() ) );
		const QDomNodeList notes = parsed.elementsByTagName( QStringLiteral( "note" ) );
		QCOMPARE( notes.length(), 2 );
		const QStringList upstreamAttributes{ "key", "len", "pan", "pos", "type", "vol" };
		for ( int i = 0; i < notes.length(); ++i )
		{
			QCOMPARE( attributeNames( notes.item( i ).toElement() ), upstreamAttributes );
		}

		// the same content re-saved, compared through the canonical form
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( savedDocumentDigest( fileToString( roundTrip ) ), savedDocumentDigest( first ) );
	}

	//! (d) A project with probability, jitter and a seed set survives a full
	//! file round trip with every value intact.
	void projectWithMidiDepthRoundTrips()
	{
		DataFile fresh( DataFile::Type::SongProject );
		const QString version = fresh.documentElement().attribute( "version" );
		QVERIFY( !version.isEmpty() );

		QString project = legacyProject( version );
		project.replace( QStringLiteral( "<head/>" ),
			QStringLiteral( "<head midiseed=\"424242\"/>" ) );
		project.replace(
			QStringLiteral( "<note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" type=\"0\"/>" ),
			QStringLiteral( "<note key=\"60\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"0\" prob=\"0.5\" type=\"0\"/>" ) );
		project.replace(
			QStringLiteral( "<note key=\"67\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"384\" type=\"0\"/>" ),
			QStringLiteral( "<note key=\"67\" vol=\"100\" pan=\"0\" len=\"384\" pos=\"384\" prob=\"0.25\" veljit=\"0.5\" type=\"0\"/>" ) );

		DataFile loaded( project.toUtf8() );
		QVERIFY( loaded.type() == DataFile::Type::SongProject );
		const QString first = fileToString( loaded );

		// the values are in the saved file (attribute order is not preserved by
		// the project writer, so each value is asserted on its own)
		QVERIFY2( first.contains( QStringLiteral( "midiseed=\"424242\"" ) ), qPrintable( first ) );
		QVERIFY2( first.contains( QStringLiteral( "prob=\"0.5\"" ) ), qPrintable( first ) );
		QVERIFY2( first.contains( QStringLiteral( "prob=\"0.25\"" ) ), qPrintable( first ) );
		QVERIFY2( first.contains( QStringLiteral( "veljit=\"0.5\"" ) ), qPrintable( first ) );

		// the seed reads back out of the header
		QCOMPARE( NoteRandom::readProjectSeed( loaded.head() ), 424242u );

		// the note elements carry exactly the expected attributes
		QDomDocument parsed;
		QVERIFY( parsed.setContent( first.toUtf8() ) );
		const QDomNodeList notes = parsed.elementsByTagName( QStringLiteral( "note" ) );
		QCOMPARE( notes.length(), 2 );
		const QStringList firstNoteAttributes{ "key", "len", "pan", "pos", "prob", "type", "vol" };
		const QStringList secondNoteAttributes{ "key", "len", "pan", "pos", "prob", "type", "veljit", "vol" };
		QCOMPARE( attributeNames( notes.item( 0 ).toElement() ), firstNoteAttributes );
		QCOMPARE( attributeNames( notes.item( 1 ).toElement() ), secondNoteAttributes );

		// and those note elements restore through the note serialiser exactly
		Note firstNote;
		firstNote.restoreState( notes.item( 0 ).toElement() );
		QCOMPARE( firstNote.probability(), 0.5f );
		QCOMPARE( firstNote.velocityJitter(), 0.f );
		Note secondNote;
		secondNote.restoreState( notes.item( 1 ).toElement() );
		QCOMPARE( secondNote.probability(), 0.25f );
		QCOMPARE( secondNote.velocityJitter(), 0.5f );

		// save -> reload -> save carries the same content, compared through the
		// canonical form rather than the writer's per-process attribute order
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( savedDocumentDigest( fileToString( roundTrip ) ), savedDocumentDigest( first ) );
	}
//! (e) Luna cluster 2344 / 3326: a value the file cannot express must not
	//! become the note's probability or jitter. `toFloat()` without checking
	//! success turns "bad" into 0 (the note is then silent for the rest of the
	//! document's life), and std::clamp leaves a NaN NaN because every
	//! comparison against it is false - so a non-finite value would travel
	//! straight into the playback roll (NoteRandom).
	void nonFiniteAndMalformedValuesFallBackToTheDocumentedDefaults()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement( QStringLiteral( "notes" ) );

		// NaN in the file: every clamp comparison is false, so the clamp hands
		// the NaN straight through unless the value is rejected first.
		const QDomElement nanElement = noteElementWith( doc, root, "nan", "nan" );
		Note fromNan;
		fromNan.restoreState( nanElement );
		QVERIFY2( std::isfinite( fromNan.probability() ),
			"prob=\"nan\" was loaded as a non-finite probability" );
		QCOMPARE( fromNan.probability(), 1.f );
		QVERIFY( std::isfinite( fromNan.velocityJitter() ) );
		QCOMPARE( fromNan.velocityJitter(), 0.f );

		// Unparsable text: toFloat() reports failure and yields 0, which used
		// to be stored as the note's real probability - a silent note.
		const QDomElement badElement = noteElementWith( doc, root, "bad", "worse" );
		Note fromBad;
		fromBad.restoreState( badElement );
		QCOMPARE( fromBad.probability(), 1.f );
		QCOMPARE( fromBad.velocityJitter(), 0.f );

		// Out-of-range but FINITE text still clamps - the existing rule is
		// untouched by this one.
		const QDomElement hugeElement = noteElementWith( doc, root, "7", "9" );
		Note fromHuge;
		fromHuge.restoreState( hugeElement );
		QCOMPARE( fromHuge.probability(), 1.f );
		QCOMPARE( fromHuge.velocityJitter(), 1.f );

		// The setters take the same route (control surface / a direct caller),
		// so they reject a non-finite argument the same way.
		Note direct;
		direct.setProbability( std::numeric_limits<float>::quiet_NaN() );
		QCOMPARE( direct.probability(), 1.f );
		direct.setVelocityJitter( std::numeric_limits<float>::quiet_NaN() );
		QCOMPARE( direct.velocityJitter(), 0.f );
		direct.setProbability( std::numeric_limits<float>::infinity() );
		QCOMPARE( direct.probability(), 1.f );
		direct.setVelocityJitter( -std::numeric_limits<float>::infinity() );
		QCOMPARE( direct.velocityJitter(), 0.f );

		// ...and a finite value on the same object still lands, so the guard
		// is not swallowing everything.
		direct.setProbability( 0.25f );
		QCOMPARE( direct.probability(), 0.25f );
		direct.setVelocityJitter( 0.75f );
		QCOMPARE( direct.velocityJitter(), 0.75f );
	}

	//! (f) Luna cluster 3325: the pre-quantise position and length are optional
	//! attributes that note.restore_timing puts back, and Note::loadSettings read
	//! them with toInt() without asking whether the conversion succeeded. QString
	//! reports that failure by returning 0, so <note qpos="bad" qlen="bad"> loaded
	//! as "this note was moved to tick 0 and is one tick long" - and restore_timing
	//! faithfully moved it there. Both halves are required together, so a valid
	//! qpos with an unreadable qlen is not usable timing either.
	void unparseableUnquantisedTimingIsNotTreatedAsTickZero()
	{
		QDomDocument doc;
		QDomElement root = doc.createElement( QStringLiteral( "notes" ) );

		for ( const auto& pair : { std::make_pair( "bad", "bad" ), std::make_pair( "96", "" ),
				 std::make_pair( "", "96" ), std::make_pair( "-1", "8" ),
				 std::make_pair( "8", "0" ) } )
		{
			QDomElement element = noteElementWith( doc, root, "1", "0" );
			element.setAttribute( QStringLiteral( "pos" ), QStringLiteral( "24" ) );
			element.setAttribute( QStringLiteral( "len" ), QStringLiteral( "48" ) );
			element.setAttribute( QStringLiteral( "qpos" ), QString::fromLatin1( pair.first ) );
			element.setAttribute( QStringLiteral( "qlen" ), QString::fromLatin1( pair.second ) );

			Note loaded;
			loaded.restoreState( element );
			// Nothing to restore, so nothing claims the note carries a timing.
			QVERIFY2( !loaded.hasUnquantisedTiming(), pair.first );
			QVERIFY( !loaded.restoreTiming() );
			QCOMPARE( static_cast<int>( loaded.pos().getTicks() ), 24 );
			QCOMPARE( static_cast<int>( loaded.length().getTicks() ), 48 );
		}

		// ...and the valid pair is still restored exactly, so the guard rejects
		// only what the file could not express.
		Note timed( TimePos( 48 ), TimePos( 24 ), 60 );
		timed.rememberTiming();
		timed.setPos( TimePos( 0 ) );
		Note timedLoaded;
		timedLoaded.restoreState( timed.saveState( doc, root ) );
		QVERIFY( timedLoaded.hasUnquantisedTiming() );
		QVERIFY( timedLoaded.restoreTiming() );
		QCOMPARE( static_cast<int>( timedLoaded.pos().getTicks() ), 24 );
		QCOMPARE( static_cast<int>( timedLoaded.length().getTicks() ), 48 );
	}
};

QTEST_GUILESS_MAIN(MidiProbabilityPersistenceTest)
#include "MidiProbabilityPersistenceTest.moc"
