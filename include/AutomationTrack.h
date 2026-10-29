/*
 * AutomationTrack.h - declaration of class AutomationTrack, which handles
 *                     automation of objects without a track
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

#ifndef LMMS_AUTOMATION_TRACK_H
#define LMMS_AUTOMATION_TRACK_H

#include "Track.h"

namespace lmms
{

namespace gui { class AutomationTrackView; }

class LMMS_EXPORT AutomationTrack : public Track
{
	Q_OBJECT
public:
	AutomationTrack( TrackContainer* tc, bool _hidden = false );
	~AutomationTrack() override = default;

	bool play( const TimePos & _start, const f_cnt_t _frames,
						const f_cnt_t _frame_base, int _clip_num = -1 ) override;

	QString nodeName() const override
	{
		return "automationtrack";
	}

	gui::TrackView * createView( gui::TrackContainerView* ) override;
	Clip* createClip(const TimePos & pos) override;

	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings( const QDomElement & _this ) override;

	/*! 0.5.0 ED-181/190: the track this automation lane belongs to (its Track::id(), 0 for none) -
	 *  the lane sits under that track and, when the arranger says so, its points move with that
	 *  track's clips. Saved as `follows`, only when set. */
	int followsTrack() const { return m_followsTrack; }
	void setFollowsTrack(int trackId) { m_followsTrack = trackId; }

private:
	friend class gui::AutomationTrackView; // qualified: unqualified, MSVC declares a phantom lmms::AutomationTrackView
	int m_followsTrack = -1;  // no track: ids start at 0, so 0 is a track (it made every lane follow track 0)

} ;


} // namespace lmms

#endif // LMMS_AUTOMATION_TRACK_H
