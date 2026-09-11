/*
 * SessionModel.cpp - Session View data layer (clip slots, scenes)
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

#include <algorithm>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

namespace lmms
{

namespace
{
using namespace sessionSerialization;

//! Upper bounds for a grid read from a file, so a malformed or hostile
//! project cannot make the loader allocate an unbounded grid.
constexpr int MaxTracks = 256;
constexpr int MaxScenes = 512;

//! Restores every <scene> child of a <session> block.
void restoreScenes( const QDomElement& scenesElement, std::vector<Scene>& scenes )
{
	for( QDomElement sceneElement = scenesElement.firstChildElement( QStringLiteral( "scene" ) );
		!sceneElement.isNull();
		sceneElement = sceneElement.nextSiblingElement( QStringLiteral( "scene" ) ) )
	{
		const int index = sceneElement.attribute( QStringLiteral( "index" ),
			QStringLiteral( "-1" ) ).toInt();
		if( index >= 0 && index < static_cast<int>( scenes.size() ) )
		{
			scenes[static_cast<std::size_t>( index )].restoreState( sceneElement );
		}
	}
}


//! Restores every <clip> child of a <session> block. Slots pointing outside
//! the grid are dropped (the block is malformed or truncated).
void restoreClips( const QDomElement& clipsElement, SessionModel& model )
{
	for( QDomElement clipElement = clipsElement.firstChildElement( QStringLiteral( "clip" ) );
		!clipElement.isNull();
		clipElement = clipElement.nextSiblingElement( QStringLiteral( "clip" ) ) )
	{
		const int track = clipElement.attribute( QStringLiteral( "track" ),
			QStringLiteral( "-1" ) ).toInt();
		const int scene = clipElement.attribute( QStringLiteral( "scene" ),
			QStringLiteral( "-1" ) ).toInt();
		if( track >= 0 && track < model.trackCount() && scene >= 0 && scene < model.sceneCount() )
		{
			model.slot( track, scene ).restoreState( clipElement );
		}
	}
}
}


// ---------------------------------------------------------------------------
// SessionModel
// ---------------------------------------------------------------------------

void SessionModel::setTrackCount( int tracks )
{
	resize( tracks, m_sceneCount );
}


void SessionModel::setSceneCount( int scenes )
{
	resize( m_trackCount, scenes );
}


void SessionModel::resize( int tracks, int scenes )
{
	tracks = std::clamp( tracks, 0, MaxTracks );
	scenes = std::clamp( scenes, 0, MaxScenes );
	if( tracks == m_trackCount && scenes == m_sceneCount )
	{
		return;
	}

	const std::size_t slotCount = static_cast<std::size_t>( tracks ) * static_cast<std::size_t>( scenes );
	// NOTE: the local must not be called "slots" - Qt defines that as a
	// keyword macro (Q_SLOTS), which would erase the declarator.
	std::vector<ClipSlot> newSlots;
	newSlots.resize( slotCount );
	const int copyTracks = std::min( tracks, m_trackCount );
	const int copyScenes = std::min( scenes, m_sceneCount );
	for( int track = 0; track < copyTracks; ++track )
	{
		for( int scene = 0; scene < copyScenes; ++scene )
		{
			const std::size_t oldIndex = static_cast<std::size_t>( track ) * m_sceneCount + scene;
			const std::size_t newIndex = static_cast<std::size_t>( track ) * scenes + scene;
			newSlots[newIndex] = m_slots[oldIndex];
		}
	}

	m_slots = std::move( newSlots );
	m_trackCount = tracks;
	m_sceneCount = scenes;
	m_scenes.resize( static_cast<std::size_t>( scenes ) );
}


const ClipSlot& SessionModel::slot( int track, int scene ) const
{
	// Out-of-range reads return a shared empty slot instead of crashing; the
	// grid is only valid for [0, trackCount) x [0, sceneCount).
	static const ClipSlot empty;
	if( track < 0 || track >= m_trackCount || scene < 0 || scene >= m_sceneCount )
	{
		return empty;
	}
	return m_slots[static_cast<std::size_t>( track ) * m_sceneCount + scene];
}


ClipSlot& SessionModel::slot( int track, int scene )
{
	// Same contract as the const overload; an out-of-range write is a caller
	// bug and lands in a throwaway slot rather than corrupting the grid.
	static ClipSlot scratch;
	if( track < 0 || track >= m_trackCount || scene < 0 || scene >= m_sceneCount )
	{
		scratch.clear();
		return scratch;
	}
	return m_slots[static_cast<std::size_t>( track ) * m_sceneCount + scene];
}


const Scene& SessionModel::scene( int scene ) const
{
	static const Scene empty;
	if( scene < 0 || scene >= m_sceneCount )
	{
		return empty;
	}
	return m_scenes[static_cast<std::size_t>( scene )];
}


Scene& SessionModel::scene( int scene )
{
	static Scene scratch;
	if( scene < 0 || scene >= m_sceneCount )
	{
		scratch = Scene();
		return scratch;
	}
	return m_scenes[static_cast<std::size_t>( scene )];
}


bool SessionModel::isEmpty() const
{
	if( m_trackCount > 0 || m_sceneCount > 0 )
	{
		return false;
	}
	if( m_globalLaunchQuantisation != DefaultLaunchQuantisation )
	{
		return false;
	}
	for( const Scene& scene : m_scenes )
	{
		if( scene.isModified() )
		{
			return false;
		}
	}
	for( const ClipSlot& slot : m_slots )
	{
		if( !slot.isEmpty() )
		{
			return false;
		}
	}
	return true;
}


void SessionModel::clear()
{
	m_trackCount = 0;
	m_sceneCount = 0;
	m_slots.clear();
	m_scenes.clear();
	m_globalLaunchQuantisation = DefaultLaunchQuantisation;
	m_hadSessionBlock = false;
	m_preservedUnknownXml.clear();
}


QDomElement SessionModel::saveState( QDomDocument& doc, QDomElement& parent ) const
{
	// A block written by a newer build is opaque to us: re-emit it verbatim
	// instead of regenerating it from the (empty) parsed model. See the
	// version policy in restoreState().
	if( !m_preservedUnknownXml.isEmpty() )
	{
		QDomDocument preserved;
		if( preserved.setContent( m_preservedUnknownXml, false ) )
		{
			const QDomElement preservedRoot = preserved.documentElement();
			if( !preservedRoot.isNull() )
			{
				QDomElement imported = doc.importNode( preservedRoot, true ).toElement();
				parent.appendChild( imported );
				return imported;
			}
		}
	}

	QDomElement session = doc.createElement( QStringLiteral( "session" ) );
	parent.appendChild( session );
	session.setAttribute( QStringLiteral( "version" ), CurrentVersion );
	session.setAttribute( QStringLiteral( "tracks" ), m_trackCount );
	session.setAttribute( QStringLiteral( "scenes" ), m_sceneCount );
	session.setAttribute( QStringLiteral( "launchquantisation" ),
		static_cast<int>( m_globalLaunchQuantisation ) );

	QDomElement scenesElement = doc.createElement( QStringLiteral( "scenes" ) );
	session.appendChild( scenesElement );
	for( int scene = 0; scene < m_sceneCount; ++scene )
	{
		const Scene& sceneModel = m_scenes[static_cast<std::size_t>( scene )];
		if( !sceneModel.isModified() )
		{
			continue;
		}
		QDomElement sceneElement = doc.createElement( QStringLiteral( "scene" ) );
		sceneElement.setAttribute( QStringLiteral( "index" ), scene );
		sceneModel.saveState( doc, sceneElement );
		scenesElement.appendChild( sceneElement );
	}

	QDomElement clipsElement = doc.createElement( QStringLiteral( "clips" ) );
	session.appendChild( clipsElement );
	for( int track = 0; track < m_trackCount; ++track )
	{
		for( int scene = 0; scene < m_sceneCount; ++scene )
		{
			const ClipSlot& clipSlot = slot( track, scene );
			if( clipSlot.isEmpty() )
			{
				continue;
			}
			QDomElement clipElement = doc.createElement( QStringLiteral( "clip" ) );
			clipElement.setAttribute( QStringLiteral( "track" ), track );
			clipElement.setAttribute( QStringLiteral( "scene" ), scene );
			clipSlot.saveState( doc, clipElement );
			clipsElement.appendChild( clipElement );
		}
	}

	return session;
}


bool SessionModel::restoreState( const QDomElement& element )
{
	clear();

	if( element.isNull() || element.tagName() != QStringLiteral( "session" ) )
	{
		return false;
	}

	// A <session> block without a version attribute can only come from this
	// feature's first version, so it is read as version 1.
	const int version = element.attribute( QStringLiteral( "version" ), QStringLiteral( "1" ) ).toInt();
	if( version > CurrentVersion )
	{
		// Version policy: a block from a newer build is IGNORED, never
		// interpreted with guessed semantics - but it is kept verbatim so
		// that saving through this build does not silently drop data the
		// newer build wrote. This matches DataFile's policy of never refusing
		// a project whose version is above the known upgrade chain
		// (DataFile::upgrade() clamps instead of rejecting), and is stricter
		// about content preservation.
		m_hadSessionBlock = true;
		m_preservedUnknownXml = elementToString( element );
		return false;
	}

	m_hadSessionBlock = true;

	const int tracks = element.attribute( QStringLiteral( "tracks" ), QStringLiteral( "0" ) ).toInt();
	const int scenes = element.attribute( QStringLiteral( "scenes" ), QStringLiteral( "0" ) ).toInt();
	resize( tracks, scenes );

	m_globalLaunchQuantisation = launchQuantisationFromInt(
		element.attribute( QStringLiteral( "launchquantisation" ),
			QString::number( static_cast<int>( DefaultLaunchQuantisation ) ) ).toInt(), false );

	restoreScenes( element.firstChildElement( QStringLiteral( "scenes" ) ), m_scenes );
	restoreClips( element.firstChildElement( QStringLiteral( "clips" ) ), *this );

	return true;
}

} // namespace lmms
