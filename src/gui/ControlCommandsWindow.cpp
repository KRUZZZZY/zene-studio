/*
 * ControlCommandsWindow.cpp - the `window.*` group (include/ControlWindowCommands.h)
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

#include "ControlWindowCommands.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMdiSubWindow>
#include <QWidget>

#include "AutomationEditor.h"
#include "ControllerRackView.h"
#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MicrotunerConfig.h"
#include "MixerView.h"
#include "PatternEditor.h"
#include "PianoRoll.h"
#include "ProjectNotes.h"
#include "SongEditor.h"

namespace lmms
{

using namespace control;

namespace
{

//! One editor: its wire name, its widget, and the MainWindow slot that toggles
//! it (the one implementation the menu, the toolbar and the socket share).
struct EditorEntry
{
	const char* name;
	QWidget* (*widget)(gui::GuiApplication*);
	void (gui::MainWindow::*toggle)();
};

void togglePatternEditor(gui::MainWindow* window) { window->togglePatternEditorWin(); }

const EditorEntry kEditors[] = {
	{"song", [](gui::GuiApplication* g) -> QWidget* { return g->songEditor(); },
		&gui::MainWindow::toggleSongEditorWin},
	// togglePatternEditorWin takes a defaulted bool, so it is called through
	// the wrapper below rather than as a member pointer.
	{"pattern", [](gui::GuiApplication* g) -> QWidget* { return g->patternEditor(); }, nullptr},
	{"piano_roll", [](gui::GuiApplication* g) -> QWidget* { return g->pianoRoll(); },
		&gui::MainWindow::togglePianoRollWin},
	{"automation", [](gui::GuiApplication* g) -> QWidget* { return g->automationEditor(); },
		&gui::MainWindow::toggleAutomationEditorWin},
	{"mixer", [](gui::GuiApplication* g) -> QWidget* { return g->mixerView(); },
		&gui::MainWindow::toggleMixerWin},
	{"controller_rack", [](gui::GuiApplication* g) -> QWidget* { return g->getControllerRackView(); },
		&gui::MainWindow::toggleControllerRack},
	{"project_notes", [](gui::GuiApplication* g) -> QWidget* { return g->getProjectNotes(); },
		&gui::MainWindow::toggleProjectNotesWin},
	// M3.2: Edit > Scales and keymaps dispatches this editor too.
	{"microtuner", [](gui::GuiApplication* g) -> QWidget* { return g->getMicrotunerConfig(); },
		&gui::MainWindow::toggleMicrotunerWin},
	// R5.3: the clip-launch grid (SessionGridView), created by its first toggle.
	{"clip_launcher", [](gui::GuiApplication* g) -> QWidget* {
		return g->mainWindow() != nullptr ? g->mainWindow()->clipLauncher() : nullptr; },
		&gui::MainWindow::toggleClipLauncherWin},
};

const EditorEntry* findEditor(const QString& name)
{
	for (const EditorEntry& entry : kEditors)
	{
		if (name == QLatin1String(entry.name)) { return &entry; }
	}
	return nullptr;
}

//! Showing = the editor's sub-window is not hidden. An editor Focus Desk has
//! mounted lives outside its sub-window; it is showing while it is visible.
bool isShowing(QWidget* widget)
{
	if (widget == nullptr) { return false; }
	if (auto* sub = qobject_cast<QMdiSubWindow*>(widget->parentWidget())) { return !sub->isHidden(); }
	return widget->isVisible();
}

ControlResult noInterface(const QString& id)
{
	return ControlResult::failure(ControlErrorKind::Requires,
		QStringLiteral("command '%1' requires an interface: this instance has no main window").arg(id));
}

QJsonObject editorState(gui::GuiApplication* g, const EditorEntry& entry)
{
	return QJsonObject{{QStringLiteral("editor"), QLatin1String(entry.name)},
		{QStringLiteral("visible"), isShowing(entry.widget(g))}};
}

ControlCommand command(const char* verb)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("window");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.mutating = false;
	cmd.requiresEngine = false;
	return cmd;
}

} // namespace

QStringList windowEditorNames()
{
	QStringList names;
	for (const EditorEntry& entry : kEditors) { names << QLatin1String(entry.name); }
	return names;
}

void dispatchWindowToggle(const QString& editor)
{
	ControlRegistry::instance()->invoke(QStringLiteral("window.toggle"),
		QJsonObject{{QStringLiteral("editor"), editor}});
}

void registerWindowCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd = command("toggle");
		cmd.description = QStringLiteral("Show or hide one editor's window - the View menu's "
			"Ctrl+1..7 items, Edit > Scales and keymaps and the window toolbar dispatch exactly this. "
			"`editor` is one of song, pattern, piano_roll, automation, mixer, controller_rack, "
			"project_notes, microtuner, clip_launcher. A "
			"hidden or unfocused editor is brought forward (and back into view if it was off "
			"screen); the focused one is hidden. `visible` is the editor's state afterwards. "
			"Interface state, not project state: nothing is journalled. Refused `requires` in a "
			"process with no interface.");
		cmd.argsSchema = objectSchema({{QStringLiteral("editor"), enumProperty(windowEditorNames())}},
			QJsonArray{QStringLiteral("editor")});
		cmd.resultSchema = objectSchema({{QStringLiteral("editor"), stringProperty()},
			{QStringLiteral("visible"), booleanProperty()}});
		cmd.handler = [](const QJsonObject& args) {
			gui::GuiApplication* g = gui::getGUI();
			if (g == nullptr || g->mainWindow() == nullptr) { return noInterface(QStringLiteral("window.toggle")); }
			const EditorEntry* entry = findEditor(args.value(QStringLiteral("editor")).toString());
			if (entry == nullptr)
			{
				return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("unknown editor"));
			}
			if (entry->toggle != nullptr) { (g->mainWindow()->*entry->toggle)(); }
			else { togglePatternEditor(g->mainWindow()); }
			return ControlResult::success(editorState(g, *entry));
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd = command("get_state");
		cmd.description = QStringLiteral("Which editors' windows are showing: `editors` is one "
			"{editor, visible} entry per window.toggle editor, in the menu's Ctrl+1..7 order. "
			"Read-only. Refused `requires` in a process with no interface.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({{QStringLiteral("editors"), arrayProperty()}});
		cmd.handler = [](const QJsonObject&) {
			gui::GuiApplication* g = gui::getGUI();
			if (g == nullptr || g->mainWindow() == nullptr) { return noInterface(QStringLiteral("window.get_state")); }
			QJsonArray editors;
			for (const EditorEntry& entry : kEditors) { editors.append(editorState(g, entry)); }
			return ControlResult::success(QJsonObject{{QStringLiteral("editors"), editors}});
		};
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
