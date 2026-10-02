/*
 * ControlBounceSupport.h - what the bounce and freeze commands share
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

#ifndef LMMS_CONTROL_BOUNCE_SUPPORT_H
#define LMMS_CONTROL_BOUNCE_SUPPORT_H

#include <QJsonObject>
#include <QString>

#include "BounceInPlace.h"
#include "lmms_export.h"

namespace lmms
{

class Track;

namespace control
{

//! `start`/`end` (ticks, together or neither) as a range; the refusal text, or empty when fine.
//! \a required: a region is needed (freeze.region).
LMMS_EXPORT QString parseRange(const QJsonObject& args, bool required, BounceInPlace::Range* range);
//! `out`, or the default `<project>-bounces/<trk>-<verb>[-<start>-<end>].wav`.
LMMS_EXPORT QString resolveOutPath(const QJsonObject& args, Track* track, const QString& verb,
	const BounceInPlace::Range& range);
//! A render's reply fields (path, rate, frames, bytes, sha256, the ticks it covers).
LMMS_EXPORT QJsonObject renderJson(const BounceInPlace::Result& rendered);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_BOUNCE_SUPPORT_H
