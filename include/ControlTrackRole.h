/*
 * ControlTrackRole.h - the role of an arrangement row: neutral or instrument (Q050-283.2)
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

#ifndef LMMS_CONTROL_TRACK_ROLE_H
#define LMMS_CONTROL_TRACK_ROLE_H

#include <QJsonObject>

#include "lmms_export.h"

namespace lmms
{

class Track;

namespace control
{

/*! The role fields of one row, for track.get_state and track.list (Q050-283.2):
 *  `role` ("neutral" or "instrument") on a row track.set_role can switch, and on a neutral row
 *  `kept_roles` - the inactive roles it keeps, each `{role, instrument}`. Other track types get
 *  nothing, so their readback is unchanged. */
LMMS_EXPORT void insertTrackRoleFields(QJsonObject* entry, const Track* track);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_TRACK_ROLE_H
