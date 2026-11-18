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

/*! Takes automation links to one side of a role-switched row off their clips before its models are
 *  destroyed, and answers them (Q050-283.2). @a xml is the captured track document. @a rowControls
 *  selects the row's mute/solo models; false selects the instrument role's controls. Each link is
 *  removed with AutomationClip::removeObject and its destroyed() connection is cut, so the clip's
 *  points stay and it queues no dangling id. Clips are searched in the song, pattern store and
 *  song-global automation track. UI/control thread only. */
LMMS_EXPORT std::vector<KeptAutomationLink> detachTrackAutomation(const QString& xml, bool rowControls);

/*! Puts @a links back once the instrument track has been rebuilt from its kept document (whose controls
 *  are given back the ids the links name). A link whose clip or control cannot be found is skipped.
 *  Answers how many were re-made. UI/control thread only. */
LMMS_EXPORT int reattachInstrumentAutomation(const std::vector<KeptAutomationLink>& links);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_TRACK_ROLE_LINKS_H
