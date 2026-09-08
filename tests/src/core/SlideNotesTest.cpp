/*
 * SlideNotesTest.cpp
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "DataFile.h"
#include "Note.h"
#include "NotePlayHandle.h"

#include <QDomDocument>
#include <QObject>
#include <QStringList>
#include <QTextStream>
#include <QtTest>

#include <cmath>

namespace
{

QString fileToString( lmms::DataFile& df )
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


//! Mirrors the default key mapping of NotePlayHandle::updateFrequency():
//! DefaultBaseFreq * exp2( (key + slideOffset - baseNote) / 12 )
float frequencyForKey( int key, float slideOffset )
{
	using namespace lmms;
	return DefaultBaseFreq * std::exp2( ( key + slideOffset - DefaultKey ) / 12.f );
}


bool closeTo( float actual, float expected )
{
	return std::fabs( actual - expected ) < 0.01f;
}

} // namespace


class SlideNotesTest : public QObject
{
	Q_OBJECT
private slots:
	//! (a) A slide note survives save -> load -> save identically, and a
	//! regular note never grows a "slide" attribute.
	void slideNoteRoundTrip()
	{
		using namespace lmms;

		QVERIFY( !Note().slide() );

		Note original( TimePos( 384 ), TimePos( 192 ), 61 );
		original.setVolume( 80 );
		original.setPanning( -25 );
		original.setSlide( true );

		QDomDocument doc;
		QDomElement parent = doc.createElement( "notes" );
		QDomElement element = original.saveState( doc, parent );
		QVERIFY( !element.isNull() );

		QCOMPARE( element.attribute( "slide" ), QString( "1" ) );
		QCOMPARE( element.attribute( "key" ).toInt(), 61 );
		QCOMPARE( element.attribute( "len" ).toInt(), 384 );
		QCOMPARE( element.attribute( "pos" ).toInt(), 192 );
		QCOMPARE( element.attribute( "vol" ).toInt(), 80 );
		QCOMPARE( element.attribute( "pan" ).toInt(), -25 );

		const QString first = nodeToString( element );

		// load and re-save: identical bytes
		Note loaded;
		loaded.restoreState( element );
		QCOMPARE( loaded.slide(), true );
		QCOMPARE( loaded.key(), 61 );
		QVERIFY( loaded.length() == original.length() );
		QVERIFY( loaded.pos() == original.pos() );
		QCOMPARE( static_cast<int>( loaded.getVolume() ), 80 );
		QCOMPARE( static_cast<int>( loaded.getPanning() ), -25 );

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement( "notes" );
		QDomElement element2 = loaded.saveState( doc2, parent2 );
		QCOMPARE( nodeToString( element2 ), first );

		// a regular note must not gain the optional attribute
		Note plain( TimePos( 384 ), TimePos( 0 ), 60 );
		QVERIFY( !plain.slide() );
		QDomDocument doc3;
		QDomElement parent3 = doc3.createElement( "notes" );
		QDomElement element3 = plain.saveState( doc3, parent3 );
		QVERIFY( !element3.hasAttribute( "slide" ) );
		const QStringList upstreamAttributes{ "key", "len", "pan", "pos", "type", "vol" };
		QCOMPARE( attributeNames( element3 ), upstreamAttributes );

		Note plainLoaded;
		plainLoaded.restoreState( element3 );
		QCOMPARE( plainLoaded.slide(), false );

		// the copy constructor (used by MidiClip::addNote and NotePlayHandle)
		// preserves the flag
		Note copy( original );
		QCOMPARE( copy.slide(), true );
		Note assigned;
		assigned = original;
		QCOMPARE( assigned.slide(), true );
	}

	//! (b) A project without slide attributes loads unchanged and re-saves
	//! byte-identically; no note grows a "slide" attribute.
	void projectWithoutSlidesRoundTripsUnchanged()
	{
		using namespace lmms;

		DataFile fresh( DataFile::Type::SongProject );
		const QString version = fresh.documentElement().attribute( "version" );
		QVERIFY( !version.isEmpty() );

		const QString project = QString(
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

		DataFile old( project.toUtf8() );
		QVERIFY( old.type() == DataFile::Type::SongProject );
		QVERIFY( !old.content().isNull() );
		QVERIFY( !old.head().isNull() );

		const QString first = fileToString( old );

		// no slide attribute anywhere in the re-saved project
		QVERIFY( !first.contains( QStringLiteral( "slide" ) ) );

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
		QVERIFY( first.contains( QStringLiteral( "key=\"60\"" ) ) );
		QVERIFY( first.contains( QStringLiteral( "key=\"67\"" ) ) );

		// byte-identical re-save: loading what we just wrote and writing
		// again must not change a single byte
		DataFile roundTrip( first.toUtf8() );
		QCOMPARE( fileToString( roundTrip ), first );
	}

	//! (c) The playback interpolation produces the expected pitch trajectory:
	//! linear in semitones (exponential in Hz) over the slide duration.
	void slidePitchTrajectory()
	{
		using namespace lmms;

		const int fromKey = DefaultKey;			// A4, 440 Hz base
		const int toKey = DefaultKey + 12;		// A5, one octave up

		// no glide without a source key: offset is zero everywhere
		for ( float p = 0.f; p <= 1.f; p += 0.25f )
		{
			QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( toKey, toKey, p ), 0.f ) );
		}

		// semitone offsets along the glide, clamped to [0, 1]
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, -0.5f ), -12.f ) );
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, 0.f ), -12.f ) );
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, 0.25f ), -9.f ) );
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, 0.5f ), -6.f ) );
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, 0.75f ), -3.f ) );
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, 1.f ), 0.f ) );
		QVERIFY( closeTo( NotePlayHandle::slidePitchOffset( fromKey, toKey, 1.5f ), 0.f ) );

		// the frequency the audio thread computes at those offsets: an exact
		// octave glide A4 -> C5 -> D#5 -> F#5 -> A5
		const float expectedHz[] = { 440.f, 523.2511f, 622.2540f, 739.9888f, 880.f };
		float previous = 0.f;
		for ( int i = 0; i < 5; ++i )
		{
			const float progress = i / 4.f;
			const float offset = NotePlayHandle::slidePitchOffset( fromKey, toKey, progress );
			const float hz = frequencyForKey( toKey, offset );
			QVERIFY( closeTo( hz, expectedHz[i] ) );
			QVERIFY( hz > previous );
			previous = hz;
		}

		// a downward slide glides from the source down to the note's own key
		const float downAtStart = NotePlayHandle::slidePitchOffset( toKey, fromKey, 0.f );
		const float downAtEnd = NotePlayHandle::slidePitchOffset( toKey, fromKey, 1.f );
		QVERIFY( closeTo( downAtStart, 12.f ) );
		QVERIFY( closeTo( downAtEnd, 0.f ) );
		QVERIFY( frequencyForKey( fromKey, downAtStart ) > frequencyForKey( fromKey, downAtEnd ) );
	}
};

QTEST_GUILESS_MAIN(SlideNotesTest)
#include "SlideNotesTest.moc"
