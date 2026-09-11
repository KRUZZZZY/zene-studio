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
//! set, so an old project loads with the old behaviour and re-saves
//! byte-identically.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QStringList>
#include <QTextStream>

#include "DataFile.h"
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

		const QStringList upstreamAttributes{ "key", "len", "pan", "pos", "type", "vol" };
		QCOMPARE( attributeNames( element ), upstreamAttributes );

		// loading such an element gives the old behaviour back
		Note loaded;
		loaded.restoreState( element );
		QCOMPARE( loaded.probability(), 1.f );
		QCOMPARE( loaded.velocityJitter(), 0.f );
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

		const QStringList expectedAttributes{ "key", "len", "pan", "pos", "prob", "type", "veljit", "vol" };
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
	//! defaults and re-saves byte-identically, growing no new attribute.
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

		// byte-identical re-save
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( fileToString( roundTrip ), first );
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

		// save -> reload -> save is byte-identical
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( fileToString( roundTrip ), first );
	}
};

QTEST_GUILESS_MAIN(MidiProbabilityPersistenceTest)
#include "MidiProbabilityPersistenceTest.moc"
