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

} // namespace sessionSerialization

} // namespace lmms

#endif // LMMS_SESSION_MODEL_PRIVATE_H
