/*
 * ShellRecordMenu.cpp - a track's Record and Retrospective Capture submenus (the track menu's More)
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

#include "ShellRecordMenu.h"

#include <QMenu>

#include "ShellMenuParts.h"

namespace lmms::gui::ShellCommandMenus
{

using namespace menuparts;

// Moved out of ShellCommandMenus.cpp unchanged (Q050-054.1 added the MIDI take entries; that file is at the
// file-length gate's limit).
void recordMenu(QMenu* menu, const QJsonObject& self, bool instrument)
{
	QMenu* record = menu->addMenu(tr("Record"));
	add(record, "Arm", "track.set_arm", with(self, "armed", true), false);
	add(record, "Arm With Input Channel...", "track.set_arm", with(self, "armed", true), true);
	add(record, "Disarm", "track.set_arm", with(self, "armed", false), false);
	record->addSeparator();
	add(record, "Audio Input...", "record.input_set", {}, true);
	if (instrument)  // Q050-054.1: MIDI takes - armed tracks record what is played while Record runs
	{
		record->addSeparator();
		choices(record, "Record MIDI Takes", "track.set_midi_arm", "armed", self);
		add(record, "MIDI Take Status", "record.midi_capture_status", {}, false, {}, true);
		add(record, "Keep the Captured MIDI", "record.midi_capture_commit", {}, false);
	}
	QMenu* retro = menu->addMenu(tr("Retrospective Capture"));
	choices(retro, "Keep Listening", "record.retro_capture_arm", "armed", {});
	if (instrument) { add(retro, "Last MIDI to a New Clip", "midi.retro_capture_to_clip", self, false, {}, true); }
	add(retro, "Save Last Audio as a Take...", "record.retro_capture_to_take", {}, true, {}, true);
}

} // namespace lmms::gui::ShellCommandMenus
