/*
 * NeutralTrack.h - a neutral arrangement row (Q050-283.1)
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
 */


#ifndef LMMS_NEUTRAL_TRACK_H
#define LMMS_NEUTRAL_TRACK_H

#include "Track.h"

namespace lmms
{

/*! A NEUTRAL arrangement row (0.5.0 Q050-283, stage 1 of 3; docs/050-features/Q050-283.1.md).
 *
 *  The row a musician adds BEFORE deciding whether it will carry an audio input or a
 *  virtual instrument. It is an ordinary row of the SAME flat track list as every other
 *  track, so it has a creation-assigned stable id (`trk-<n>`), a position in the
 *  arrangement, a name, mute/solo, colour, height and folder membership - all of that is
 *  Track's own, persisted by Track::saveTrack, which is why stage 1 adds NO new
 *  persistence beyond one empty `<neutraltrack>` element.
 *
 *  It plays nothing, owns no clips and no mixer channel: it is a placeholder row whose
 *  later roles (instrument, audio input) are stages 2 and 3. Those stages add their
 *  role data to THIS track's own element (as attributes or the `<neutraltrack>` element's
 *  own children, never a child of `<track>`: Track::loadTrack turns an unrecognised
 *  `<track>` child into a Clip), so a neutral row saved by this stage still loads in them.
 */
class LMMS_EXPORT NeutralTrack : public Track
{
	Q_OBJECT
public:
	explicit NeutralTrack(TrackContainer* tc);
	~NeutralTrack() override = default;

	//! Never schedules anything: a neutral row contributes no play handle.
	bool play(const TimePos& start, const f_cnt_t frames, const f_cnt_t frameBase, int clipNum = -1) override;

	QString nodeName() const override { return QStringLiteral("neutraltrack"); }

	gui::TrackView* createView(gui::TrackContainerView* view) override;
	//! A neutral row holds no clips, so there is nothing to create: always null (like TrackFolder).
	Clip* createClip(const TimePos& pos) override;

	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings(const QDomElement& element) override;
};

} // namespace lmms

#endif // LMMS_NEUTRAL_TRACK_H
