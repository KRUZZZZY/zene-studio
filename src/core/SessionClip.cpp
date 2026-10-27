/*
 * SessionClip.cpp - ClipSlot and Scene serialisation (Session View)
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


#include "SessionModelPrivate.h"

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "Engine.h"
#include "Song.h"

namespace lmms
{

namespace
{
using namespace sessionSerialization;

//! A malformed, present clip integer must be visible in the project-load
//! report even when its documented fallback is also zero.
int clipIntegerAttribute( const QDomElement& element, const QString& name, int defaultValue )
{
	int value = defaultValue;
	if( intAttribute( element, name, value ) ) { return value; }
	if( element.hasAttribute( name ) )
	{
		if( Song* song = Engine::getSong() )
		{
			song->collectError( Song::tr( "Malformed session clip attribute '%1'; using its default." ).arg( name ) );
		}
	}
	return defaultValue;
}

//! A Follow Action chain as a <followactions> child - the one vocabulary a cell and (R5.2) a
//! scene share. An empty chain writes nothing, so neither element gains a byte.
void saveFollowChain( QDomDocument& doc, QDomElement& element, const std::vector<FollowAction>& actions )
{
	if( actions.empty() ) { return; }
	QDomElement chain = doc.createElement( QStringLiteral( "followactions" ) );
	element.appendChild( chain );
	for( const FollowAction& action : actions )
	{
		QDomElement actionElement = doc.createElement( QStringLiteral( "followaction" ) );
		actionElement.setAttribute( QStringLiteral( "type" ), static_cast<int>( action.type ) );
		actionElement.setAttribute( QStringLiteral( "chance" ), doubleToAttribute( action.chance ) );
		actionElement.setAttribute( QStringLiteral( "linked" ), action.linked ? 1 : 0 );
		actionElement.setAttribute( QStringLiteral( "time" ), doubleToAttribute( action.timeBars ) );
		actionElement.setAttribute( QStringLiteral( "jump" ), action.jumpTo );
		chain.appendChild( actionElement );
	}
}

std::vector<FollowAction> restoreFollowChain( const QDomElement& element )
{
	std::vector<FollowAction> actions;
	const QDomElement chain = element.firstChildElement( QStringLiteral( "followactions" ) );
	for( QDomElement actionElement = chain.firstChildElement( QStringLiteral( "followaction" ) );
		!actionElement.isNull();
		actionElement = actionElement.nextSiblingElement( QStringLiteral( "followaction" ) ) )
	{
		FollowAction action;
		// The chain's numbers are read through the STRICT helpers:
		// QString::toInt() answers 0 for a value it cannot convert, and 0 is
		// both a valid action type (NoAction) and a valid scene row - so an
		// unchecked read of a damaged chain would quietly aim it at the FIRST
		// scene.
		int type = 0;
		intAttribute( actionElement, QStringLiteral( "type" ), type );
		action.type = followActionTypeFromInt( type );
		action.chance = doubleAttribute( actionElement, QStringLiteral( "chance" ), 1.0 );
		action.linked = boolAttribute( actionElement, QStringLiteral( "linked" ), true );
		action.timeBars = doubleAttribute( actionElement, QStringLiteral( "time" ), 1.0 );
		// A jump target that is not a number is NOT scene zero: -1 is this
		// codebase's "no addressable target" (include/SessionFollow.h's
		// followTargetScene refuses it), so the action keeps its place in the
		// chain and its fire is REFUSED - the same answer an out-of-grid row
		// gets, never a silent jump to the first scene. An attribute that is
		// not there at all keeps the legacy default of 0.
		int jump = 0;
		if( !intAttribute( actionElement, QStringLiteral( "jump" ), jump )
			&& actionElement.hasAttribute( QStringLiteral( "jump" ) ) )
		{
			jump = -1;
		}
		action.jumpTo = jump;
		actions.push_back( action );
	}
	return actions;
}

} // namespace

// ---------------------------------------------------------------------------
// ClipSlot
// ---------------------------------------------------------------------------

void ClipSlot::setPatternReference( int patternId )
{
	m_type = Type::Midi;
	m_patternId = patternId;
	m_audioSource.clear();
}


void ClipSlot::setAudioReference( const QString& source )
{
	m_type = Type::Audio;
	m_audioSource = source;
	m_patternId = -1;
}


void ClipSlot::clear()
{
	*this = ClipSlot();
}


void ClipSlot::saveState( QDomDocument& doc, QDomElement& element ) const
{
	Q_UNUSED( doc )

	element.setAttribute( QStringLiteral( "type" ), static_cast<int>( m_type ) );
	switch( m_type )
	{
		case Type::Midi:
			element.setAttribute( QStringLiteral( "pattern" ), m_patternId );
			break;
		case Type::Audio:
			element.setAttribute( QStringLiteral( "src" ), m_audioSource );
			break;
		case Type::Empty:
			break;
	}
	if( !m_name.isEmpty() )
	{
		element.setAttribute( QStringLiteral( "name" ), m_name );
	}
	element.setAttribute( QStringLiteral( "launchmode" ), static_cast<int>( m_launchMode ) );
	element.setAttribute( QStringLiteral( "quantisation" ), static_cast<int>( m_launchQuantisation ) );
	element.setAttribute( QStringLiteral( "legato" ), m_legato ? 1 : 0 );
	element.setAttribute( QStringLiteral( "loopstart" ), m_loopStart );
	element.setAttribute( QStringLiteral( "looplength" ), m_loopLength );
	element.setAttribute( QStringLiteral( "gain" ), doubleToAttribute( m_gainDb ) );
	element.setAttribute( QStringLiteral( "transpose" ), m_transpose );
	element.setAttribute( QStringLiteral( "detune" ), m_detune );
	element.setAttribute( QStringLiteral( "ram" ), m_ramMode ? 1 : 0 );
	if( m_warp )  // only when set: a project that never warps saves exactly as before
	{
		element.setAttribute( QStringLiteral( "warp" ), 1 );
		element.setAttribute( QStringLiteral( "warpbpm" ), doubleToAttribute( m_warpBpm ) );
	}

	saveFollowChain( doc, element, m_followActions );
}


void ClipSlot::restoreState( const QDomElement& element )
{
	*this = ClipSlot();

	// The reader takes its numbers through the STRICT helpers
	// (sessionSerialization::intAttribute): QString::toInt() answers 0 for a
	// value it cannot convert, and 0 is a VALID type, a VALID pattern id and
	// a VALID quantisation - so an unreadable attribute used to aim the cell
	// at the first pattern instead of failing. A value that cannot be
	// converted now falls back to the attribute's own documented default.
	int type = 0;
	intAttribute( element, QStringLiteral( "type" ), type );
	switch( type )
	{
		case static_cast<int>( Type::Midi ):
		{
			m_type = Type::Midi;
			// A failed conversion restores as "no reference", which is what a
			// missing attribute already did.
			int patternId = -1;
			intAttribute( element, QStringLiteral( "pattern" ), patternId );
			m_patternId = patternId;
			break;
		}
		case static_cast<int>( Type::Audio ):
			m_type = Type::Audio;
			m_audioSource = element.attribute( QStringLiteral( "src" ) );
			break;
		default:
			m_type = Type::Empty;
			break;
	}

	m_name = element.attribute( QStringLiteral( "name" ) );
	m_launchMode = launchModeFromInt( clipIntegerAttribute( element,
		QStringLiteral( "launchmode" ), 0 ) );
	// The documented default of an absent quantisation is Global (-1); an
	// unreadable one now falls back to it instead of to "None" (0).
	int quantisation = -1;
	intAttribute( element, QStringLiteral( "quantisation" ), quantisation );
	m_launchQuantisation = launchQuantisationFromInt( quantisation, true );
	m_legato = clipIntegerAttribute( element, QStringLiteral( "legato" ), 0 ) != 0;
	m_loopStart = clipIntegerAttribute( element, QStringLiteral( "loopstart" ), 0 );
	m_loopLength = clipIntegerAttribute( element, QStringLiteral( "looplength" ), 0 );
	m_gainDb = static_cast<float>( doubleAttribute( element, QStringLiteral( "gain" ), 0.0 ) );
	m_transpose = clipIntegerAttribute( element, QStringLiteral( "transpose" ), 0 );
	m_detune = clipIntegerAttribute( element, QStringLiteral( "detune" ), 0 );
	m_ramMode = clipIntegerAttribute( element, QStringLiteral( "ram" ), 0 ) != 0;
	m_warp = clipIntegerAttribute( element, QStringLiteral( "warp" ), 0 ) != 0;
	m_warpBpm = doubleAttribute( element, QStringLiteral( "warpbpm" ), 120.0 );

	m_followActions = restoreFollowChain( element );
}



// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

void Scene::saveState( QDomDocument& doc, QDomElement& element ) const
{
	if( !m_name.isEmpty() )
	{
		element.setAttribute( QStringLiteral( "name" ), m_name );
	}
	if( m_tempoEnabled )
	{
		element.setAttribute( QStringLiteral( "tempo" ), doubleToAttribute( m_tempo ) );
	}
	if( m_timeSigEnabled )
	{
		element.setAttribute( QStringLiteral( "timesig_numerator" ), m_timeSigNumerator );
		element.setAttribute( QStringLiteral( "timesig_denominator" ), m_timeSigDenominator );
	}
	if( !m_color.isEmpty() )
	{
		element.setAttribute( QStringLiteral( "color" ), m_color );
	}
	// R5.2: the cell's own vocabulary, so one reader serves both.
	saveFollowChain( doc, element, m_followActions );
}


void Scene::restoreState( const QDomElement& element )
{
	*this = Scene();

	m_name = element.attribute( QStringLiteral( "name" ) );
	if( element.hasAttribute( QStringLiteral( "tempo" ) ) )
	{
		m_tempoEnabled = true;
		m_tempo = doubleAttribute( element, QStringLiteral( "tempo" ), 120.0 );
	}
	if( element.hasAttribute( QStringLiteral( "timesig_numerator" ) )
		|| element.hasAttribute( QStringLiteral( "timesig_denominator" ) ) )
	{
		m_timeSigEnabled = true;
		// The documented default of an absent half is 4; an unreadable one now
		// falls back to it instead of to 0 (a time signature of 0/4).
		int numerator = 4;
		intAttribute( element, QStringLiteral( "timesig_numerator" ), numerator );
		m_timeSigNumerator = numerator;
		int denominator = 4;
		intAttribute( element, QStringLiteral( "timesig_denominator" ), denominator );
		m_timeSigDenominator = denominator;
	}
	m_followActions = restoreFollowChain( element );
	m_color = element.attribute( QStringLiteral( "color" ) );
}

} // namespace lmms
