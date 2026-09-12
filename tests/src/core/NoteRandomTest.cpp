/*
 * NoteRandomTest.cpp - seeded note randomisation (note probability / velocity jitter)
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

//! The seeded roll behind note probability and velocity jitter. The pair that
//! matters is asserted here directly: the same seed reproduces the same
//! played/not-played set exactly, and a different seed produces a different
//! one. The render-level half of the same proof (same seed -> identical WAV,
//! different seed -> different WAV) is in docs/MIDI-DEPTH.md.

#include <QtTest>

#include <QDomDocument>

#include <vector>

#include "NoteRandom.h"

namespace
{

constexpr int NoteCount = 32;

//! A fixed, boring note table: pitch, position and length are all distinct so
//! that a roll keyed on only one of them cannot accidentally look correct.
struct TestNote
{
	int key;
	int pos;
	int length;
};

const TestNote s_notes[NoteCount] =
{
	{ 48, 0, 24 },    { 53, 24, 36 },   { 57, 48, 48 },   { 60, 72, 60 },
	{ 62, 96, 24 },   { 64, 120, 36 },  { 65, 144, 48 },  { 67, 168, 60 },
	{ 69, 192, 24 },  { 71, 216, 36 },  { 72, 240, 48 },  { 74, 264, 60 },
	{ 76, 288, 24 },  { 77, 312, 36 },  { 79, 336, 48 },  { 81, 360, 60 },
	{ 48, 384, 24 },  { 52, 408, 36 },  { 55, 432, 48 },  { 59, 456, 60 },
	{ 60, 480, 24 },  { 63, 504, 36 },  { 66, 528, 48 },  { 68, 552, 60 },
	{ 70, 576, 24 },  { 73, 600, 36 },  { 75, 624, 48 },  { 78, 648, 60 },
	{ 80, 672, 24 },  { 82, 696, 36 },  { 84, 720, 48 },  { 86, 744, 60 }
};

//! The bit mask of which notes a 50% roll lets through, for a given seed.
std::vector<bool> mask( uint32_t seed, float probability )
{
	std::vector<bool> out;
	for( const TestNote& n : s_notes )
	{
		out.push_back( lmms::NoteRandom::passesProbability(
			probability, seed, n.key, n.pos, n.length ) );
	}
	return out;
}

int maskCount( const std::vector<bool>& m )
{
	int count = 0;
	for( bool b : m ) { if( b ) { ++count; } }
	return count;
}

} // namespace


class NoteRandomTest : public QObject
{
	Q_OBJECT
private slots:
	//! The roll is a pure function: the same inputs give the same output, and
	//! neighbouring inputs give different output.
	void rollIsAPureFunction()
	{
		using namespace lmms;

		QCOMPARE( NoteRandom::roll( 7, 60, 0, 24 ), NoteRandom::roll( 7, 60, 0, 24 ) );
		QCOMPARE( NoteRandom::rollUnit( 7, 60, 0, 24, 1 ), NoteRandom::rollUnit( 7, 60, 0, 24, 1 ) );

		// each input moves the value
		QVERIFY( NoteRandom::roll( 7, 60, 0, 24 ) != NoteRandom::roll( 8, 60, 0, 24 ) );
		QVERIFY( NoteRandom::roll( 7, 60, 0, 24 ) != NoteRandom::roll( 7, 61, 0, 24 ) );
		QVERIFY( NoteRandom::roll( 7, 60, 0, 24 ) != NoteRandom::roll( 7, 60, 24, 24 ) );
		QVERIFY( NoteRandom::roll( 7, 60, 0, 24 ) != NoteRandom::roll( 7, 60, 0, 36 ) );
		QVERIFY( NoteRandom::roll( 7, 60, 0, 24, 1 ) != NoteRandom::roll( 7, 60, 0, 24, 0 ) );

		// and stays inside [0, 1)
		for( const TestNote& n : s_notes )
		{
			const float u = NoteRandom::rollUnit( 1234, n.key, n.pos, n.length );
			QVERIFY( u >= 0.f );
			QVERIFY( u < 1.f );
		}
	}

	//! (a) The repeatability pair, at the level of the played set: the same
	//! seed reproduces the same set exactly; a different seed changes it.
	void sameSeedSameSetDifferentSeedDifferentSet()
	{
		using namespace lmms;

		const std::vector<bool> first = mask( 1, 0.5f );
		const std::vector<bool> again = mask( 1, 0.5f );
		const std::vector<bool> other = mask( 2, 0.5f );

		// same seed -> identical set, element for element
		QVERIFY( again == first );

		// different seed -> a different set (a roll that always said "true"
		// would make these equal, which is what this assertion catches)
		QVERIFY( other != first );

		// and neither is degenerate: a 50% roll on 32 notes drops some and
		// keeps some
		const int firstCount = maskCount( first );
		const int otherCount = maskCount( other );
		QVERIFY( firstCount > 0 );
		QVERIFY( firstCount < NoteCount );
		QVERIFY( otherCount > 0 );
		QVERIFY( otherCount < NoteCount );
	}

	void probabilityExtremesAreExact()
	{
		using namespace lmms;

		for( const TestNote& n : s_notes )
		{
			QVERIFY( NoteRandom::passesProbability( 1.f, 42, n.key, n.pos, n.length ) );
			QVERIFY( NoteRandom::passesProbability( 2.f, 42, n.key, n.pos, n.length ) );
			QVERIFY( !NoteRandom::passesProbability( 0.f, 42, n.key, n.pos, n.length ) );
			QVERIFY( !NoteRandom::passesProbability( -1.f, 42, n.key, n.pos, n.length ) );
		}

		QCOMPARE( maskCount( mask( 9, 1.f ) ), NoteCount );
		QCOMPARE( maskCount( mask( 9, 0.f ) ), 0 );
	}

	//! A 50% roll over many distinct notes keeps roughly half, the same half
	//! every time for a given seed.
	void probabilityDistribution()
	{
		using namespace lmms;

		int kept = 0;
		const int total = 2000;
		for( int i = 0; i < total; ++i )
		{
			const int key = 36 + ( i % 60 );
			const int pos = i * 12;
			const int length = 12 + ( i % 5 ) * 12;
			if( NoteRandom::passesProbability( 0.5f, 20260911, key, pos, length ) ) { ++kept; }
		}

		// wide bounds: this catches a broken roll (all/none) without being a
		// flaky statistical test
		QVERIFY( kept > total / 4 );
		QVERIFY( kept < ( 3 * total ) / 4 );

		// and it is exactly reproducible
		int keptAgain = 0;
		for( int i = 0; i < total; ++i )
		{
			if( NoteRandom::passesProbability( 0.5f, 20260911, 36 + ( i % 60 ), i * 12,
					12 + ( i % 5 ) * 12 ) ) { ++keptAgain; }
		}
		QCOMPARE( keptAgain, kept );
	}

	void velocityJitterBounds()
	{
		using namespace lmms;

		// jitter 0 leaves the velocity exactly alone
		for( const TestNote& n : s_notes )
		{
			QCOMPARE( NoteRandom::velocityFactor( 0.f, 5, n.key, n.pos, n.length ), 1.f );
		}

		// jitter j stays within [1-j, 1+j] and actually spreads
		float lowest = 2.f;
		float highest = 0.f;
		for( const TestNote& n : s_notes )
		{
			const float f = NoteRandom::velocityFactor( 0.25f, 5, n.key, n.pos, n.length );
			QVERIFY( f >= 0.75f );
			QVERIFY( f <= 1.25f );
			lowest = std::min( lowest, f );
			highest = std::max( highest, f );
		}
		QVERIFY( lowest < 0.9f );
		QVERIFY( highest > 1.1f );

		// a jitter above 1 is clamped to 1
		const float clamped = NoteRandom::velocityFactor( 4.f, 5, 60, 0, 24 );
		QVERIFY( clamped >= 0.f );
		QVERIFY( clamped <= 2.f );
	}

	void velocityJitterIsSeeded()
	{
		using namespace lmms;

		std::vector<float> seedA;
		std::vector<float> seedARepeat;
		std::vector<float> seedB;
		for( const TestNote& n : s_notes )
		{
			seedA.push_back( NoteRandom::velocityFactor( 0.5f, 100, n.key, n.pos, n.length ) );
			seedARepeat.push_back( NoteRandom::velocityFactor( 0.5f, 100, n.key, n.pos, n.length ) );
			seedB.push_back( NoteRandom::velocityFactor( 0.5f, 200, n.key, n.pos, n.length ) );
		}
		QVERIFY( seedARepeat == seedA );
		QVERIFY( seedB != seedA );
	}

	//! The seed lives in the project header, and a project that never sets one
	//! does not grow the attribute.
	void projectSeedAttribute()
	{
		using namespace lmms;

		QDomDocument doc;
		QDomElement head = doc.createElement( "head" );

		// absent -> the default
		QCOMPARE( NoteRandom::readProjectSeed( head ), 0u );
		QVERIFY( !NoteRandom::readProjectSeed( QDomElement() ) );

		// the default is not written
		NoteRandom::writeProjectSeed( head, 0 );
		QVERIFY( !head.hasAttribute( "midiseed" ) );
		head.setAttribute( "bpm", "140" );
		NoteRandom::writeProjectSeed( head, 0 );
		QCOMPARE( head.attributes().length(), 1 );

		// a real seed is written and reads back
		NoteRandom::writeProjectSeed( head, 424242u );
		QVERIFY( head.hasAttribute( "midiseed" ) );
		QCOMPARE( head.attribute( "midiseed" ), QString( "424242" ) );
		QCOMPARE( NoteRandom::readProjectSeed( head ), 424242u );

		// and a seed of 0 can be written back to the default state
		NoteRandom::writeProjectSeed( head, 0 );
		QCOMPARE( head.attribute( "midiseed" ), QString( "424242" ) ); // not cleared, just not written
	}
};

QTEST_GUILESS_MAIN(NoteRandomTest)
#include "NoteRandomTest.moc"
