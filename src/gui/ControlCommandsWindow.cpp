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
#include <QWidget>

#include "ControllerRackView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MicrotunerConfig.h"
#include "PerformView.h"
#include "PianoRoll.h"
#include "ProjectNotes.h"
#include "SampleEditorDialog.h"
#include "Shell.h"
#include "ShellToolsMenu.h"

namespace lmms
{

using namespace control;

namespace
{

//! One `editor` value: either a shell panel (docs/UI-SHELL.md) or a floating window. The MDI
//! era's names stay valid and resolve to the panel each editor became - song is the
//! arranger, piano_roll the note editor ("detail"), clip_launcher the launcher - so a driver
//! written against 0.4.0 still works.
struct EditorEntry
{
	const char* name;
	const char* panel;                         //!< shell panel id, or nullptr for a window
	QWidget* (*window)(gui::GuiApplication*);  //!< the floating window, when not a panel
	void (gui::MainWindow::*toggle)();
	void (*toggleWindow)() = nullptr;          //!< a window that is not MainWindow's: its own toggle
};

const EditorEntry kEditors[] = {
	{"song", "arranger", nullptr, nullptr},
	{"pattern", "pattern", nullptr, nullptr},
	{"piano_roll", "detail", nullptr, nullptr},
	{"automation", "automation", nullptr, nullptr},
	{"mixer", "mixer", nullptr, nullptr},
	{"controller_rack", nullptr, [](gui::GuiApplication* g) -> QWidget* { return g->getControllerRackView(); },
		&gui::MainWindow::toggleControllerRack},
	{"project_notes", nullptr, [](gui::GuiApplication* g) -> QWidget* { return g->getProjectNotes(); },
		&gui::MainWindow::toggleProjectNotesWin},
	// M3.2: Edit > Scales and keymaps dispatches this editor too.
	{"microtuner", nullptr, [](gui::GuiApplication* g) -> QWidget* { return g->getMicrotunerConfig(); },
		&gui::MainWindow::toggleMicrotunerWin},
	{"clip_launcher", "launcher", nullptr, nullptr},
	// 0.5.0: the classic piano roll, a floating window beside the note editor (NOTE-EDITOR.md §7).
	{"classic_piano_roll", nullptr, [](gui::GuiApplication* g) -> QWidget* { return g->pianoRoll(); },
		&gui::MainWindow::toggleClassicPianoRollWin},
	// 0.5.0: every shell panel by its own id.
	{"arranger", "arranger", nullptr, nullptr},
	{"launcher", "launcher", nullptr, nullptr},
	{"detail", "detail", nullptr, nullptr},
	{"devices", "devices", nullptr, nullptr},
	{"inspector", "inspector", nullptr, nullptr},
	{"browser", "browser", nullptr, nullptr},
	// 0.5.0 L02 (WF-071): perform mode, the launcher's live show window (PerformView.h).
	{"perform", nullptr, [](gui::GuiApplication*) -> QWidget* { return gui::PerformView::instance(false); }, nullptr,
		&gui::PerformView::toggle},
};

const EditorEntry* findEditor(const QString& name)
{
	for (const EditorEntry& entry : kEditors)
	{
		if (name == QLatin1String(entry.name)) { return &entry; }
	}
	return nullptr;
}

bool isShowing(gui::GuiApplication* g, const EditorEntry& entry)
{
	if (entry.panel != nullptr)
	{
		gui::Shell* shell = g->mainWindow()->shell();
		return shell->model().isVisible(QLatin1String(entry.panel)) || shell->isDetached(QLatin1String(entry.panel));
	}
	// A floating window is its own top level: it is showing while it is visible.
	QWidget* widget = entry.window(g);
	return widget != nullptr && widget->isVisible();
}

QJsonObject editorState(gui::GuiApplication* g, const EditorEntry& entry)
{
	return QJsonObject{{QStringLiteral("editor"), QLatin1String(entry.name)},
		{QStringLiteral("visible"), isShowing(g, entry)},
		{QStringLiteral("panel"), entry.panel != nullptr ? QLatin1String(entry.panel) : QLatin1String("")}};
}

ControlCommand command(const char* verb)
{
	return guiCommand("window", verb);
}

gui::GuiApplication* interfaceOrNull()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr && g->mainWindow() != nullptr && g->mainWindow()->shell() != nullptr ? g : nullptr;
}

ControlResult toggle(const QJsonObject& args)
{
	gui::GuiApplication* g = interfaceOrNull();
	if (g == nullptr) { return noInterface(QStringLiteral("window.toggle")); }
	const EditorEntry* entry = findEditor(args.value(QStringLiteral("editor")).toString());
	if (entry == nullptr) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("unknown editor")); }
	if (entry->panel != nullptr) { g->mainWindow()->togglePanel(QLatin1String(entry->panel)); }
	else if (entry->toggleWindow != nullptr) { entry->toggleWindow(); }
	else { (g->mainWindow()->*entry->toggle)(); }
	QJsonObject state = editorState(g, *entry);
	state.insert(QStringLiteral("view"), gui::ShellLayout::viewName(g->mainWindow()->shell()->model().view()));
	return ControlResult::success(state);
}

