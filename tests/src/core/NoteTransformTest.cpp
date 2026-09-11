/*
 * NoteTransformTest.cpp - search/transform operations over note events
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

//! Every assertion here is on the exact resulting note set - the keys,
//! positions or velocities of the notes the operation touched. "Something
//! changed" is not tested anywhere, and a transform that silently did nothing
//! fails the count assertions.

#include <QtTest>

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "Note.h"
#include "NoteTransform.h"

using namespace lmms;

namespace
{

//! Owns the notes the tests manipulate and keeps the view in sync.
class NoteTable
{
public:
	Note* add( int key, int pos, int length, int volume )
	{
		m_owned.emplace_back( new Note( TimePos( length ), TimePos( pos ), key,
			static_cast<volume_t>( volume ), DefaultPanning ) );
		m_view.push_back( m_owned.back().get() );
		return m_owned.back().get();
	}

	const NoteVector& vector() const { return m_view; }
	NoteVector& vector() { return m_view; }

private:
	std::vector<std::unique_ptr<Note>> m_owned;
	NoteVector m_view;
};

QString dump( const std::vector<int>& values )
{
	QStringList parts;
	for( int v : values ) { parts << QString::number( v ); }
	return parts.join( ',' );
}

std::vector<int> keysOf( const NoteVector& notes )
{
	std::vector<int> out;
	for( const Note* n : notes ) { out.push_back( n->key() ); }
	return out;
}

std::vector<int> positionsOf( const NoteVector& notes )
{
	std::vector<int> out;
	for( const Note* n : notes ) { out.push_back( n->pos() ); }
	return out;
}

std::vector<int> volumesOf( const NoteVector& notes )
{
	std::vector<int> out;
	for( const Note* n : notes ) { out.push_back( n->getVolume() ); }
	return out;
}

//! The pitch classes a filter selects, identified by key.
std::vector<int> selectedKeys( const NoteVector& notes, const NoteTransform::Filter& f )
{
	return keysOf( NoteTransform::select( notes, f ) );
}

const std::vector<int> CMajor{ 0, 2, 4, 5, 7, 9, 11 };

} // namespace


class NoteTransformTest : public QObject
{
	Q_OBJECT
private slots:
	//! The fixture the rest of the file works on, so a later failure cannot be
	//! blamed on a mis-built table.
	void fixtureIsWhatTheOtherSlotsAssume()
	{
		NoteTable t;
		t.add( 60, 0, 96, 100 );
		t.add( 61, 48, 96, 80 );
		t.add( 67, 96, 96, 120 );
		t.add( 72, 192, 96, 60 );

		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "60,61,67,72" ) );
		QCOMPARE( dump( positionsOf( t.vector() ) ), QString( "0,48,96,192" ) );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "100,80,120,60" ) );

		// the default filter matches everything, in order
		NoteTransform::Filter all;
		QCOMPARE( dump( selectedKeys( t.vector(), all ) ), QString( "60,61,67,72" ) );
	}

	void selectByKeyVelocityAndPosition()
	{
		NoteTable t;
		t.add( 60, 0, 96, 100 );
		t.add( 61, 48, 96, 80 );
		t.add( 67, 96, 96, 120 );
		t.add( 72, 192, 96, 60 );

		NoteTransform::Filter keyRange;
		keyRange.useKeyRange = true;
		keyRange.minKey = 60;
		keyRange.maxKey = 61;
		QCOMPARE( dump( selectedKeys( t.vector(), keyRange ) ), QString( "60,61" ) );

		NoteTransform::Filter velocityRange;
		velocityRange.useVelocityRange = true;
		velocityRange.minVelocity = 90;
		velocityRange.maxVelocity = 200;
		QCOMPARE( dump( selectedKeys( t.vector(), velocityRange ) ), QString( "60,67" ) );

		NoteTransform::Filter positionRange;
		positionRange.usePositionRange = true;
		positionRange.minPos = 48;
		positionRange.maxPos = 96;
		QCOMPARE( dump( selectedKeys( t.vector(), positionRange ) ), QString( "61,67" ) );

		// clauses combine with AND
		NoteTransform::Filter both = keyRange;
		both.useVelocityRange = true;
		both.minVelocity = 90;
		both.maxVelocity = 200;
		QCOMPARE( dump( selectedKeys( t.vector(), both ) ), QString( "60" ) );

		// a single note can be named exactly
		NoteTransform::Filter exact;
		exact.usePositionRange = true;
		exact.minPos = exact.maxPos = 96;
		exact.useKeyRange = true;
		exact.minKey = exact.maxKey = 67;
		QCOMPARE( dump( selectedKeys( t.vector(), exact ) ), QString( "67" ) );

		// range bounds are inclusive
		NoteTransform::Filter inclusive;
		inclusive.useVelocityRange = true;
		inclusive.minVelocity = 60;
		inclusive.maxVelocity = 80;
		QCOMPARE( dump( selectedKeys( t.vector(), inclusive ) ), QString( "61,72" ) );

		// an empty selection stays empty
		NoteTransform::Filter none;
		none.useKeyRange = true;
		none.minKey = 100;
		none.maxKey = 110;
		QCOMPARE( dump( selectedKeys( t.vector(), none ) ), QString() );
	}

	void selectByScaleMembership()
	{
		NoteTable t;
		t.add( 60, 0, 96, 100 );	// C  - in C major
		t.add( 61, 48, 96, 80 );	// C# - out
		t.add( 67, 96, 96, 120 );	// G  - in
		t.add( 72, 192, 96, 60 );	// C  - in

		NoteTransform::Filter inScale;
		inScale.scaleMatch = NoteTransform::ScaleMatch::InScale;
		inScale.scaleDegrees = CMajor;
		QCOMPARE( dump( selectedKeys( t.vector(), inScale ) ), QString( "60,67,72" ) );

		NoteTransform::Filter outOfScale = inScale;
		outOfScale.scaleMatch = NoteTransform::ScaleMatch::OutOfScale;
		QCOMPARE( dump( selectedKeys( t.vector(), outOfScale ) ), QString( "61" ) );

		// the same pitch class in another octave is the same member
		NoteTable octaves;
		octaves.add( 60, 0, 96, 100 );
		octaves.add( 84, 24, 96, 100 );
		octaves.add( 49, 48, 96, 100 );	// C# below middle C
		QCOMPARE( dump( selectedKeys( octaves.vector(), inScale ) ), QString( "60,84" ) );

		// degrees are taken modulo 12, in either direction
		std::vector<int> wrapped{ 12, -1, 0, 2 };
		QCOMPARE( dump( NoteTransform::pitchClasses( wrapped ) ), QString( "0,2,11" ) );

		NoteTransform::Filter chromatic;
		chromatic.scaleMatch = NoteTransform::ScaleMatch::InScale;
		chromatic.scaleDegrees = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
		QCOMPARE( dump( selectedKeys( t.vector(), chromatic ) ), QString( "60,61,67,72" ) );

		// an empty scale matches nothing, in both directions
		NoteTransform::Filter empty = inScale;
		empty.scaleDegrees.clear();
		QCOMPARE( dump( selectedKeys( t.vector(), empty ) ), QString() );
		empty.scaleMatch = NoteTransform::ScaleMatch::OutOfScale;
		QCOMPARE( dump( selectedKeys( t.vector(), empty ) ), QString() );
	}

	void invertSelectsTheComplement()
	{
		NoteTable t;
		t.add( 60, 0, 96, 100 );
		t.add( 61, 48, 96, 80 );
		t.add( 67, 96, 96, 120 );
		t.add( 72, 192, 96, 60 );

		NoteTransform::Filter keyRange;
		keyRange.useKeyRange = true;
		keyRange.minKey = 60;
		keyRange.maxKey = 61;
		keyRange.invert = true;
		QCOMPARE( dump( selectedKeys( t.vector(), keyRange ) ), QString( "67,72" ) );

		// an inverted empty filter selects nothing
		NoteTransform::Filter all;
		all.invert = true;
		QCOMPARE( dump( selectedKeys( t.vector(), all ) ), QString() );

		// and an inverted scale filter is the "out of key" selection
		NoteTransform::Filter inScale;
		inScale.scaleMatch = NoteTransform::ScaleMatch::InScale;
		inScale.scaleDegrees = CMajor;
		inScale.invert = true;
		QCOMPARE( dump( selectedKeys( t.vector(), inScale ) ), QString( "61" ) );
	}

	void transposeClampsToTheMidiRange()
	{
		NoteTable t;
		Note* a = t.add( 60, 0, 96, 100 );
		Note* b = t.add( 61, 48, 96, 80 );
		Note* c = t.add( 67, 96, 96, 120 );
		t.add( 72, 192, 96, 60 );

		QCOMPARE( NoteTransform::transpose( { a, c }, 2 ), 2 );
		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "62,61,69,72" ) );

		QCOMPARE( NoteTransform::transpose( t.vector(), -12 ), 4 );
		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "50,49,57,60" ) );

		QCOMPARE( NoteTransform::transpose( t.vector(), 0 ), 0 );
		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "50,49,57,60" ) );

		// the rails clamp, and a note that cannot move is not counted
		Note* low = t.add( 0, 288, 96, 100 );
		Note* high = t.add( 127, 336, 96, 100 );
		QCOMPARE( NoteTransform::transpose( { low }, -5 ), 0 );
		QCOMPARE( low->key(), 0 );
		QCOMPARE( NoteTransform::transpose( { high }, 5 ), 0 );
		QCOMPARE( high->key(), 127 );

		QCOMPARE( NoteTransform::transpose( { low, high }, 1 ), 1 );
		QCOMPARE( dump( std::vector<int>{ low->key(), high->key() } ), QString( "1,127" ) );

		QCOMPARE( NoteTransform::transpose( { b }, -1000 ), 1 );
		QCOMPARE( b->key(), 0 );
	}

	void velocityTransforms()
	{
		NoteTable t;
		Note* a = t.add( 60, 0, 96, 100 );
		Note* b = t.add( 61, 48, 96, 80 );
		Note* c = t.add( 67, 96, 96, 120 );
		t.add( 72, 192, 96, 60 );

		QCOMPARE( NoteTransform::offsetVelocity( { b, c }, -100 ), 2 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "100,0,20,60" ) );

		QCOMPARE( NoteTransform::offsetVelocity( t.vector(), 50 ), 4 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "150,50,70,110" ) );

		// everything clamps at the rail
		QCOMPARE( NoteTransform::offsetVelocity( t.vector(), 500 ), 4 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "200,200,200,200" ) );

		// and at the rail there is nothing more to gain, so nothing changes
		QCOMPARE( NoteTransform::offsetVelocity( t.vector(), 1 ), 0 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "200,200,200,200" ) );

		QCOMPARE( NoteTransform::scaleVelocity( t.vector(), 0.5f ), 4 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "100,100,100,100" ) );

		// factor 1 is exactly a no-op
		QCOMPARE( NoteTransform::scaleVelocity( t.vector(), 1.0f ), 0 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "100,100,100,100" ) );

		// scaling rounds to the nearest integer
		QCOMPARE( NoteTransform::scaleVelocity( { a }, 0.83f ), 1 );
		QCOMPARE( static_cast<int>( a->getVolume() ), 83 );	// 100 * 0.83 = 83

		// and clamps at the rail
		QCOMPARE( NoteTransform::scaleVelocity( { a }, 10.f ), 1 );
		QCOMPARE( static_cast<int>( a->getVolume() ), 200 );

		// a factor that rounds back to the same value counts as unchanged
		QCOMPARE( NoteTransform::scaleVelocity( { a }, 1.0001f ), 0 );
		QCOMPARE( static_cast<int>( a->getVolume() ), 200 );
	}

	void quantizeToGrid()
	{
		NoteTable t;
		Note* a = t.add( 60, 20, 96, 100 );
		Note* b = t.add( 61, 70, 96, 80 );
		Note* c = t.add( 67, 100, 96, 120 );

		QCOMPARE( NoteTransform::quantizePositions( t.vector(), 48, NoteTransform::QuantizeMode::Nearest ), 3 );
		QCOMPARE( dump( positionsOf( t.vector() ) ), QString( "0,48,96" ) );

		// already on the grid: nothing moves
		QCOMPARE( NoteTransform::quantizePositions( t.vector(), 48, NoteTransform::QuantizeMode::Nearest ), 0 );
		QCOMPARE( dump( positionsOf( t.vector() ) ), QString( "0,48,96" ) );

		a->setPos( TimePos( 20 ) );
		b->setPos( TimePos( 70 ) );
		c->setPos( TimePos( 100 ) );
		QCOMPARE( NoteTransform::quantizePositions( t.vector(), 48, NoteTransform::QuantizeMode::Floor ), 3 );
		QCOMPARE( dump( positionsOf( t.vector() ) ), QString( "0,48,96" ) );

		a->setPos( TimePos( 20 ) );
		b->setPos( TimePos( 70 ) );
		c->setPos( TimePos( 100 ) );
		QCOMPARE( NoteTransform::quantizePositions( t.vector(), 48, NoteTransform::QuantizeMode::Ceil ), 3 );
		QCOMPARE( dump( positionsOf( t.vector() ) ), QString( "48,96,144" ) );

		// a position exactly on the grid is not moved by Ceil either
		QCOMPARE( NoteTransform::quantizePositions( { b }, 48, NoteTransform::QuantizeMode::Ceil ), 0 );
		QCOMPARE( b->pos(), 96 );

		// a non-positive grid is a no-op
		QCOMPARE( NoteTransform::quantizePositions( t.vector(), 0, NoteTransform::QuantizeMode::Nearest ), 0 );
		QCOMPARE( NoteTransform::quantizePositions( t.vector(), -8, NoteTransform::QuantizeMode::Nearest ), 0 );
		QCOMPARE( dump( positionsOf( t.vector() ) ), QString( "48,96,144" ) );
	}

	void snapToScaleMovesOnlyOutOfKeyNotes()
	{
		NoteTable t;
		Note* cSharp = t.add( 61, 0, 96, 100 );	// C# -> C
		Note* fSharp = t.add( 66, 48, 96, 80 );	// F# -> F
		t.add( 67, 96, 96, 120 );		// G  -> untouched
		Note* aSharp = t.add( 70, 192, 96, 60 );	// A# -> A

		QCOMPARE( NoteTransform::snapToScale( t.vector(), CMajor ), 3 );
		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "60,65,67,69" ) );

		// already in the scale: nothing moves
		QCOMPARE( NoteTransform::snapToScale( t.vector(), CMajor ), 0 );
		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "60,65,67,69" ) );

		// D# sits one below E and one above D, both in C major: the tie
		// resolves downward
		Note* dSharp = t.add( 63, 240, 96, 100 );
		QCOMPARE( NoteTransform::snapToScale( { dSharp }, CMajor ), 1 );
		QCOMPARE( dSharp->key(), 62 );

		// when only the upper neighbour is in the scale, the note moves up:
		// F# is five semitones below B, and nowhere near the C below it
		Note* fLow = t.add( 6, 288, 96, 100 );
		QCOMPARE( NoteTransform::snapToScale( { fLow }, { 0, 11 } ), 1 );
		QCOMPARE( fLow->key(), 11 );

		// an empty scale is a no-op
		QCOMPARE( NoteTransform::snapToScale( { dSharp }, {} ), 0 );
		QCOMPARE( dSharp->key(), 62 );

		// a note that has nowhere to go keeps its key and is not counted
		Note* railed = t.add( 0, 336, 96, 100 );
		QCOMPARE( NoteTransform::snapToScale( { railed }, CMajor ), 0 );
		QCOMPARE( railed->key(), 0 );

		// C# is still out of C major, so the selection has not been "fixed"
		// into a no-op
		QCOMPARE( cSharp->key(), 60 );
		QCOMPARE( fSharp->key(), 65 );
		QCOMPARE( aSharp->key(), 69 );
	}

	void sortByPositionRestoresClipOrder()
	{
		NoteTable t;
		t.add( 60, 96, 96, 100 );
		t.add( 61, 0, 96, 80 );
		t.add( 64, 0, 96, 120 );

		// deliberately shuffled, as a transform can leave it
		NoteVector shuffled = t.vector();
		std::swap( shuffled[0], shuffled[2] );
		NoteTransform::sortByPosition( shuffled );

		// position ascending, then key descending - the order Note::lessThan
		// defines and MidiClip expects
		QCOMPARE( dump( keysOf( shuffled ) ), QString( "64,61,60" ) );
		QCOMPARE( dump( positionsOf( shuffled ) ), QString( "0,0,96" ) );
	}

	void selectThenTransformIsExact()
	{
		NoteTable t;
		t.add( 60, 0, 96, 100 );	// in C
		t.add( 61, 48, 96, 80 );	// out of C
		t.add( 67, 96, 96, 120 );	// in C
		t.add( 72, 192, 96, 60 );	// in C

		// the pipeline a piano-roll command would run: select the out-of-key
		// notes, then snap them in
		NoteTransform::Filter outOfKey;
		outOfKey.scaleMatch = NoteTransform::ScaleMatch::OutOfScale;
		outOfKey.scaleDegrees = CMajor;
		const NoteVector selected = NoteTransform::select( t.vector(), outOfKey );
		QCOMPARE( dump( keysOf( selected ) ), QString( "61" ) );

		QCOMPARE( NoteTransform::snapToScale( selected, CMajor ), 1 );
		QCOMPARE( dump( keysOf( t.vector() ) ), QString( "60,60,67,72" ) );

		// and a second pass finds nothing left out of key
		QCOMPARE( NoteTransform::select( t.vector(), outOfKey ).size(), std::size_t( 0 ) );

		// the same pipeline as "select the loud notes and quieten them"
		NoteTransform::Filter loud;
		loud.useVelocityRange = true;
		loud.minVelocity = 100;
		loud.maxVelocity = 200;
		const NoteVector loudNotes = NoteTransform::select( t.vector(), loud );
		QCOMPARE( dump( volumesOf( loudNotes ) ), QString( "100,120" ) );
		QCOMPARE( NoteTransform::offsetVelocity( loudNotes, -20 ), 2 );
		QCOMPARE( dump( volumesOf( t.vector() ) ), QString( "80,80,100,60" ) );
	}
};

QTEST_GUILESS_MAIN(NoteTransformTest)
#include "NoteTransformTest.moc"
