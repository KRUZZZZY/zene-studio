/*
 * AutomationTrack.cpp - AutomationTrack handles automation of objects without
 *                       a track
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2006-2008 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
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

#include "AutomationTrack.h"

#include "AutomationTrackView.h"
#include "AutomationClip.h"

namespace lmms
{


AutomationTrack::AutomationTrack( TrackContainer* tc, bool _hidden ) :
	Track( _hidden ? Type::HiddenAutomation : Type::Automation, tc )
{
	setName( tr( "Automation track" ) );
}

bool AutomationTrack::play( const TimePos & time_start, const f_cnt_t _frames,
							const f_cnt_t _frame_base, int _clip_num )
{
	return false;
}




gui::TrackView* AutomationTrack::createView( gui::TrackContainerView* tcv )
{
	return new gui::AutomationTrackView( this, tcv );
}




Clip* AutomationTrack::createClip(const TimePos & pos)
{
	auto p = new AutomationClip(this);
	p->movePosition(pos);
	return p;
}




void AutomationTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& _this, bool presetMode)
{
	if (m_followsTrack >= 0) { _this.setAttribute("follows", m_followsTrack); }  // 0.5.0 ED-181
}




void AutomationTrack::loadTrackSpecificSettings( const QDomElement & _this )
{
	// A `follows` that is not a number is not a track id. QString::toInt() answers
	// 0 for text it cannot parse, and 0 IS a track id (ids start at 0), so the
	// failed conversion has to become the no-track sentinel -1, not track 0.
	bool followsOk = false;
	const int followsId = _this.attribute("follows", "-1").toInt(&followsOk);
	m_followsTrack = followsOk ? followsId : -1;
	// just in case something somehow wrent wrong...
	if( type() == Type::HiddenAutomation )
	{
		setMuted( false );
	}
}


} // namespace lmms
