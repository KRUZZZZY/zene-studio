/*
 * ControlReversibilityTableDevices.cpp - the 0.5.0 L10 device commands' rows (and L03 / L02's) of the SPEC A16 table
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
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kDeviceRows[] = {
	R("plugin.copy", RC::TrueInverse, true, "it adds a copy of a device, with its settings, to a chain",
		"action checkpoint: the recorded undo step takes the copy out; its redo recreates it from the same settings document", ""),
	R("chain.copy", RC::TrueInverse, true, "it adds copies of a chain's devices to another chain (or replaces that chain's devices)",
		"action checkpoint: the recorded undo step takes the copies out and recreates any replaced device at its slot", ""),
	R("plugin.ab", RC::TrueInverse, true,
		"a recall or toggle puts stored settings on a device; storing writes only the session's A/B slots",
		"action checkpoint: the recorded undo step puts the device's previous settings document back", ""),
	R("plugin.organise", RC::TrueInverse, true,
		"it writes one plug-in's favourite / hidden / folder entry in the config file (plugins/organiser) - the user's "
		"interface, not the project; the previous entry is one bounded object",
		"action checkpoint: the recorded undo step writes the previous entry back", ""),
	R("plugin.organisation", RC::NotMutating, false, "it reads the organised plug-ins. Nothing is written", "no write", ""),
	R("plugin.paths_get", RC::NotMutating, false, "it reads the plug-in folders. Nothing is written", "no write", ""),
	R("plugin.paths_set", RC::TrueInverse, true,
		"it writes one format's plug-in folders in the config file (paths/<format>dirs) - a bounded list",
		"action checkpoint: the recorded undo step writes the previous folders back", ""),
	R("rack.wrap", RC::TrueInverse, true, "it moves devices of a channel's chain into a new rack chain and selects it",
		"action checkpoints: the recorded undo steps take the moved devices out of the new chain, recreate them in the "
		"channel's chain at their slots, then remove the chain and restore the selection", ""),
	R("remote.connect", RC::NotMutating, false, "it binds a MIDI controller's knobs: session state, not the project; what the "
		"knobs then move are the parameters themselves, as any MIDI controller moves them", "no write (remote.disconnect)", ""),
	R("remote.disconnect", RC::NotMutating, false, "it releases the knobs: session state", "no write (remote.connect)", ""),
	R("remote.target", RC::NotMutating, false, "it chooses the device the knobs drive: session state",
		"no write (set the previous target)", ""),
	R("remote.page", RC::NotMutating, false, "it chooses the page the knobs show: session state", "no write", ""),
	R("remote.state", RC::NotMutating, false, "it reads what the knobs drive. Nothing is written", "no write", ""),
	R("track.drum_rack", RC::TrueInverse, true, "it adds a folder of pad tracks, a channel per pad and a bus channel",
		"action checkpoint: the recorded undo step removes the rack's tracks and channels; its redo builds it again from "
		"the same pads", ""),
	// 0.5.0 L03 / L02, beside the device rows (one table for the W2 lanes after L09).
	R("transport.record_options", RC::TrueInverse, true,
		"it writes the count-in, pre-roll and record mode, a preference in the config file - three bounded scalars",
		"action checkpoint: the recorded undo step writes the previous options back, as settings.set does", ""),
	// 0.5.0 theme 7, beside the record options (the same kind of preference).
	R("transport.time_format", RC::TrueInverse, true,
		"it writes the time format musician-facing fields show, a preference in the config file - one bounded enum",
		"action checkpoint: the recorded undo step writes the previous format back, as settings.set does", ""),
	R("comp.cycle", RC::TrueInverse, true, "it switches the take a comp segment plays (the track's take lanes)",
		"ProjectJournal (Track checkpoint): the track carries its take lanes", ""),
	R("session.bindings_connect", RC::NotMutating, false, "it opens a MIDI controller for the launcher: device state, not "
		"the project; what its notes launch records as the launch commands do", "no write", ""),
	R("session.bind", RC::TrueInverse, true, "it writes one launcher binding in the config file - a bounded list",
		"action checkpoint: the recorded undo step writes the previous bindings back", ""),
	R("session.bind_learn", RC::NotMutating, false, "it waits for the next note; the binding it makes is written then",
		"no write until the note (session.unbind removes it)", ""),
	R("session.unbind", RC::TrueInverse, true, "it removes one launcher binding from the config file",
		"action checkpoint: the recorded undo step writes the previous bindings back", ""),
	R("session.bindings", RC::NotMutating, false, "it reads the launcher's bindings. Nothing is written", "no write", ""),
	// Q050-217.1: the controller bound to built-in commands.
	R("session.bind_command", RC::TrueInverse, true, "it writes one command binding in the config file - a bounded list "
		"(and takes the note off the launcher's list, in the same edit)",
		"action checkpoint: the recorded undo step writes both previous lists back", ""),
	R("session.unbind_command", RC::TrueInverse, true, "it removes one command binding from the config file",
		"action checkpoint: the recorded undo step writes the previous bindings back", ""),
	R("session.command_bindings", RC::NotMutating, false, "it reads the controller's command bindings. Nothing is written",
		"no write", ""),
	R("session.capture_to_slot", RC::TrueInverse, true,
		"it adds a pattern holding what was played and points a launcher cell at it",
		"action checkpoint: the recorded undo step restores the <session> block and removes the pattern track it made", ""),
	// 0.5.0 L02 (WF-055): audio into a launcher cell - arming and recording are a launch, landing is the edit.
	R("session.record_slot", RC::NotMutating, false, "it arms the cell's audio take and launches the cell: a launch, "
		"not a project edit (the take is landed by session.record_slot_land)", "no write (cancel: true drops the take)", ""),
	R("session.record_slot_status", RC::NotMutating, false, "it reads the take's progress. Nothing is written", "no write", ""),
	R("session.record_slot_land", RC::TrueInverse, true, "it writes the take to a WAV and points the launcher cell at it",
		"action checkpoint: the recorded undo step restores the <session> block (the WAV stays on disk)", ""),
	// 0.5.0 L10 (Q050-156): an input take into a sampler - capture is engine state, landing is the edit.
	R("record.sampler_capture", RC::NotMutating, false, "it arms a bounded input take for a sampler: engine mode "
		"state, not a project edit (the take is landed by record.sampler_capture_land)", "no write (cancel: true drops the take)", ""),
	R("record.sampler_capture_stop", RC::NotMutating, false, "it ends the take in hand: engine mode state, not a "
		"project edit", "no write", ""),
	R("record.sampler_capture_status", RC::NotMutating, false, "it reads the take's progress. Nothing is written", "no write", ""),
	R("record.sampler_capture_land", RC::TrueInverse, true, "it writes the take to a WAV and loads it into the "
		"sampler device (its sample and, for SlicerT, its slices)",
		"ProjectJournal (Track checkpoint): the device's previous sample and settings come back; the WAV stays on disk", ""),
	// 0.5.0 L01 / L11 / L12 / L14.
	R("transport.low_latency", RC::NotMutating, false, "it stops high-latency devices processing while monitoring: session "
		"state (Effect::setDontRun, neither journalled nor saved)", "no project write (enabled:false puts them back)", ""),
	R("browser.collection_save", RC::TrueInverse, true, "it writes one saved search in the collections file (library state)",
		"action checkpoint: the recorded undo step writes the previous collections back", ""),
	R("browser.collection_remove", RC::TrueInverse, true, "it removes one saved search from the collections file",
		"action checkpoint: the recorded undo step writes the previous collections back", ""),
	R("browser.collections", RC::NotMutating, false, "it reads the saved searches. Nothing is written", "no write", ""),
	R("browser.collection_run", RC::NotMutating, false, "it runs a saved search. Nothing is written", "no write", ""),
	R("track.preset_save", RC::NotMutating, false, "it writes a preset file and nothing else: the project is unchanged",
		"no write to the project (delete the file)", ""),
	R("track.preset_load", RC::TrueInverse, true, "it replaces a track's device, effects and settings",
		"ProjectJournal (Track checkpoint): the track's previous state comes back", ""),
	R("plugin.replace", RC::TrueInverse, true, "it puts another device in a device's place",
		"action checkpoints: the recorded undo steps recreate the replaced device with its settings and take the new one out", ""),
	R("range.bounce", RC::TrueInverse, true, "it lands a render of the selection on a new track and mutes the bounced tracks",
		"action checkpoint: the recorded undo step removes the landed track and puts the mutes back", ""),
	R("project.import_tracks", RC::TrueInverse, true, "it adds tracks copied from another project",
		"action checkpoint: the recorded undo step removes the imported tracks and the mixer channels made for them", ""),
	R("project.handoff", RC::NotMutating, false, "it writes a folder (a project copy, its media, a manifest); the session and "
		"its own file are untouched", "no write to the project (delete the folder)", ""),
	R("app.help", RC::NotMutating, false, "it opens the manual or the command reference: interface state", "no write", ""),
	R("app.tours", RC::NotMutating, false, "it lists the guided tours. Nothing is written", "no write", ""),
	R("app.tour", RC::NotMutating, false, "it starts a guided tour: interface state", "no write (Close ends it)", ""),
	R("window.appearance", RC::TrueInverse, true, "it writes the interface zoom, palette, accessible-mode and workspace "
		"preferences (config file) - four bounded values", "action checkpoint: the recorded undo step writes the previous appearance back", ""),
	// 0.5.0 L07 (W3): the note tools.
	R("note.invert", RC::TrueInverse, true, "it changes the keys of notes", "MidiClip journal checkpoint", ""),
	R("note.arpeggiate", RC::TrueInverse, true, "it replaces chords with runs", "MidiClip journal checkpoint", ""),
	R("note.flam", RC::TrueInverse, true, "it adds grace notes", "MidiClip journal checkpoint", ""),
	R("note.transpose_in_key", RC::TrueInverse, true, "it changes the keys of notes", "MidiClip journal checkpoint", ""),
	R("note.repeat", RC::TrueInverse, true, "it adds copies of notes", "MidiClip journal checkpoint", ""),
	R("note.generate", RC::TrueInverse, true, "it adds notes", "MidiClip journal checkpoint", ""),
	R("note.move_to_track", RC::TrueInverse, true, "it moves notes into a new clip on another track",
		"journal checkpoints of the source clip and the destination track", ""),
	R("note.split_by_pitch", RC::TrueInverse, true, "it adds one track per key", "action checkpoint: undo removes the tracks", ""),
	R("note.follow_chords", RC::TrueInverse, true, "it changes the keys of notes", "MidiClip journal checkpoint", ""),
	R("clip.quantize", RC::TrueInverse, true, "it moves clips", "journal checkpoints of the clips' tracks", ""),
	R("scale.define", RC::TrueInverse, true, "it writes the custom scales (config file)",
		"action checkpoint: the recorded undo step writes the previous scales back", ""),
	R("scale.detect", RC::NotMutating, false, "it reads a clip's notes. Nothing is written", "no write", ""),
	R("scale.custom", RC::NotMutating, false, "it lists the custom scales. Nothing is written", "no write", ""),
	R("transport.panic", RC::NotMutating, false, "it stops sounding notes: playback state, not project state", "no write", ""),
	R("note.lyrics", RC::TrueInverse, true, "it sets the notes' lyrics", "MidiClip journal checkpoint", ""),
	R("note.group", RC::TrueInverse, true, "it puts notes in a group", "MidiClip journal checkpoint", ""),
	R("note.ungroup", RC::TrueInverse, true, "it takes notes out of their groups", "MidiClip journal checkpoint", ""),
	R("track.articulations_set", RC::TrueInverse, true, "it replaces a track's articulations",
		"action checkpoint: the recorded undo step writes the previous map back", ""),
	R("track.articulations", RC::NotMutating, false, "it reads a track's articulations. Nothing is written", "no write", ""),
	R("transport.groove", RC::TrueInverse, true, "it sets the global swing (project state, two bounded values)",
		"action checkpoint: the recorded undo step writes the previous swing back", ""),
	R("workflow.record_start", RC::NotMutating, false, "it starts listening for the commands to record: interface state", "no write", ""),
	R("workflow.record_stop", RC::TrueInverse, true,
		"it saves the recorded workflow in the library in use (config file, or the project's <workspace>), as workflow.save does",
		"workflow.save's own: the recorded undo step puts the previous workflows back", ""),
	R("sample.edit_range", RC::TrueInverse, true, "it bakes an edit of a range into a clip's source - including the whole-source 'mono' downmix, whose new channel layout is part of the serialized clip state", "Clip checkpoint", ""),
	R("clip.audio_to_midi", RC::TrueInverse, true, "it adds an instrument track with the notes found in the audio",
		"action checkpoint: the recorded undo step removes the track", ""),
	R("clip.import_file", RC::TrueInverse, true, "it adds one clip (and, with no track named, the track) made from a MIDI or audio file",
		"Track checkpoint, or - when it created the track - an action checkpoint: the recorded undo step removes the track", ""),
	R("sample.spectrogram", RC::NotMutating, false, "it reads a clip's frequency content. Nothing is written", "no write", ""),
	R("track.folder_move", RC::TrueInverse, true, "it moves every clip inside a folder", "journal checkpoints of the folder's tracks", ""),
	R("clip.set_loop", RC::TrueInverse, true, "it sets a MIDI clip's content loop", "Clip journal checkpoint", ""),
	R("clip.export_musicxml", RC::NotMutating, false, "it writes a MusicXML file at the caller's path; nothing in the session changes", "no write", ""),
	R("clip.speed", RC::TrueInverse, true, "it sets an audio clip's speed ramp and speed fades", "Clip journal checkpoint", ""),
	R("clip.set_snap_point", RC::TrueInverse, true, "it sets the point inside a clip that snaps to the grid", "Clip journal checkpoint", ""),
	R("clip.set_formant", RC::TrueInverse, true, "it sets a sample clip's formant handling and stretch mode", "Clip journal checkpoint", ""),
	R("sample.crop", RC::TrueInverse, true, "it replaces a clip's source with the frames it plays", "Clip checkpoint", ""),
	R("sample.make_unique", RC::TrueInverse, true, "it gives a clip its own copy of its source", "Clip checkpoint", ""),
	R("clip.fade_curve", RC::NotMutating, false, "it reads a clip's fades and envelope. Nothing is written", "no write", ""),
	R("automation.switches", RC::TrueInverse, true, "it sets the song-wide automation read and write switches",
		"action checkpoint: the recorded undo step puts the previous switches back", ""),
	R("automation.trim_get", RC::NotMutating, false, "it reads a control's trim. Nothing is written", "no write", ""),
	R("automation.trim_set", RC::TrueInverse, true, "it sets a control's trim (runtime state, not saved)",
		"action checkpoint: the recorded undo step puts the previous trim back", ""),
	R("automation.move_point", RC::TrueInverse, true, "it moves an automation point", "AutomationClip journal checkpoint", ""),
	R("automation.write_to_end", RC::TrueInverse, true, "it writes a control's value from a tick to the song's end",
		"AutomationClip journal checkpoint (or an action step removing a created track)", ""),
	R("note.rotate", RC::TrueInverse, true, "it moves notes (or turns an audio clip's content), wrapping inside the clip", "Clip journal checkpoint (MidiClip notes, or an audio clip's loop and offset)", ""),
	R("clip.list", RC::NotMutating, false, "it lists clips. Nothing is written", "no write", ""),
	R("arrangement.next_transient", RC::NotMutating, false, "it moves the play position through transport.seek, which records itself",
		"no write of its own", ""),
	R("clip.remove_silence", RC::TrueInverse, true, "it splits a clip and deletes its quiet pieces", "Track checkpoint", ""),
	R("section.list", RC::NotMutating, false, "it lists the sections. Nothing is written", "no write", ""),
	R("section.add", RC::TrueInverse, true, "it adds a section", "action checkpoint: the recorded undo step puts the previous list back", ""),
	R("section.set", RC::TrueInverse, true, "it changes a section", "action checkpoint: the recorded undo step puts the previous list back", ""),
	R("section.remove", RC::TrueInverse, true, "it removes a section", "action checkpoint: the recorded undo step puts the previous list back", ""),
	R("section.select", RC::NotMutating, false, "it selects the section's span through range.set: interface state", "no write", ""),
	R("comp.move_edge", RC::TrueInverse, true, "it moves a boundary of the composite", "Track checkpoint", ""),
	R("comp.split_passes", RC::TrueInverse, true, "it cuts a looped take into passes on their own lanes", "Track checkpoint", ""),
	R("comp.select_group", RC::TrueInverse, true, "it changes the composite of several tracks", "Track checkpoints, merged into one step", ""),
	R("transport.metronome_options", RC::TrueInverse, true, "it writes the metronome preferences (config file)",
		"action checkpoint: the recorded undo step writes the previous options back", ""),
	R("transport.punch_here", RC::TrueInverse, true, "it sets or clears the punch region at the play position",
		"transport.punch_set's / punch_clear's own: a Timeline checkpoint", ""),
	R("record.route_source", RC::TrueInverse, true, "it points a record route at the input, the master or a channel (interface state)",
		"action checkpoint: the recorded undo step puts the previous source back", ""),
	R("record.remaining", RC::NotMutating, false, "it reads the free disk space. Nothing is written", "no write", ""),
	R("session.release_slot", RC::NotMutating, false, "it queues a cell's release: playback state, not project state", "no write", ""),
	R("session.jump_slot", RC::NotMutating, false, "it moves a playing cell's position: playback state", "no write", ""),
	R("session.capture_scene", RC::TrueInverse, true, "it adds a scene row copied from the playing cells",
		"action checkpoint: the recorded undo step restores the captured <session> block", ""),
	R("session.set_playlist", RC::TrueInverse, true, "it writes the listed scenes' follow chains",
		"action checkpoint: the recorded undo step restores the captured <session> block", ""),
	R("session.launch_group", RC::NotMutating, false, "it queues launches: playback state, not project state", "no write", ""),
	R("session.subscene_create", RC::TrueInverse, true, "it adds a sub-scene to the session model",
		"action checkpoint: the recorded undo step restores the captured <session> block", ""),
	R("session.subscene_rename", RC::TrueInverse, true, "it renames a sub-scene",
		"action checkpoint: the recorded undo step restores the captured <session> block", ""),
	R("session.subscene_delete", RC::TrueInverse, true, "it removes a sub-scene (its cells are untouched)",
		"action checkpoint: the recorded undo step restores the captured <session> block", ""),
	R("session.subscene_launch", RC::NotMutating, false, "it queues launches: playback state, not project state", "no write", ""),
	R("session.setlist", RC::TrueInverse, true, "it writes the setlist preference (config file)",
		"action checkpoint: the recorded undo step writes the previous setlist back", ""),
	R("session.setlist_go", RC::Irreversible, false, "it opens another project, as project.open does: the open one is replaced",
		"none: a project load", "go back with session.setlist_go 'previous'; unsaved changes to the replaced project are lost - save first"),
	R("plugin.prefer_format", RC::TrueInverse, true, "it writes the preferred plug-in format (config file)",
		"action checkpoint: the recorded undo step writes the previous format back", ""),
	R("mixer.options", RC::TrueInverse, true, "it sets the project's pan law", "action checkpoint: the recorded undo step writes the previous law back", ""),
	R("mixer.send_enable", RC::TrueInverse, true, "it switches a send off or on", "action checkpoint: the recorded undo step switches it back", ""),
	R("mixer.create_return", RC::TrueInverse, true, "it adds a return channel and a send to it",
		"action checkpoint: the recorded undo step deletes the channel (and with it the send)", ""),
	R("mixer.channel_eq", RC::TrueInverse, true, "it sets a channel's EQ", "action checkpoint: the recorded undo step puts the previous EQ back", ""),
	R("meter.set_reference", RC::TrueInverse, true, "it writes the calibration reference (config file)",
		"action checkpoint: the recorded undo step writes the previous reference back", ""),
	R("meter.input", RC::NotMutating, false, "it reads the input level. Nothing is written", "no write", ""),
	R("track.set_delay", RC::TrueInverse, true, "it sets an instrument track's note delay", "action checkpoint: the recorded undo step puts the previous delay back", ""),
	R("routing.usage", RC::NotMutating, false, "it reads what feeds and follows a channel. Nothing is written", "no write", ""),
	R("routing.matrix", RC::NotMutating, false, "it reads the channels and their sends. Nothing is written", "no write", ""),
	R("mixer.send_meters", RC::NotMutating, false, "it reads the sends' levels. Nothing is written", "no write", ""),
	R("mixer.eq_compare", RC::NotMutating, false, "it reads two channels' EQ settings. Nothing is written", "no write", ""),
	R("track.midi_out", RC::TrueInverse, true, "it sets an instrument track's MIDI output ports and channel", "Track journal checkpoint", ""),
	R("monitor.control_room", RC::NotMutating, false, "it changes what the speakers get - never the project or a render; trims are preferences", "no write", ""),
	R("mixer.crossfader", RC::TrueInverse, true, "it moves the crossfader or puts a channel on a side", "closure plus a model journal step", ""),
	R("browser.preview_output", RC::NotMutating, false, "it chooses where previews play, a preference in the config file; the project is untouched", "no write", ""),
	R("mixer.find", RC::NotMutating, false, "it reads channel names. Nothing is written", "no write", ""),
	R("mixer.adjust", RC::TrueInverse, true, "it moves several faders and pans", "journal checkpoints of the volume and pan models", ""),
	R("mixer.order_by_tracks", RC::TrueInverse, true, "it reorders the mixer channels",
		"action checkpoint: the recorded undo step moves them back into the previous order", ""),
	R("vca.color_members", RC::TrueInverse, true, "it colours a group's member channels",
		"action checkpoint: the recorded undo step puts each previous colour back", ""),
	R("chain.bypass_all", RC::TrueInverse, true, "it bypasses or enables every device of a chain",
		"action checkpoint: the recorded undo step puts each device's previous state back", ""),
	R("chain.clean", RC::TrueInverse, true, "it removes the bypassed devices of a chain",
		"structural checkpoints: each removal's undo recreates the device at its slot", ""),
	R("transport.midi_options", RC::TrueInverse, true, "it writes the chase and MIDI offset preferences (config file)",
		"action checkpoint: the recorded undo step writes the previous preferences back", ""),
};

#undef R

constexpr int kDeviceRowCount = static_cast<int>(sizeof(kDeviceRows) / sizeof(kDeviceRows[0]));

} // namespace

const ReversibilityRow* reversibilityDeviceRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kDeviceRowCount; }
	return kDeviceRows;
}

} // namespace control
} // namespace lmms
