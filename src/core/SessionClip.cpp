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

namespace lmms
{

namespace
{
using namespace sessionSerialization;
}

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

	if( !m_followActions.empty() )
	{
		QDomElement chain = doc.createElement( QStringLiteral( "followactions" ) );
		element.appendChild( chain );
		for( const FollowAction& action : m_followActions )
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
}


void ClipSlot::restoreState( const QDomElement& element )
{
	*this = ClipSlot();

	const int type = element.attribute( QStringLiteral( "type" ), QStringLiteral( "0" ) ).toInt();
	switch( type )
	{
		case static_cast<int>( Type::Midi ):
			m_type = Type::Midi;
			m_patternId = element.attribute( QStringLiteral( "pattern" ), QStringLiteral( "-1" ) ).toInt();
			break;
		case static_cast<int>( Type::Audio ):
			m_type = Type::Audio;
			m_audioSource = element.attribute( QStringLiteral( "src" ) );
			break;
		default:
			m_type = Type::Empty;
			break;
	}

	m_name = element.attribute( QStringLiteral( "name" ) );
	m_launchMode = launchModeFromInt( element.attribute( QStringLiteral( "launchmode" ),
		QStringLiteral( "0" ) ).toInt() );
	m_launchQuantisation = launchQuantisationFromInt( element.attribute( QStringLiteral( "quantisation" ),
		QStringLiteral( "-1" ) ).toInt(), true );
	m_legato = element.attribute( QStringLiteral( "legato" ), QStringLiteral( "0" ) ).toInt() != 0;
	m_loopStart = element.attribute( QStringLiteral( "loopstart" ), QStringLiteral( "0" ) ).toInt();
	m_loopLength = element.attribute( QStringLiteral( "looplength" ), QStringLiteral( "0" ) ).toInt();
	m_gainDb = static_cast<float>( doubleAttribute( element, QStringLiteral( "gain" ), 0.0 ) );
	m_transpose = element.attribute( QStringLiteral( "transpose" ), QStringLiteral( "0" ) ).toInt();
	m_detune = element.attribute( QStringLiteral( "detune" ), QStringLiteral( "0" ) ).toInt();
	m_ramMode = element.attribute( QStringLiteral( "ram" ), QStringLiteral( "0" ) ).toInt() != 0;

	const QDomElement chain = element.firstChildElement( QStringLiteral( "followactions" ) );
	for( QDomElement actionElement = chain.firstChildElement( QStringLiteral( "followaction" ) );
		!actionElement.isNull();
		actionElement = actionElement.nextSiblingElement( QStringLiteral( "followaction" ) ) )
	{
		FollowAction action;
		action.type = followActionTypeFromInt( actionElement.attribute( QStringLiteral( "type" ),
			QStringLiteral( "0" ) ).toInt() );
		action.chance = doubleAttribute( actionElement, QStringLiteral( "chance" ), 1.0 );
		action.linked = actionElement.attribute( QStringLiteral( "linked" ), QStringLiteral( "1" ) ).toInt() != 0;
		action.timeBars = doubleAttribute( actionElement, QStringLiteral( "time" ), 1.0 );
		action.jumpTo = actionElement.attribute( QStringLiteral( "jump" ), QStringLiteral( "0" ) ).toInt();
		m_followActions.push_back( action );
	}
}



// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------

void Scene::saveState( QDomDocument& doc, QDomElement& element ) const
{
	Q_UNUSED( doc )

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
		m_timeSigNumerator = element.attribute( QStringLiteral( "timesig_numerator" ),
			QStringLiteral( "4" ) ).toInt();
		m_timeSigDenominator = element.attribute( QStringLiteral( "timesig_denominator" ),
			QStringLiteral( "4" ) ).toInt();
	}
}

} // namespace lmms
