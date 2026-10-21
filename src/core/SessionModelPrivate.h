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
#include <vector>

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

//! A whole-number attribute, read STRICTLY. False when the attribute is
//! absent or does not hold a number at all - QString::toInt() answers 0 for a
//! nonnumeric text AND for an overflow, and 0 is a VALID index: an unchecked
//! read of a damaged attribute does not fail loudly, it silently addresses the
//! first scene, the first track or the first cell. The reader of an index must
//! therefore go through this helper and REFUSE the malformed value instead of
//! taking the 0 that toInt() hands back.
inline bool intAttribute( const QDomElement& element, const QString& name, int& value )
{
	if( !element.hasAttribute( name ) ) { return false; }
	bool ok = false;
	const int parsed = element.attribute( name ).toInt( &ok );
	if( !ok ) { return false; }
	value = parsed;
	return true;
}

//! A 0/1 attribute, read STRICTLY: the given default when the attribute is
//! absent or does not hold a number (the same discipline doubleAttribute
//! applies to a floating-point attribute).
inline bool boolAttribute( const QDomElement& element, const QString& name, bool defaultValue )
{
	if( !element.hasAttribute( name ) ) { return defaultValue; }
	bool ok = false;
	const int parsed = element.attribute( name ).toInt( &ok );
	return ok ? parsed != 0 : defaultValue;
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
	// EVERY grid the enum names, listed rather than switched on: the sub-bar
	// values and EightBars carry their own enumerators (include/SessionModel.h),
	// and a switch that stopped at FourBars sent all of them to the fallback -
	// a 1/16 cell came back as Global, and a global EightBars came back as Bar.
	// A value that is not a grid at all still falls back rather than producing
	// an enum no switch downstream has a case for.
	static constexpr LaunchQuantisation Grids[] = {
		LaunchQuantisation::None,
		LaunchQuantisation::Bar,
		LaunchQuantisation::TwoBars,
		LaunchQuantisation::FourBars,
		LaunchQuantisation::Sixteenth,
		LaunchQuantisation::Eighth,
		LaunchQuantisation::Quarter,
		LaunchQuantisation::Half,
		LaunchQuantisation::EightBars,
	};
	for( const LaunchQuantisation grid : Grids )
	{
		if( static_cast<int>( grid ) == value ) { return grid; }
	}
	return allowGlobal ? LaunchQuantisation::Global : LaunchQuantisation::Bar;
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
	// v2 (R5.2) is v1 plus scene Follow Action chains; this build reads both.
	const QString version = section.attribute( QStringLiteral( "v" ) );
	if( ( version != QLatin1String( "1" ) && version != QLatin1String( "2" ) )
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

//! R5.2: "2" when any scene carries a Follow Action chain, else "1" (the v1 bytes).
inline QString scenesSectionVersion( const std::vector<Scene>& scenes )
{
	for( const Scene& scene : scenes )
	{
		if( !scene.followActions().empty() ) { return QStringLiteral( "2" ); }
	}
	return QStringLiteral( "1" );
}

//! A <z:scene> row: the index attribute this build writes, in range, then
//! Scene's own restoreState at the legacy reader's depth (upconverter
//! parity - the attribute vocabulary never moved).
inline bool applySceneRow( const QDomElement& row, std::vector<Scene>& scenes )
{
	int index = -1;
	if( !intAttribute( row, QStringLiteral( "index" ), index ) ) { return false; }
	if( static_cast<unsigned>( index ) >= scenes.size() ) { return false; }
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
	int track = -1;
	if( !intAttribute( cell, QStringLiteral( "track" ), track ) ) { return false; }
	if( static_cast<unsigned>( track ) >= static_cast<unsigned>( tracks ) ) { return false; }
	int scene = -1;
	if( !intAttribute( cell, QStringLiteral( "scene" ), scene ) ) { return false; }
	if( static_cast<unsigned>( scene ) >= static_cast<unsigned>( scenes ) ) { return false; }
	slotList[static_cast<std::size_t>( track ) * static_cast<std::size_t>( scenes )
		+ static_cast<std::size_t>( scene )].restoreState( cell );
	return true;
}

} // namespace sessionSerialization

} // namespace lmms

#endif // LMMS_SESSION_MODEL_PRIVATE_H
