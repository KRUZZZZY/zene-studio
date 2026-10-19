/*
 * NoteEditorMenus.cpp - the note editor's Notes menu: quantise, velocity, timing, groove, event list, find and change
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

#include <algorithm>
#include <functional>
#include <vector>

#include <QAction>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeySequence>
#include <QMenu>

#include "Accessibility.h"
#include "ArticulationMapEditor.h"
#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlVocabulary.h"
#include "DeprecationHelper.h"
#include "DesignTokens.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "ShellChrome.h"

namespace lmms::gui
{

namespace
{

QJsonObject merged(QJsonObject base, const QJsonObject& extra)
{
	for (auto it = extra.begin(); it != extra.end(); ++it) { base.insert(it.key(), it.value()); }
	return base;
}

//! The notes a menu acts on: the selection, or every note when @a selection is false.
std::vector<Note*> notesInScope(const MidiClip* clip, bool selection)
{
	std::vector<Note*> notes;
	for (Note* note : clip->notes())
	{
		if (!selection || note->selected()) { notes.push_back(note); }
	}
	return notes;
}

//! A note.edit setting 'muted' on @a notes.
QJsonArray muteEdits(const std::vector<Note*>& notes, bool mute)
{
	QJsonArray edits;
	for (const Note* note : notes)
	{
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("muted"), mute ? 1 : 0}});
	}
	return edits;
}

//! 0.5.0 L07 (W3): the note tools - each one command over the scope, or over the clip.
void fillToolsMenu(QMenu* menu, const QJsonObject& scoped, const QJsonObject& where)
{
	const auto run = [](const char* id, const QJsonObject& fixed, bool ask) {
		return [id, fixed, ask]() { return CommandCall{QLatin1String(id), fixed, {}, {}, ask}; };
	};
	const auto tr = [](const char* text) { return NoteEditor::tr(text); };
	addCommandAction(menu, tr("Invert"), QStringLiteral("note.invert"), run("note.invert", scoped, false));
	addCommandAction(menu, tr("Arpeggiate..."), QStringLiteral("note.arpeggiate"), run("note.arpeggiate", scoped, true));
	addCommandAction(menu, tr("Flam"), QStringLiteral("note.flam"), run("note.flam", scoped, false));
	addCommandAction(menu, tr("Rotate..."), QStringLiteral("note.rotate"), run("note.rotate", scoped, true));
	addCommandAction(menu, tr("Repeat..."), QStringLiteral("note.repeat"), run("note.repeat", scoped, true));
	addCommandAction(menu, tr("Up a Degree in Key"), QStringLiteral("note.transpose_in_key"),
		run("note.transpose_in_key", merged(scoped, {{QStringLiteral("steps"), 1}}), false));
	addCommandAction(menu, tr("Down a Degree in Key"), QStringLiteral("note.transpose_in_key"),
		run("note.transpose_in_key", merged(scoped, {{QStringLiteral("steps"), -1}}), false));
	addCommandAction(menu, tr("Follow the Chord Track"), QStringLiteral("note.follow_chords"), run("note.follow_chords", scoped, true));
	menu->addSeparator();
	addCommandAction(menu, tr("Generate a Rhythm..."), QStringLiteral("note.generate"), run("note.generate", where, true));
	addCommandAction(menu, tr("Move to Track..."), QStringLiteral("note.move_to_track"), run("note.move_to_track", scoped, true));
	addCommandAction(menu, tr("Split by Key into Tracks"), QStringLiteral("note.split_by_pitch"), run("note.split_by_pitch", where, false));
	menu->addSeparator();
	addCommandAction(menu, tr("Detect the Key"), QStringLiteral("scale.detect"),
		[where]() { return CommandCall{QStringLiteral("scale.detect"), where, {}, {}, false, true}; });
	addCommandAction(menu, tr("Define a Scale..."), QStringLiteral("scale.define"), run("scale.define", {}, true));
	menu->addSeparator();
	addCommandAction(menu, tr("Group"), QStringLiteral("note.group"), run("note.group", scoped, false));
	addCommandAction(menu, tr("Ungroup"), QStringLiteral("note.ungroup"), run("note.ungroup", scoped, false));
	addCommandAction(menu, tr("Lyrics..."), QStringLiteral("note.lyrics"), run("note.lyrics", scoped, true));
}

//! One note.edit setting @a values on every note of @a notes - one undo step.
QJsonObject editAll(const QString& clip, const std::vector<Note*>& notes, const QJsonObject& values)
{
	QJsonArray edits;
	for (const Note* note : notes)
	{
		QJsonObject entry = values;
		entry.insert(QStringLiteral("note"), control::noteIdOf(note));
		edits.append(entry);
	}
	return {{QStringLiteral("clip"), clip}, {QStringLiteral("edits"), edits}};
}

//! 0.5.0 L07: which passes the notes play on (ED-133) and the articulation they ask for (ED-121).
void fillPlayMenus(QMenu* passes, QMenu* articulations, const QString& clip, const std::vector<Note*>& notes, const InstrumentTrack* track,
	QWidget* host)
{
	const auto entry = [clip, &notes](QMenu* menu, const QString& label, const QJsonObject& values) {
		const QJsonObject args = editAll(clip, notes, values);
		addCommandAction(menu, label, QStringLiteral("note.edit"), [args]() { return CommandCall{QStringLiteral("note.edit"), args, {}, {}, false}; })
			->setEnabled(!notes.empty());
	};
	const auto tr = [](const char* text) { return NoteEditor::tr(text); };
	const struct { const char* label; int length; int mask; } patterns[] = {{"Every Pass", 0, 0}, {"First of Two", 2, 1},
		{"Second of Two", 2, 2}, {"First of Four", 4, 1}, {"Last of Four", 4, 8}, {"Not the First of Four", 4, 14}};
	for (const auto& pattern : patterns)
	{
		entry(passes, tr(pattern.label), {{QStringLiteral("recur_length"), pattern.length}, {QStringLiteral("recur_mask"), pattern.mask}});
	}
	entry(articulations, tr("None"), {{QStringLiteral("articulation"), 0}});
	const std::vector<Articulation>& map = track != nullptr ? track->articulations().entries() : std::vector<Articulation>{};
	for (std::size_t i = 0; i < map.size(); ++i) { entry(articulations, map[i].name, {{QStringLiteral("articulation"), static_cast<int>(i) + 1}}); }
	if (map.empty()) { articulations->addAction(tr("(Edit Articulations... defines them)"))->setEnabled(false); }
	if (track == nullptr) { return; }
	// Q050-015: the map's editor; its edits are track.articulations_set, which this entry declares.
	articulations->addSeparator();
	QAction* editor = articulations->addAction(tr("Edit Articulations..."));
	editor->setProperty("controlCommand", QStringLiteral("track.articulations_set"));
	const QString trackId = control::trackIdOf(track);
	QObject::connect(editor, &QAction::triggered, host, [host, trackId] { ArticulationMapEditor::open(trackId, host->window()); });
}

//! 0.5.0 FL model: the clipboard verbs and Delete, each one command over the scope (the keys Ctrl+C, Ctrl+X,
//! Ctrl+V, Ctrl+B and Delete do the same in the note area).
void fillEditMenu(QMenu* menu, const QJsonObject& scoped, const QJsonArray& selected, int grid)
{
	const auto now = [](const char* id, const QJsonObject& args) {
		return [id, args]() { return CommandCall{QLatin1String(id), args, {}, {}, false}; };
	};
	const auto label = [](const char* text, const QKeySequence& keys) {
		return NoteEditor::tr(text) + QLatin1Char('\t') + keys.toString(QKeySequence::NativeText);
	};
	const QJsonObject where{{QStringLiteral("clip"), scoped.value(QStringLiteral("clip"))}};
	addCommandAction(menu, label("Copy", QKeySequence::Copy), QStringLiteral("note.copy"), now("note.copy", scoped));
	addCommandAction(menu, label("Cut", QKeySequence::Cut), QStringLiteral("note.cut"), now("note.cut", scoped));
	addCommandAction(menu, label("Paste", QKeySequence::Paste), QStringLiteral("note.paste"), now("note.paste", where));
	addCommandAction(menu, label("Duplicate", keySequence(Qt::CTRL, Qt::Key_B)), QStringLiteral("note.duplicate"),
		now("note.duplicate", merged(scoped, {{QStringLiteral("grid"), std::max(1, grid)}})));
	addCommandAction(menu, label("Delete", QKeySequence(Qt::Key_Delete)), QStringLiteral("note.remove_many"),
		now("note.remove_many", merged(where, {{QStringLiteral("notes"), selected}})))->setEnabled(!selected.isEmpty());
}

//! A window.note_editor_set call with @a values that leaves the panel where it is.
std::function<CommandCall()> viewSet(const QJsonObject& values)
{
	QJsonObject args = values;
	args.insert(QStringLiteral("reveal"), false);
	return [args]() { return CommandCall{QStringLiteral("window.note_editor_set"), args, {}, {}, false}; };
}

//! 0.5.0 L07: the audio drawn behind the notes (ED-081).
void fillAudioBehind(QMenu* menu, const NoteEditorState& state)
{
	QAction* behind = addCommandAction(menu, NoteEditor::tr("Audio Behind the Notes"), QStringLiteral("window.note_editor_set"),
		viewSet({{QStringLiteral("audio_behind"), state.audioBehind.isEmpty() ? QStringLiteral("auto") : QString()}}));
	behind->setCheckable(true);
	behind->setChecked(!state.audioBehind.isEmpty());
}

} // namespace

QString NoteEditor::noteScope() const
{
	if (m_state.clip == nullptr) { return QStringLiteral("clip"); }
	for (const Note* note : m_state.clip->notes())
	{
		if (note->selected()) { return QStringLiteral("selection"); }
	}
	return QStringLiteral("clip");
}

void NoteEditor::addNotesMenu(QWidget* bar, QHBoxLayout* row)
{
	// Every clip-wide note command (L07), each a registered command run through its form.
	auto* button = new ShellButton(bar);
	button->setText(tr("Notes"));
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	button->setPopupMode(QToolButton::InstantPopup);
	button->setObjectName(QStringLiteral("noteEditorNotesMenu"));
	auto* menu = new QMenu(button);
	button->setMenu(menu);
	a11y::announce(button, tr("Notes"), tr("Quantise, velocity, timing, groove and the event list for the clip's notes."));
	connect(menu, &QMenu::aboutToShow, this, [this, menu]() { fillNotesMenu(menu); });
	// A key-names preset changes the keyboard's labels and the drum view's rows, not the clip.
	connect(menu, &QMenu::triggered, this, &NoteEditor::refresh);
	row->addWidget(button);
	row->addSpacing(tokens::space::md);
}

void NoteEditor::fillNotesMenu(QMenu* menu)
{
	menu->clear();
	if (m_state.clip == nullptr)
	{
		menu->addAction(tr("No clip open"))->setEnabled(false);
		return;
	}
	const QString clip = clipId();
	// What the scoped commands below act on: the selection, or the whole clip when nothing is selected.
	const bool selection = noteScope() == QStringLiteral("selection");
	const int count = static_cast<int>(notesInScope(m_state.clip, selection).size());
	menu->addAction(selection ? tr("Acting on %n selected note(s)", nullptr, count) : tr("Acting on all %n note(s) of the clip", nullptr, count))
		->setEnabled(false);
	menu->addSeparator();
	const QJsonObject where{{QStringLiteral("clip"), clip}};
	const QJsonObject scoped{{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), noteScope()}};
	QJsonArray selected;
	for (const Note* note : notesInScope(m_state.clip, true)) { selected.append(control::noteIdOf(note)); }
	fillEditMenu(menu->addMenu(tr("Edit")), scoped, selected, m_state.gridTicks);
	auto call = [](const QString& id, const QJsonObject& fixed, const QJsonObject& defaults = {}, bool ask = true) {
		return [id, fixed, defaults, ask]() { return CommandCall{id, fixed, defaults, {}, ask}; };
	};
	addCommandAction(menu, tr("Quantise..."), QStringLiteral("groove.quantize"), call(QStringLiteral("groove.quantize"),
		where, {{QStringLiteral("grid"), std::max(1, m_state.gridTicks)}, {QStringLiteral("scope"), noteScope()}}));
	fillVelocityMenu(menu->addMenu(tr("Velocity")), scoped);
	fillTimingMenu(menu->addMenu(tr("Timing")), scoped);
	fillToolsMenu(menu->addMenu(tr("Tools")), scoped, where);
	QMenu* passes = menu->addMenu(tr("Play On"));
	QMenu* articulations = menu->addMenu(tr("Articulation"));
	fillPlayMenus(passes, articulations, clip, notesInScope(m_state.clip, noteScope() == QStringLiteral("selection")),
		dynamic_cast<const InstrumentTrack*>(m_state.clip->getTrack()), this);
	fillNewLengthMenu(menu->addMenu(tr("New Note Length")));
	fillAudioBehind(menu, m_state);
	fillMuteMenu(menu->addMenu(tr("Mute")));
	fillPitchCurveMenu(menu->addMenu(tr("Pitch Curve")));
	QMenu* groove = menu->addMenu(tr("Groove"));
	addCommandAction(groove, tr("Apply Groove..."), QStringLiteral("groove.apply"), call(QStringLiteral("groove.apply"), where));
	addCommandAction(groove, tr("Extract Groove..."), QStringLiteral("groove.extract"), call(QStringLiteral("groove.extract"),
		where, {{QStringLiteral("grid"), std::max(1, m_state.gridTicks)}}));
	menu->addSeparator();
	QAction* list = menu->addAction(tr("MIDI Event List..."));
	list->setProperty("controlCommand", QStringLiteral("note.list"));
	connect(list, &QAction::triggered, this, [this, clip]() { showCommandList(this, CommandLists::noteEvents(clip)); });
	addCommandAction(menu, tr("Select Matching..."), QStringLiteral("note.select_where"),
		call(QStringLiteral("note.select_where"), where, {{QStringLiteral("filter"), QJsonObject{}}}));
	addCommandAction(menu, tr("Find && Change..."), QStringLiteral("note.transform_where"),
		call(QStringLiteral("note.transform_where"), where, {{QStringLiteral("filter"), QJsonObject{}}}));
	const QString track = control::trackIdOf(m_state.clip->getTrack());
	addCommandChoices(menu, tr("Key Names"), QStringLiteral("key_names.preset"), QStringLiteral("preset"),
		call(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), track}}, {}, false));
	const QJsonObject view{{QStringLiteral("reveal"), false}};
	addCommandChoices(menu, tr("Colour By"), QStringLiteral("window.note_editor_set"), QStringLiteral("color_mode"),
		call(QStringLiteral("window.note_editor_set"), view, {}, false));
	addCommandChoices(menu, tr("Keep in Scale"), QStringLiteral("window.note_editor_set"), QStringLiteral("keep_in_scale"),
		call(QStringLiteral("window.note_editor_set"), view, {}, false));
	menu->addSeparator();
	fillScriptsMenu(menu->addMenu(tr("Scripts")), clip);
}

void NoteEditor::fillNewLengthMenu(QMenu* menu)
{
	// 0.5.0 L07 (ED-073), FL's rule: a new note takes the last note's length - drawn, touched or resized - unless
	// a fixed length is chosen here.
	const struct { const char* label; int ticks; } lengths[] = {{"Last Note", 0}, {"1/32", 6}, {"1/16", 12}, {"1/8", 24},
		{"1/4", 48}, {"1/2", 96}, {"1/1", 192}};
	for (const auto& choice : lengths)
	{
		QAction* action = addCommandAction(menu, tr(choice.label), QStringLiteral("window.note_editor_set"),
			viewSet({{QStringLiteral("new_length"), choice.ticks}}));
		action->setCheckable(true);
		action->setChecked(m_state.newLength == choice.ticks);
	}
}

void NoteEditor::fillPitchCurveMenu(QMenu* menu)
{
	// ED-122: shapes for the notes' own pitch curves, relative to each note's length - the selection,
	// or every note when nothing is selected; one note.pitch_curve_set, one undo step.
	QJsonArray notes;
	const bool selection = noteScope() == QStringLiteral("selection");
	for (const Note* note : m_state.clip->notes())
	{
		if (!selection || note->selected()) { notes.append(control::noteIdOf(note)); }
	}
	const auto point = [](double at, double semitones) {
		return QJsonObject{{QStringLiteral("tick"), at}, {QStringLiteral("semitones"), semitones}};
	};
	QJsonArray vibrato;
	for (int i = 0; i <= 16; ++i) { vibrato.append(point(i / 16.0, i % 2 == 0 ? 0.0 : (i % 4 == 1 ? 0.3 : -0.3))); }
	const std::pair<QString, QJsonArray> shapes[] = {
		{tr("Bend Up a Tone"), {point(0, 0), point(1, 2)}}, {tr("Bend Down a Tone"), {point(0, 0), point(1, -2)}},
		{tr("Scoop In"), {point(0, -2), point(0.25, 0)}}, {tr("Fall Off"), {point(0, 0), point(0.6, 0), point(1, -5)}},
		{tr("Vibrato"), vibrato}, {tr("Clear"), {}}};
	const QString clip = clipId();
	for (const auto& [label, points] : shapes)
	{
		const QJsonObject fixed{{QStringLiteral("clip"), clip}, {QStringLiteral("notes"), notes}, {QStringLiteral("relative"), true},
			{QStringLiteral("points"), points}};
		addCommandAction(menu, label, QStringLiteral("note.pitch_curve_set"),
			[fixed]() { return CommandCall{QStringLiteral("note.pitch_curve_set"), fixed, {}, {}, false}; })->setEnabled(!notes.isEmpty());
	}
}

void NoteEditor::fillScriptsMenu(QMenu* menu, const QString& clip)
{
	// The user's saved scripts (script.list); a script finds the open clip in window.get_state's
	// note_editor, or is written here with it filled in (ED-091).
	const QJsonArray scripts = run(QStringLiteral("script.list"), {}).value(QStringLiteral("scripts")).toArray();
	for (const QJsonValue& value : scripts)
	{
		const QJsonObject script = value.toObject();
		const QJsonObject fixed{{QStringLiteral("path"), script.value(QStringLiteral("path"))}};
		addCommandAction(menu, script.value(QStringLiteral("name")).toString(), QStringLiteral("script.run"),
			[fixed]() { return CommandCall{QStringLiteral("script.run"), fixed, {}, {}, false, true}; });
	}
	if (!scripts.isEmpty()) { menu->addSeparator(); }
	addCommandAction(menu, tr("Run Script on Notes..."), QStringLiteral("script.run"), [clip]() {
		return CommandCall{QStringLiteral("script.run"), {},
			{{QStringLiteral("source"), QStringLiteral("-- the open clip is %1\nlocal clip = \"%1\"\n").arg(clip)}}, {}, true, true};
	});
}

void NoteEditor::fillVelocityMenu(QMenu* menu, const QJsonObject& scoped)
{
	const auto run = [scoped](const char* id, const QJsonObject& fixed = {}, bool ask = true) {
		return [id, scoped, fixed, ask]() { return CommandCall{QLatin1String(id), merged(scoped, fixed), {}, {}, ask}; };
	};
	addCommandAction(menu, tr("Scale..."), QStringLiteral("note.velocity_scale"), run("note.velocity_scale"));
	addCommandAction(menu, tr("Offset..."), QStringLiteral("note.velocity_offset"), run("note.velocity_offset"));
	addCommandAction(menu, tr("Softer (x0.8)"), QStringLiteral("note.velocity_scale"),
		run("note.velocity_scale", {{QStringLiteral("factor"), 0.8}}, false));
	addCommandAction(menu, tr("Louder (x1.25)"), QStringLiteral("note.velocity_scale"),
		run("note.velocity_scale", {{QStringLiteral("factor"), 1.25}}, false));
	addCommandAction(menu, tr("Humanise..."), QStringLiteral("note.randomize"), run("note.randomize"));
}

void NoteEditor::fillTimingMenu(QMenu* menu, const QJsonObject& scoped)
{
	const auto run = [scoped](const char* id, const QJsonObject& fixed = {}, bool ask = false) {
		return [id, scoped, fixed, ask]() { return CommandCall{QLatin1String(id), merged(scoped, fixed), {}, {}, ask}; };
	};
	addCommandAction(menu, tr("Double Time (x2)"), QStringLiteral("note.scale"),
		run("note.scale", {{QStringLiteral("time_factor"), 2.0}}));
	addCommandAction(menu, tr("Half Time (x0.5)"), QStringLiteral("note.scale"),
		run("note.scale", {{QStringLiteral("time_factor"), 0.5}}));
	addCommandAction(menu, tr("Scale Time..."), QStringLiteral("note.scale"), run("note.scale", {}, true));
	menu->addSeparator();
	for (int count : {2, 3, 4})
	{
		addCommandAction(menu, tr("Ratchet x%1").arg(count), QStringLiteral("note.ratchet"),
			run("note.ratchet", {{QStringLiteral("count"), count}}));
	}
	addCommandAction(menu, tr("Ratchet..."), QStringLiteral("note.ratchet"), run("note.ratchet", {}, true));
	menu->addSeparator();
	addCommandAction(menu, tr("Transpose..."), QStringLiteral("note.transpose"), run("note.transpose", {}, true));
	menu->addSeparator();
	addCommandAction(menu, tr("Restore Original Timing"), QStringLiteral("note.restore_timing"), run("note.restore_timing"));
	addCommandAction(menu, tr("Freeze Quantise"), QStringLiteral("note.restore_timing"),
		run("note.restore_timing", {{QStringLiteral("freeze"), true}}));
}

void NoteEditor::fillMuteMenu(QMenu* menu)
{
	// The selection, or every note when nothing is selected - one note.edit, one undo step.
	const std::vector<Note*> notes = notesInScope(m_state.clip, noteScope() == QStringLiteral("selection"));
	const QString clip = clipId();
	for (const bool mute : {true, false})
	{
		const QJsonObject fixed{{QStringLiteral("clip"), clip}, {QStringLiteral("edits"), muteEdits(notes, mute)}};
		QAction* action = addCommandAction(menu, mute ? tr("Mute Notes") : tr("Unmute Notes"), QStringLiteral("note.edit"),
			[fixed]() { return CommandCall{QStringLiteral("note.edit"), fixed, {}, {}, false}; });
		action->setEnabled(!notes.empty());
	}
}

} // namespace lmms::gui
