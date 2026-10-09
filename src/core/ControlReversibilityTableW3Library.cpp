/*
 * ControlReversibilityTableW3Library.cpp - the 0.5.0 L11 / L12 (W3) library and project commands' rows of the SPEC A16 table
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

const ReversibilityRow kW3LibraryRows[] = {
	// L12 (MX-170): mixer snapshots.
	R("mixer.snapshot_store", RC::TrueInverse, true, "it stores the mixer's state under a name in the project (<mixer-snapshots>)",
		"action checkpoint: the recorded undo step puts the previous snapshot list back", ""),
	R("mixer.snapshot_recall", RC::TrueInverse, true, "it writes a snapshot's faders, pans, mutes, solos and send settings onto the "
		"channels and sends that still exist",
		"action checkpoint: the recorded undo step puts the whole mixer back as it was captured just before the recall", ""),
	R("mixer.snapshot_list", RC::NotMutating, false, "it reads the stored snapshots. Nothing is written", "no write", ""),
	R("mixer.snapshot_remove", RC::TrueInverse, true, "it removes a stored snapshot",
		"action checkpoint: the recorded undo step puts the previous snapshot list back", ""),
	// L12 (WF-043 / WF-047 / WF-048 / WF-044): the project as a whole.
	R("project.settings", RC::TrueInverse, true, "it writes the tempo, time signature, master volume and pitch, pan law and "
		"document type", "action checkpoint: the recorded undo step puts every previous setting back", ""),
	R("project.metadata", RC::TrueInverse, true, "it writes the project's metadata fields (<project-info>)",
		"action checkpoint: the recorded undo step puts the previous fields back", ""),
	R("track.set_locked", RC::TrueInverse, true, "it locks or unlocks a track against editing (<edit-locks>)",
		"action checkpoint: the recorded undo step puts the previous lock back", ""),
	R("clip.set_locked", RC::TrueInverse, true, "it locks or unlocks a clip against editing (<edit-locks>)",
		"action checkpoint: the recorded undo step puts the previous lock back", ""),
	R("project.locks", RC::NotMutating, false, "it reads the locked tracks and clips. Nothing is written", "no write", ""),
	// L11 (WF-025 / WF-022): device presets and clips in the library - files outside the project.
	R("plugin.preset_remove", RC::TrueInverse, true, "it deletes one of a device's user preset files (outside the project)",
		"structural step: the recorded undo step writes the file's bytes back (counted against the undo byte budget)", ""),
	R("plugin.preset_rename", RC::TrueInverse, true, "it renames one of a device's user preset files (outside the project)",
		"action checkpoint: the recorded undo step renames it back", ""),
	R("browser.save_clip", RC::TrueInverse, true, "it writes a clip as an audio or MIDI file into the library (outside the project) "
		"and tags it", "action checkpoint: the recorded undo step removes the tags it added and deletes the file", ""),
	// L11 (WF-012 / WF-020 / WF-018 / WF-009 / WF-014 / WF-023): the library index, thumbnails, preview, navigation.
	R("browser.reindex", RC::NotMutating, false, "it rebuilds the library index - a cache in the config directory, not the "
		"project or the library itself", "no write to the project (rebuild the cache again)", ""),
	R("browser.index_status", RC::NotMutating, false, "it reads the library index's state. Nothing is written", "no write", ""),
	R("browser.index_query", RC::NotMutating, false, "it searches the library index. Nothing is written", "no write", ""),
	R("browser.similar", RC::NotMutating, false, "it ranks indexed sounds by their features. Nothing is written", "no write", ""),
	R("browser.thumbnail", RC::NotMutating, false, "it draws a waveform picture into the thumbnail cache (config directory) - "
		"not the project", "no write to the project", ""),
	R("browser.preview", RC::NotMutating, false, "it plays a file to hear it: session audio, not the project", "no write (browser.preview_stop)", ""),
	R("browser.preview_stop", RC::NotMutating, false, "it stops the preview: session audio", "no write", ""),
	R("browser.browse", RC::NotMutating, false, "it runs a browser list and remembers it in the session's history", "no write", ""),
	R("browser.history", RC::NotMutating, false, "it reads the browser's history. Nothing is written", "no write", ""),
	R("browser.history_go", RC::NotMutating, false, "it steps through the browser's history (session state)", "no write", ""),
	R("browser.sources", RC::NotMutating, false, "it reads the source remembered for each view. Nothing is written", "no write", ""),
	// Theme 3 (0.5.0 UX): Enter on a browser result.
	R("browser.load", RC::TrueInverse, true, "it adds a track holding the file: a preset's instrument, a sample in the instrument "
		"that opens it, or a sample track playing it", "action checkpoint: the recorded undo step removes the created track "
		"(control::recordCreatedTrack, as clip.slice_to_instrument's does); redo builds it again from what it was saved as", ""),
	// L12 (WF-040 / WF-046 / ED-207 / WF-042): the render queue, the task list, flattening, copying between projects.
	R("render.queue_add", RC::NotMutating, false, "it queues a render: the job writes an OUTPUT ARTEFACT from a snapshot of the "
		"session (or another file), as render.render does; the session is not modified", "no write to the project", ""),
	R("render.queue_bounce", RC::TrueInverse, true, "it queues an asynchronous render; a completed freeze, land, flatten or range bounce "
		"records one ProjectJournal step, while a stale target is refused", "the completed job's single journal step (control.undo)", ""),
	R("render.convert", RC::NotMutating, false, "it queues conversions of audio files into new files; the sources and the "
		"project are not modified", "no write to the project", ""),
	R("render.queue_list", RC::NotMutating, false, "it reads the render queue. Nothing is written", "no write", ""),
	R("render.queue_cancel", RC::NotMutating, false, "it cancels queued or running jobs: session state", "no write", ""),
	R("render.queue_clear", RC::NotMutating, false, "it forgets ended jobs: session state", "no write", ""),
	R("render.queue_report", RC::NotMutating, false, "it reads one job's report. Nothing is written", "no write", ""),
	R("app.tasks", RC::NotMutating, false, "it reads the background tasks. Nothing is written", "no write", ""),
	R("app.task_cancel", RC::NotMutating, false, "it cancels a background task: session state, not the project", "no write", ""),
	R("track.flatten", RC::TrueInverse, true, "it replaces a track with an audio track playing its rendered output",
		"structural step: the recorded undo step removes the audio track and recreates the source track from the document "
		"captured before (bounded; a track too large to capture is refused)", ""),
	R("project.copy_tracks", RC::NotMutating, false, "it writes tracks to the system clipboard, not the project", "no write", ""),
	R("project.paste_tracks", RC::TrueInverse, true, "it adds the clipboard's tracks to the project",
		"action checkpoint: the recorded undo step removes the pasted tracks; its redo pastes them again", ""),
	// L12 (WF-045 / WF-032): subprojects and alternatives.
	R("project.subproject_add", RC::TrueInverse, true, "it renders another project file and adds a track playing it, linked "
		"in <subprojects>", "action checkpoint: the recorded undo step removes the track and the link", ""),
	R("project.subprojects", RC::NotMutating, false, "it reads the subproject links. Nothing is written", "no write", ""),
	R("project.subproject_refresh", RC::TrueInverse, true, "it renders a subproject again and points its track's clips at the "
		"new file", "action checkpoint: the recorded undo step points the clips and the link back at the previous file", ""),
	R("project.alternative_save", RC::TrueInverse, true, "it writes the session to an alternative's file beside the project and "
		"lists it in <alternatives>", "action checkpoint: the recorded undo step puts the previous list back (the file stays)", ""),
	R("project.alternatives", RC::NotMutating, false, "it reads the project's alternatives. Nothing is written", "no write", ""),
	R("project.alternative_switch", RC::Irreversible, false,
		"it opens another file of the project's family - a project.open, which replaces the session",
		"none: the session is saved to its own file first, so nothing is lost - unless save_current was false or the session "
		"was never saved, when its unsaved changes are discarded",
		"switch back with project.alternative_switch to the alternative the session was (main for the main file)"),
	R("project.alternative_remove", RC::TrueInverse, true, "it takes an alternative off the list (its file stays)",
		"action checkpoint: the recorded undo step puts the previous list back", ""),
	R("browser.remember_source", RC::NotMutating, false, "it remembers the browser's source for a view: an interface preference "
		"in the config file, as window.set_view's view is", "no write to the project", ""),
	// 0.5.0 F3 (WF-107): track spacers.
	R("track.set_spacer", RC::TrueInverse, true, "it sets or removes the gap below a track in the track list (<track-spacers>)",
		"action checkpoint: the recorded undo step puts the previous spacer back", ""),
	R("track.spacers", RC::NotMutating, false, "it reads the tracks' spacers. Nothing is written", "no write", ""),
	// 0.5.0 F3 (WF-042): the reference project and switching between projects.
	R("reference.open", RC::NotMutating, false, "it parses another project file as the read-only reference: interface state, "
		"not the project", "no write (reference.close)", ""),
	R("reference.close", RC::NotMutating, false, "it closes the reference project: interface state", "no write", ""),
	R("reference.get_state", RC::NotMutating, false, "it reads the reference project. Nothing is written", "no write", ""),
	R("reference.copy_tracks", RC::TrueInverse, true, "it adds copies of the reference's tracks to the project",
		"action checkpoint: the recorded undo step removes the copied tracks; its redo copies them again", ""),
	R("reference.copy_clips", RC::TrueInverse, true, "it adds copies of the reference's clips to a track of the project",
		"ProjectJournal (Track checkpoint: Track::restoreState re-loads the track's clips as they were before the copy)", ""),
	R("reference.copy_devices", RC::TrueInverse, true, "it appends copies of the reference's devices to a chain of the project",
		"action checkpoint: the recorded undo step restores the chain's own <fxchain> document captured before the copy "
		"(bounded; a chain too large to capture is refused)", ""),
	R("project.recent", RC::NotMutating, false, "it reads the recently opened projects. Nothing is written", "no write", ""),
	R("project.switch", RC::Irreversible, false, "it opens another project - a project.open, which replaces the session",
		"none: the session is saved to its own file first when it changed, so nothing is lost - unless save_current was false",
		"switch back with project.switch (with no arguments it goes to the project just left)"),
	// 0.5.0 F3 (WF-048): a new document of a type, from its template.
	R("project.new_document", RC::Irreversible, false, "a new document replaces the whole session, exactly as project.new does, "
		"and the engine keeps no snapshot of what it replaced (unsaved edits included)", "none",
		"save first (project.save), then reopen that file with project.open"),
};

#undef R

constexpr int kW3LibraryRowCount = static_cast<int>(sizeof(kW3LibraryRows) / sizeof(kW3LibraryRows[0]));

} // namespace

const ReversibilityRow* reversibilityW3LibraryRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kW3LibraryRowCount; }
	return kW3LibraryRows;
}

} // namespace control
} // namespace lmms
