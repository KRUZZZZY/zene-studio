/*
 * ShellCommandMenus.h - what a track, a clip and a mixer channel offer: registered commands
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

#ifndef LMMS_GUI_SHELL_COMMAND_MENUS_H
#define LMMS_GUI_SHELL_COMMAND_MENUS_H

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms
{
class Clip;
class Track;
}

namespace lmms::gui
{

/*! The 0.5.0 W1 surface (plans/BACKLOG-050-FEATURES.md): the capabilities the control surface
 *  had and the interface did not, offered where a person looks for them - the Inspector's
 *  section menus and the arranger's context menus fill their menus from here, so a track,
 *  clip or channel offers the same thing wherever it is clicked. Every entry runs a registered
 *  command through CommandForm (SPEC A11): a form where it takes values, at once otherwise. */
namespace ShellCommandMenus
{

//! Recording (arm, input, retrospective capture), take lanes and comping, rendering (bounce
//! to a new track, bounce in place, freeze) and, on a sample track, a test-signal clip.
LMMS_EXPORT void populateTrack(QMenu* menu, Track* track);

//! Position, links and edit groups for any clip; warp, stretch, detection, destructive
//! processing and crossfades for a sample clip; scale, chords, groove and the random seed for
//! a MIDI clip.
LMMS_EXPORT void populateClip(QMenu* menu, Clip* clip);

//! Sends (pre or post fader), a sidechain into another channel's devices, rack zones and the
//! chain's wiring, the loudness meter.
LMMS_EXPORT void populateChannel(QMenu* menu, const QString& channel);

//! The project's own surfaces (the header's Project Tools menu): revisions, files and
//! relinking, collected media, the plug-ins in the project, groups, grooves, auto-mastering,
//! the loudness meter, safe start.
LMMS_EXPORT void populateProject(QMenu* menu);

} // namespace ShellCommandMenus

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_COMMAND_MENUS_H
