/*
 * MonitorMode.h - R2.1: the three input-monitoring states and their rule
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

#ifndef LMMS_MONITOR_MODE_H
#define LMMS_MONITOR_MODE_H

#include <QString>

namespace lmms
{

//! How a track treats its live input (Live's three monitor states).
enum class MonitorMode : int
{
	Off = 0,  //!< never heard
	Auto = 1, //!< heard while the track is armed and not playing a clip
	In = 2    //!< always heard
};

/*! The decision, pure: Off never passes, In always does, Auto passes while the track is
 *  record-armed and not playing back a clip - the Live rule the plan names (R2.1). */
constexpr bool monitorPasses(MonitorMode mode, bool armed, bool playingClip) noexcept
{
	return mode == MonitorMode::In || (mode == MonitorMode::Auto && armed && !playingClip);
}

//! The wire names (track.set_monitor, track.get_state): "off", "auto", "in".
inline QString monitorModeName(MonitorMode mode)
{
	switch (mode)
	{
		case MonitorMode::Auto: return QStringLiteral("auto");
		case MonitorMode::In: return QStringLiteral("in");
		case MonitorMode::Off: break;
	}
	return QStringLiteral("off");
}

inline bool monitorModeFromName(const QString& name, MonitorMode* mode)
{
	for (const MonitorMode candidate : {MonitorMode::Off, MonitorMode::Auto, MonitorMode::In})
	{
		if (name == monitorModeName(candidate)) { *mode = candidate; return true; }
	}
	return false;
}

} // namespace lmms

#endif // LMMS_MONITOR_MODE_H
