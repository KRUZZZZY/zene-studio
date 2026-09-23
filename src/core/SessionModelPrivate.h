/*
 * SessionModelPrivate.h - internal serialisation helpers (Session View)
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


#ifndef LMMS_SESSION_MODEL_PRIVATE_H
#define LMMS_SESSION_MODEL_PRIVATE_H

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QTextStream>

#include "SessionModel.h"

namespace lmms
{

//! Internal helpers shared by SessionModel.cpp and SessionClip.cpp. Not part
//! of the public API.
namespace sessionSerialization
{

//! Shortest decimal that round-trips `value` exactly: readable in the XML
//! ("0.6", "-3.25", "174") but never lossy - a value that needs more digits
//! falls back to 'g' with 17 significant digits, which round-trips every
//! IEEE-754 double through QString::toDouble().
inline QString doubleToAttribute( double value )
{
	const QString shortForm = QString::number( value );
	bool ok = false;
	const double parsed = shortForm.toDouble( &ok );
	if( ok && parsed == value )
	{
		return shortForm;
	}
	return QString::number( value, 'g', 17 );
}

inline double doubleAttribute( const QDomElement& element, const QString& name, double defaultValue )
{
	bool ok = false;
	const double value = element.attribute( name ).toDouble( &ok );
	return ok ? value : defaultValue;
}

inline LaunchMode launchModeFromInt( int value )
{
	if( value < static_cast<int>( LaunchMode::Trigger )
		|| value > static_cast<int>( LaunchMode::Repeat ) )
	{
		return LaunchMode::Trigger;
	}
	return static_cast<LaunchMode>( value );
}

inline LaunchQuantisation launchQuantisationFromInt( int value, bool allowGlobal )
{
	if( allowGlobal && value == static_cast<int>( LaunchQuantisation::Global ) )
	{
		return LaunchQuantisation::Global;
	}
	switch( value )
	{
		case static_cast<int>( LaunchQuantisation::None ):
		case static_cast<int>( LaunchQuantisation::Bar ):
		case static_cast<int>( LaunchQuantisation::TwoBars ):
		case static_cast<int>( LaunchQuantisation::FourBars ):
			return static_cast<LaunchQuantisation>( value );
		default:
			return allowGlobal ? LaunchQuantisation::Global : LaunchQuantisation::Bar;
	}
}

inline FollowAction::Type followActionTypeFromInt( int value )
{
	if( value < static_cast<int>( FollowAction::Type::NoAction )
		|| value > static_cast<int>( FollowAction::Type::Jump ) )
	{
		return FollowAction::Type::NoAction;
	}
	return static_cast<FollowAction::Type>( value );
}

inline QString elementToString( const QDomElement& element )
{
	QString out;
	QTextStream stream( &out );
	element.save( stream, 2 );
	stream.flush();
	return out;
}

//! Grid clamp bounds for a section read from a file, so a malformed or
//! hostile project cannot make the loader allocate an unbounded grid.
//! Moved here from SessionModel.cpp's anonymous namespace so the <z:scenes>
//! claim helpers below (ARCH-4 S8) can share them; only SessionModel.cpp
//! and SessionClip.cpp include this header, so the constants stay TU-private.
constexpr int MaxTracks = 256;
constexpr int MaxScenes = 512;

//! ARCH-4 S8 claim-shape check for a top-level <z:scenes> section: the
//! version this build writes and both dimension attributes, each within the
//! grid clamp (the unsigned compare also rejects negatives). Anything else
//! declines, and Song::restoreNamedSection's walk preserves the element
//! verbatim as unclaimed instead of half-loading it.
inline bool scenesSectionDims( const QDomElement& section, int& tracks, int& scenes )
{
	if( section.attribute( QStringLiteral( "v" ) ) != QLatin1String( "1" )
		|| !section.hasAttribute( QStringLiteral( "tracks" ) )
		|| !section.hasAttribute( QStringLiteral( "scenes" ) ) )
	{
		return false;
	}
	bool ok = false;
	tracks = section.attribute( QStringLiteral( "tracks" ) ).toInt( &ok );
	if( !ok || static_cast<unsigned>( tracks ) > static_cast<unsigned>( MaxTracks ) )
	{
		return false;
	}
	scenes = section.attribute( QStringLiteral( "scenes" ) ).toInt( &ok );
	if( !ok || static_cast<unsigned>( scenes ) > static_cast<unsigned>( MaxScenes ) )
	{
		return false;
	}
	return true;
}

//! A <z:scene> row: the index attribute this build writes, in range, then
//! Scene's own restoreState at the legacy reader's depth (upconverter
//! parity - the attribute vocabulary never moved).
inline bool applySceneRow( const QDomElement& row, std::vector<Scene>& scenes )
{
	if( !row.hasAttribute( QStringLiteral( "index" ) ) ) { return false; }
	bool ok = false;
	const int index = row.attribute( QStringLiteral( "index" ) ).toInt( &ok );
	if( !ok || static_cast<unsigned>( index ) >= scenes.size() ) { return false; }
	scenes[static_cast<std::size_t>( index )].restoreState( row );
	return true;
}

//! A <z:cell>: the two coordinates of the unchanged `track * scenes + scene`
//! addressing rule (SPEC-ARCH-4 1.1), in range, then ClipSlot's own
//! restoreState at the legacy reader's depth. A missing or out-of-grid
//! coordinate declines the whole section.
inline bool applySceneCell( const QDomElement& cell, std::vector<ClipSlot>& slotList,
	int tracks, int scenes )
{
	// NOTE: the parameter is slotList, never `slots` - Qt defines slots as a
	// macro (Q_SLOTS) in every TU that pulls in qobjectdefs, and the macro
	// would eat the identifier here (cost one compile cycle in S8).
	if( !cell.hasAttribute( QStringLiteral( "track" ) )
		|| !cell.hasAttribute( QStringLiteral( "scene" ) ) ) { return false; }
	bool ok = false;
	const int track = cell.attribute( QStringLiteral( "track" ) ).toInt( &ok );
	if( !ok || static_cast<unsigned>( track ) >= static_cast<unsigned>( tracks ) )
	{
		return false;
	}
	const int scene = cell.attribute( QStringLiteral( "scene" ) ).toInt( &ok );
	if( !ok || static_cast<unsigned>( scene ) >= static_cast<unsigned>( scenes ) )
	{
		return false;
	}
	slotList[static_cast<std::size_t>( track ) * static_cast<std::size_t>( scenes )
		+ static_cast<std::size_t>( scene )].restoreState( cell );
	return true;
}

} // namespace sessionSerialization

} // namespace lmms

#endif // LMMS_SESSION_MODEL_PRIVATE_H
