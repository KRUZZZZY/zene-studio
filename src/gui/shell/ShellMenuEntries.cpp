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

#include <QAction>
#include <QFileDialog>
#include <QMenu>
#include <QPointer>

#include "AudioEngine.h"
#include "Clip.h"
#include "ClipEdits.h"
#include "CommandForm.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "CrossfadeEditorDialog.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
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

void clipScoreEntries(QMenu* menu, const QJsonObject& self)
{
	QAction* score = menu->addAction(tr("Export as MusicXML..."));
	score->setProperty("controlCommand", QStringLiteral("clip.export_musicxml"));
	QObject::connect(score, &QAction::triggered, menu, [menu, self] {
		const QString path = QFileDialog::getSaveFileName(menu->parentWidget(), tr("Export as MusicXML"), QString(), tr("MusicXML (*.musicxml)"));
		if (path.isEmpty()) { return; }
		runCommand(menu->parentWidget(), CommandCall{QStringLiteral("clip.export_musicxml"),
			with(self, "path", path.endsWith(QLatin1String(".musicxml")) ? path : path + QStringLiteral(".musicxml")), {}, QString(), false});
	});
}

void crossfaderEntries(QMenu* menu, const QJsonObject& channel)
{
	choices(menu, "Crossfader Side", "mixer.crossfader", "side", channel);
	add(menu, "Crossfader Position...", "mixer.crossfader", {}, true, {{QStringLiteral("position"), 0.0}});
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
	if (out->isEmpty())
	{
		QAction* none = out->addAction(tr("No MIDI output ports"));
		none->setProperty("controlCommand", QStringLiteral("track.midi_out"));  // what it would run, as every entry declares
		none->setEnabled(false);
	}
	out->addSeparator();
	add(out, "Channel...", "track.midi_out", self, true, {{QStringLiteral("channel"), 1}});
	add(out, "Stop Sending MIDI Out", "track.midi_out", with(self, "enabled", false), false);
}

void clipLoopEntries(QMenu* menu, Clip* clip, const QJsonObject& self)
{
	if (dynamic_cast<MidiClip*>(clip) != nullptr) { add(menu, "Loop the Content...", "clip.set_loop", self, true, {{QStringLiteral("length"), 192}}); }
	if (auto* audio = dynamic_cast<SampleClip*>(clip))  // the whole audio by default, from the in-point
	{
		add(menu, "Loop the Content...", "clip.set_loop", self, true, {{QStringLiteral("start"), 0},
			{QStringLiteral("length"), static_cast<int>(audio->sampleLength().getTicks())}});
	}
}

void crossfadeEditorEntries(QMenu* menu, Clip* clip)
{
	QAction* editor = menu->addAction(tr("Crossfade Editor..."));
	editor->setProperty("controlCommand", QStringLiteral("clip.crossfade"));
	const QPointer<SampleClip> self = dynamic_cast<SampleClip*>(clip);
	const QPointer<SampleClip> other = CrossfadeEditorDialog::overlapping(clip);
	editor->setEnabled(!self.isNull() && !other.isNull());
	QObject::connect(editor, &QAction::triggered, menu, [self, other, menu] {
		if (self && other) { CrossfadeEditorDialog::openFor(self, other, menu->parentWidget()); }
	});
}

namespace
{

//! The manual delay of @a target (a track or a channel), its stored value in the entry's name.
void offsetEntry(QMenu* menu, const QString& label, const QString& target, int samples)
{
	QAction* action = menu->addAction(label.arg(samples));
	action->setProperty("controlCommand", QStringLiteral("pdc.set_offset"));
	QObject::connect(action, &QAction::triggered, menu, [target, samples, menu] {
		runCommand(menu->parentWidget(), CommandCall{QStringLiteral("pdc.set_offset"), {{QStringLiteral("target"), target}},
			{{QStringLiteral("offset_samples"), samples}}, QString(), true});
	});
}

} // namespace

void trackDelayEntries(QMenu* menu, Track* track, const QString& id)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
	{
		offsetEntry(menu, tr("Track Delay (%1 samples)..."), id, instrument->audioBusHandle()->offsetFrames());
	}
	else if (auto* sample = dynamic_cast<SampleTrack*>(track))
	{
		offsetEntry(menu, tr("Track Delay (%1 samples)..."), id, sample->audioBusHandle()->offsetFrames());
	}
}

void channelDelayEntries(QMenu* menu, const QString& channel)
{
	ControlResult ignored;
	const MixerChannel* resolved = control::resolveMixerChannel(channel, &ignored);
	if (resolved != nullptr && resolved->index() != 0) { offsetEntry(menu, tr("Channel Delay (%1 samples)..."), channel, resolved->offsetFrames()); }
}

void stripEntries(QMenu* strip, const QString& channel)
{
	const QJsonObject self{{QStringLiteral("channel"), channel}};
	choices(strip, "Polarity Left", "mixer.set_strip", "polarity_left", self);
	choices(strip, "Polarity Right", "mixer.set_strip", "polarity_right", self);
	choices(strip, "Mono", "mixer.set_strip", "mono", self);
	choices(strip, "Swap Left and Right", "mixer.set_strip", "swap", self);  // MX-033
	choices(strip, "Mono Input", "mixer.set_strip", "input_mono", self);  // MX-059
	add(strip, "EQ...", "mixer.channel_eq", self, true);  // MX-085
	choices(strip, "Pan Mode", "mixer.set_strip", "pan_mode", self);
	choices(strip, "Solo Safe", "mixer.set_strip", "solo_safe", self);
	choices(strip, "Meter Mode", "mixer.set_strip", "meter_mode", self);  // Q050-108: this strip's own meter
	add(strip, "Trim, Width and Note...", "mixer.set_strip", self, true);
	channelDelayEntries(strip, channel);  // Q050-339
	add(strip, "Meters", "mixer.strip_get", self, false, {}, true);
	add(strip, "Clear All Clip Lights", "mixer.clear_clips", {}, false);
}

} // namespace lmms::gui::menuparts