ControlResult getState(const QJsonObject&)
{
	gui::GuiApplication* g = interfaceOrNull();
	if (g == nullptr) { return noInterface(QStringLiteral("window.get_state")); }
	QJsonArray editors;
	for (const EditorEntry& entry : kEditors) { editors.append(editorState(g, entry)); }
	QJsonObject state = g->mainWindow()->shell()->state();
	state.insert(QStringLiteral("editors"), editors);
	state.insert(QStringLiteral("tools"), gui::shelltools::state());
	state.insert(QStringLiteral("note_editor"), noteEditorState());
	state.insert(QStringLiteral("selection"), shellSelectionState());
	return ControlResult::success(state);
}

ControlResult pin(const QJsonObject& args)
{
	gui::GuiApplication* g = interfaceOrNull();
	if (g == nullptr) { return noInterface(QStringLiteral("window.pin")); }
	const QString panel = args.value(QStringLiteral("panel")).toString();
	const bool pinned = args.value(QStringLiteral("pinned")).toBool(true);
	if (!g->mainWindow()->shell()->setPanelPinned(panel, pinned))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a side panel that can be pinned").arg(panel));
	}
	return ControlResult::success({{QStringLiteral("panel"), panel}, {QStringLiteral("pinned"), pinned}});
}

ControlResult setView(const QJsonObject& args)
{
	gui::GuiApplication* g = interfaceOrNull();
	if (g == nullptr) { return noInterface(QStringLiteral("window.set_view")); }
	gui::ShellView view = gui::ShellView::Arrange;
	if (!gui::ShellLayout::viewFromName(args.value(QStringLiteral("view")).toString(), view))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("unknown view"));
	}
	gui::Shell* shell = g->mainWindow()->shell();
	const bool changed = shell->model().view() != view;
	shell->setView(view);
	return ControlResult::success(QJsonObject{
		{QStringLiteral("view"), gui::ShellLayout::viewName(view)}, {QStringLiteral("changed"), changed}});
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
		cmd.description = QStringLiteral("Show or hide one shell panel or floating window - the View "
			"menu, the footer's panel buttons and Tools menu, a region's tabs, Alt+I/B/D/E/M/A/P/L, the MDI-era Ctrl+1..7 and Edit > "
			"Scales and keymaps dispatch exactly this. `editor` is a panel id (arranger, launcher, "
			"detail, devices, inspector, browser, mixer, automation, pattern), a 0.4.0 editor name "
			"(song = arranger, piano_roll = detail, clip_launcher = launcher), or a floating window "
			"(controller_rack, project_notes, microtuner, classic_piano_roll, perform - perform mode, the launcher's live show "
			"window). A panel shares its region with others, so "
			"showing it hides the region's previous panel; a view's main panel cannot be hidden (it "
			"stays visible); a panel the current view does not offer is revealed by switching to the "
			"Arrange view. `visible` is the state afterwards, `panel` the panel id (empty for a "
			"window), `view` the view afterwards. Interface state, not project state: nothing is "
			"journalled. Refused `requires` in a process with no interface.");
		cmd.argsSchema = objectSchema({{QStringLiteral("editor"), enumProperty(windowEditorNames())}},
			QJsonArray{QStringLiteral("editor")});
		cmd.resultSchema = objectSchema({{QStringLiteral("editor"), stringProperty()},
			{QStringLiteral("visible"), booleanProperty()}, {QStringLiteral("panel"), stringProperty()},
			{QStringLiteral("view"), stringProperty()}});
		cmd.handler = toggle;
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd = command("get_state");
		cmd.description = QStringLiteral("The shell's layout: `view` (arrange, mix or edit); `panels`, "
			"one {id, title, region, visible, available, shortcut, mounted} per panel, where region is "
			"left, center, bottom or right and available says whether the current view offers it; "
			"`focused`, the panel that last took keyboard focus; `launcher`, whether the clip launcher "
			"sits beside the view's main panel; `editors`, one {editor, visible, panel} entry per "
			"window.toggle name; `tools`, one {name, command, editor, home} per tool that opens in a window of its own (the "
			"footer's Tools menu: the command that opens it, and its window.toggle editor when it has one); `regions`, per side "
			"region (left, bottom, right) the panel `open` there and the `tabs` its tab strip offers in this view; `footer`, how "
			"the footer's switches fit its width (labelled, compact, or overflowing into its More button); `note_editor`, the "
			"note editor's view (window.note_editor_set's reply); and "
			"`selection`, what the Inspector and the Device panel show (window.select's reply). Read-only. Refused `requires` in a process with no interface.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({{QStringLiteral("editors"), arrayProperty()},
			{QStringLiteral("view"), stringProperty()}, {QStringLiteral("panels"), arrayProperty()},
			{QStringLiteral("focused"), stringProperty()}, {QStringLiteral("launcher"), booleanProperty()},
			{QStringLiteral("note_editor"), objectProperty()}, {QStringLiteral("selection"), objectProperty()},
			{QStringLiteral("tools"), arrayProperty()}, {QStringLiteral("regions"), objectProperty()},
			{QStringLiteral("footer"), stringProperty()}});
		cmd.handler = getState;
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd = command("set_view");
		cmd.description = QStringLiteral("Switch the shell's view - the footer's view words, View > "
			"Arrange/Mix/Edit and Alt+1/2/3 dispatch exactly this. `view` is arrange (the arranger "
			"timeline in the centre), mix (the mixer) or edit (the note editor); each view keeps its "
			"own side panels. `changed` is false when the view was already showing. Interface state, "
			"not project state: nothing is journalled. Refused `requires` in a process with no "
			"interface.");
		cmd.argsSchema = objectSchema({{QStringLiteral("view"), enumProperty(gui::ShellLayout::viewNames())}},
			QJsonArray{QStringLiteral("view")});
		cmd.resultSchema = objectSchema({{QStringLiteral("view"), stringProperty()},
			{QStringLiteral("changed"), booleanProperty()}});
		cmd.handler = setView;
		registry.registerCommand(cmd);
	}
	{
		ControlCommand cmd = command("pin");
		cmd.description = QStringLiteral("Pin a side panel ('panel': browser, inspector, devices, mixer, detail...) open in every view "
			"that offers it, or unpin it ('pinned' false) - the pin button in the panel's header. Kept with the layout (a "
			"preference, not project state). Refused for the arranger (always a main panel) and the launcher; a pinned mixer or "
			"note editor is still the main panel of its own view.");
		cmd.argsSchema = objectSchema({{QStringLiteral("panel"), stringProperty()}, {QStringLiteral("pinned"), booleanProperty()}},
			QJsonArray{QStringLiteral("panel")});
		cmd.resultSchema = objectProperty();
		cmd.handler = pin;
		registry.registerCommand(cmd);
	}
	registerNoteEditorWindowCommands(registry);
	registerSampleEditorWindowCommands(registry);  // 0.5.0 wave 3 theme 15
	registerSelectionWindowCommands(registry);
	registerShortcutCommands(registry);
	registerAutomationEditorCommands(registry);  // 0.5.0 L13 stage 1: automation.editor_mode
	registerMixerViewCommands(registry);
	registerHelpCommands(registry);
	registerLayoutCommands(registry);
	registerMacroCommands(registry);
	registerArrangerViewCommands(registry);
	registerArrangerNavigatorCommands(registry);
	registerModifierToolCommands(registry);
}

} // namespace lmms
