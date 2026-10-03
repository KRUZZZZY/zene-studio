/*
 * ControlCommandsNoteEditor.cpp - window.note_editor_set: the note editor's view, over the socket
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "NoteEditor.h"

namespace lmms
{

using namespace control;

namespace
{

const QStringList kTools{QStringLiteral("draw"), QStringLiteral("select"), QStringLiteral("slice"),
	QStringLiteral("erase"), QStringLiteral("audition"), QStringLiteral("paint"), QStringLiteral("mute")};
const QStringList kFolds{QStringLiteral("none"), QStringLiteral("scale"), QStringLiteral("used"), QStringLiteral("drums")};
const QStringList kColors{QStringLiteral("velocity"), QStringLiteral("pitch"), QStringLiteral("track")};

QStringList laneIds()
{
	QStringList ids;
	for (const gui::NoteLaneSpec& spec : gui::notelanes::all()) { ids << spec.id; }
	return ids;
}

//! Open @a args' clip in the editor; false (with @a error) when it names no MIDI clip.
bool applyClip(gui::NoteEditor* editor, const QJsonObject& args, ControlResult* error)
{
	if (!args.contains(QStringLiteral("clip"))) { return true; }
	ClipRef ref;
	MidiClip* clip = resolveMidiClip(args.value(QStringLiteral("clip")).toString(), &ref, error);
	if (clip == nullptr) { return false; }
	editor->setClip(clip);
	return true;
}

//! The index of @a args' @a key in @a names, or -1 when absent or unknown (the schema refuses
//! an unknown value before the handler runs; this is the belt to that brace).
int choice(const QJsonObject& args, const char* key, const QStringList& names)
{
	return names.indexOf(args.value(QLatin1String(key)).toString());
}

void applyChoices(gui::NoteEditor* editor, const QJsonObject& args)
{
	if (const int tool = choice(args, "tool", kTools); tool >= 0) { editor->setTool(static_cast<gui::NoteTool>(tool)); }
	if (const int fold = choice(args, "fold", kFolds); fold >= 0) { editor->setFold(static_cast<gui::NoteFold>(fold)); }
	gui::NoteLane lane = gui::NoteLane::Velocity;
	if (gui::notelanes::fromId(args.value(QStringLiteral("lane")).toString(), lane)) { editor->setLane(lane); }
	if (args.contains(QStringLiteral("grid"))) { editor->setGridTicks(args.value(QStringLiteral("grid")).toInt()); }
	if (args.contains(QStringLiteral("ghosts"))) { editor->setGhosts(args.value(QStringLiteral("ghosts")).toBool()); }
	if (const int color = choice(args, "color_mode", kColors); color >= 0) { editor->setColorMode(static_cast<gui::NoteColor>(color)); }
	if (args.contains(QStringLiteral("keep_in_scale"))) { editor->setKeepInScale(args.value(QStringLiteral("keep_in_scale")).toBool()); }
	if (args.contains(QStringLiteral("zoom"))) { editor->setZoom(args.value(QStringLiteral("zoom")).toDouble()); }
	if (args.value(QStringLiteral("fit")).toBool()) { editor->zoomToFit(); }
}

ControlResult noteEditorSet(const QJsonObject& args)
{
	gui::GuiApplication* g = gui::getGUI();
	if (g == nullptr || g->mainWindow() == nullptr || g->noteEditor() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Requires,
			QStringLiteral("command 'window.note_editor_set' requires an interface: this instance has no main window"));
	}
	gui::NoteEditor* editor = g->noteEditor();
	ControlResult error;
	if (!applyClip(editor, args, &error)) { return error; }
	if (args.value(QStringLiteral("reveal")).toBool(true)) { g->mainWindow()->revealPanel(QStringLiteral("detail")); }
	applyChoices(editor, args);
	return ControlResult::success(editor->describe());
}

} // namespace

QJsonObject noteEditorState()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr && g->noteEditor() != nullptr ? g->noteEditor()->describe() : QJsonObject();
}

void registerNoteEditorWindowCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("window");
	cmd.verb = QStringLiteral("note_editor_set");
	cmd.id = QStringLiteral("window.note_editor_set");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.description = QStringLiteral("Drive the note editor's VIEW - what the editor's header and keys set - "
		"and answer its state (the same object window.get_state reports as `note_editor`). Every field is "
		"optional: `clip` opens that MIDI clip in the editor (what double-clicking it does); `tool` is draw, "
		"select, slice, erase, audition, paint (drag to lay a note on every grid cell) or mute; `grid` the snap in ticks "
		"(1 = off); `fold` none, scale, used or drums (the keys the track names - key_names.* - and the used ones); "
		"`lane` the expression lane (velocity, pan, chance, timbre, pressure, pitch); `ghosts` shows the "
		"track's other clips; `color_mode` colours notes by velocity, pitch or track; `keep_in_scale` moves "
		"drawn and dragged notes to the nearest key of the scale; `zoom` is pixels per tick; `fit` zooms to the clip and centres its notes "
		"(after the panel is shown); `reveal` (default true) brings the note editor "
		"panel into view. Interface state, not project state: nothing is journalled - the notes themselves "
		"are edited through note.*. Refused `requires` in a process with no interface.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("tool"), enumProperty(kTools)},
		{QStringLiteral("grid"), integerProperty(1, 192 * 4)},
		{QStringLiteral("fold"), enumProperty(kFolds)},
		{QStringLiteral("lane"), enumProperty(laneIds())},
		{QStringLiteral("ghosts"), booleanProperty()},
		{QStringLiteral("color_mode"), enumProperty(kColors)},
		{QStringLiteral("keep_in_scale"), booleanProperty()},
		{QStringLiteral("zoom"), numberProperty()},
		{QStringLiteral("reveal"), booleanProperty()},
		{QStringLiteral("fit"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("tool"), stringProperty()},
		{QStringLiteral("grid"), integerProperty(1, 192 * 4)}, {QStringLiteral("fold"), stringProperty()},
		{QStringLiteral("rows"), integerProperty(0, 128)}, {QStringLiteral("lane"), stringProperty()},
		{QStringLiteral("ghosts"), booleanProperty()}, {QStringLiteral("scale_highlight"), booleanProperty()},
		{QStringLiteral("scale"), stringProperty()}, {QStringLiteral("zoom"), numberProperty()},
		{QStringLiteral("color_mode"), stringProperty()}, {QStringLiteral("keep_in_scale"), booleanProperty()},
		{QStringLiteral("scroll_ticks"), numberProperty()}, {QStringLiteral("scroll_y"), integerProperty(0, 1 << 20)},
		{QStringLiteral("top_key"), integerProperty(-1, 127)}, {QStringLiteral("visible_notes"), integerProperty(0, 1919808)},
	});
	cmd.handler = noteEditorSet;
	registry.registerCommand(cmd);
}

} // namespace lmms
