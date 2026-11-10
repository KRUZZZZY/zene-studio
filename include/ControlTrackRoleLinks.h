/*
 * ControlTrackRoleLinks.h - the automation links a kept instrument role takes with it (Q050-283.2)
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

#ifndef LMMS_CONTROL_TRACK_ROLE_LINKS_H
#define LMMS_CONTROL_TRACK_ROLE_LINKS_H

#include <vector>

#include <QString>

#include "NeutralTrack.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

namespace control
{

/*! Takes every automation link to a control of @a instrument OFF its automation clip, before the
 *  instrument track is destroyed by a role switch, and answers them (Q050-283.2). @a xml is the track's
 *  captured document, taken while the links were still in place: a linked control is "automated", so its
 *  element carries its id, and the ids in the document are exactly the instrument's own linked controls.
 *  Each link is removed with AutomationClip::removeObject and its destroyed() connection is cut, so the
 *  clip neither keeps a dangling target nor queues the id for a later resolve; the clip's points stay.
 *  The row's own mute/solo controls (direct children of `<track>`) are not the role's and are left alone.
 *  Clips are searched in the song, the pattern store and the song-global automation track.
 *  UI/control thread only. */
LMMS_EXPORT std::vector<KeptAutomationLink> detachInstrumentAutomation(const QString& xml);

/*! Puts @a links back once the instrument track has been rebuilt from its kept document (whose controls
 *  are given back the ids the links name). A link whose clip or control cannot be found is skipped.
 *  Answers how many were re-made. UI/control thread only. */
LMMS_EXPORT int reattachInstrumentAutomation(const std::vector<KeptAutomationLink>& links);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_TRACK_ROLE_LINKS_H
