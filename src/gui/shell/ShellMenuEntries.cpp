/*
 * ShellMenuEntries.cpp - entries the shell's command menus gained after ShellCommandMenus.cpp filled up
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
#include "ShellMenuParts.h"

#include <QMenu>

#include "AudioEngine.h"
#include "Clip.h"
#include "ClipEdits.h"
#include "CommandForm.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "Song.h"
#include "TrackVersions.h"

namespace lmms::gui::menuparts
{

void clipPlacementEntries(QMenu* menu, Clip* clip, const QJsonObject& self)
{
	QMenu* snap = menu->addMenu(tr("Snap Point"));
	const int inside = static_cast<int>(Engine::getSong()->getPlayPos().getTicks() - clip->startPosition().getTicks());
	if (inside > 0 && inside < clip->length().getTicks())
	{
		add(snap, "At the Play Position", "clip.set_snap_point", with(self, "ticks", inside), false);
	}
	add(snap, "Set the Snap Point...", "clip.set_snap_point", self, true, {{QStringLiteral("ticks"), clip->clipEdits().snapTicks}});
	if (clip->clipEdits().snapTicks > 0) { add(snap, "Snap by the Start", "clip.set_snap_point", with(self, "ticks", 0), false); }
}

void clipSpeedEntries(QMenu* menu, Clip* clip, const QJsonObject& self)
{
	const ClipEdits& edits = clip->clipEdits();
	QMenu* speed = menu->addMenu(tr("Speed"));
	add(speed, "Speed Ramp...", "clip.speed", self, true, {{QStringLiteral("from"), edits.speedFrom}, {QStringLiteral("to"), edits.speedTo}});
	const int beat = clip->length().getTicks() > 48 ? 48 : clip->length().getTicks() / 2;
	add(speed, "Tape Start", "clip.speed", with(self, "fade_in", beat), false);
	add(speed, "Tape Stop", "clip.speed", with(self, "fade_out", beat), false);
	if (edits.hasSpeed())
	{
		QJsonObject plain = self;
		for (const char* key : {"from", "to"}) { plain.insert(QLatin1String(key), 100); }
		for (const char* key : {"fade_in", "fade_out"}) { plain.insert(QLatin1String(key), 0); }
		add(speed, "Normal Speed", "clip.speed", plain, false);
	}
}

void trackVersionEntries(QMenu* menu, Track* track, const QJsonObject& self)
{
	QMenu* versions = menu->addMenu(tr("Versions"));
	const TrackVersions::Set set = TrackVersions::instance().of(track->id());
	for (const QString& name : set.clips.keys())
	{
		const QJsonObject args = with(self, "name", name);
		QAction* entry = addCommandAction(versions, name, QStringLiteral("track.version_switch"),
			[args] { return CommandCall{QStringLiteral("track.version_switch"), args, {}, QString(), false}; });
		entry->setCheckable(true);
		entry->setChecked(name == set.active);
	}
	if (!set.clips.isEmpty()) { versions->addSeparator(); }
	add(versions, "Keep as a Version...", "track.version_save", self, true, {{QStringLiteral("name"), set.clips.isEmpty() ? tr("Main") : tr("Version %1").arg(set.clips.size() + 1)}});
	if (!set.clips.isEmpty()) { add(versions, "Forget a Version...", "track.version_remove", self, true, {{QStringLiteral("name"), set.active}}); }
}

void controlRoomEntries(QMenu* menu)
{
	QMenu* room = menu->addMenu(tr("Control Room"));
	for (const char* speaker : {"A", "B", "C"})
	{
		const QJsonObject args{{QStringLiteral("speaker"), QLatin1String(speaker)}};
		addCommandAction(room, tr("Speakers %1").arg(QLatin1String(speaker)), QStringLiteral("monitor.control_room"),
			[args] { return CommandCall{QStringLiteral("monitor.control_room"), args, {}, QString(), false}; });
	}
	add(room, "Speaker Trim...", "monitor.control_room", {}, true, {{QStringLiteral("trim_db"), 0.0}});
	room->addSeparator();
	choices(room, "Dim", "monitor.control_room", "dim", {});
	choices(room, "Mono Check", "monitor.control_room", "mono", {});
	choices(room, "Cut", "monitor.control_room", "cut", {});
}

void trackMidiOutEntries(QMenu* menu, Track* track, const QJsonObject& self)
{
	if (dynamic_cast<InstrumentTrack*>(track) == nullptr) { return; }
	QMenu* out = menu->addMenu(tr("MIDI Out"));
	for (const QString& port : Engine::audioEngine()->midiClient()->writablePorts())
	{
		const QJsonObject args = with(self, "port", port);
		addCommandAction(out, port, QStringLiteral("track.midi_out"), [args] { return CommandCall{QStringLiteral("track.midi_out"), args, {}, QString(), false}; });
	}
	if (out->isEmpty()) { out->addAction(tr("No MIDI output ports"))->setEnabled(false); }
	out->addSeparator();
	add(out, "Channel...", "track.midi_out", self, true, {{QStringLiteral("channel"), 1}});
	add(out, "Stop Sending MIDI Out", "track.midi_out", with(self, "enabled", false), false);
}

} // namespace lmms::gui::menuparts
