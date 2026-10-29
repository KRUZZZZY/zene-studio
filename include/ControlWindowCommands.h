/*
 * ControlWindowCommands.h - the `window.*` group: the editors' windows as commands
 *
 * The main window's seven editor toggles (View menu Ctrl+1..7 and the window
 * toolbar) were the largest block of the agent-surface grandfather list: actions
 * an agent could not reach and the command palette could not list. They are now
 * one registered command, `window.toggle {editor}`, and the menu items and toolbar
 * buttons DISPATCH THROUGH it (the plan of record's item 0a, "GUI actions become
 * registered commands"), so the interface and the socket are one path.
 * `window.get_state` reads which editors are showing.
 *
 * Interface state, not project state: nothing is journalled and nothing is
 * undone (the `not_mutating` rows in ControlReversibilityTablePassive.cpp, the
 * same rule `midi.learn_toggle` follows). Neither command needs the engine. In a
 * process with no interface (the CLI renderer) both answer `requires`, typed.
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

#ifndef LMMS_CONTROL_WINDOW_COMMANDS_H
#define LMMS_CONTROL_WINDOW_COMMANDS_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

class ControlRegistry;
struct ControlCommand;
struct ControlResult;

namespace gui
{
class MainWindow;
}

//! The interface commands' shared scaffolding (src/gui/ControlGuiSupport.cpp): a command of
//! @a group.@a verb that needs no engine and records nothing (a mutating one sets the flag);
//! the typed `requires` refusal of a process without a main window; that main window.
LMMS_EXPORT ControlCommand guiCommand(const char* group, const char* verb);
LMMS_EXPORT ControlResult noInterface(const QString& id);
LMMS_EXPORT gui::MainWindow* controlMainWindow();

LMMS_EXPORT void registerWindowCommands(ControlRegistry& registry);

//! M3.2: window.fullscreen / attach_all / detach_all / settings, app.about /
//! online_help and project.save_version (src/gui/ControlCommandsShell.cpp).
LMMS_EXPORT void registerShellCommands(ControlRegistry& registry);

//! What a menu action whose command is one of these does: the registry call,
//! so the action and the socket share one implementation (SPEC A11).
LMMS_EXPORT void dispatchShellCommand(const QString& id, const QJsonObject& args = QJsonObject());

//! Help > Online Help's implementation: hands the documentation URL to the desktop and,
//! when the desktop will not take it (no URL handler), says so in the window's status bar -
//! the menu discards the command's own `opened: false`, so nothing else would.
//! app.online_help declares `requires: human`, which the registry refuses for EVERY
//! caller (ControlRegistry::checkRequires), so the action calls this directly - the same
//! arrangement as the Telemetry action and its openTelemetryConsentScreen (MainWindow.cpp).
//! @a window is the window the menu belongs to; it must not be null. Returns whether the
//! desktop took the URL.
LMMS_EXPORT bool openOnlineHelp(gui::MainWindow* window);

//! The `editor` values window.toggle accepts, in the menu's Ctrl+1..7 order.
LMMS_EXPORT QStringList windowEditorNames();

//! window.note_editor_set - the note editor's view over the socket
//! (src/gui/ControlCommandsNoteEditor.cpp); registered by registerWindowCommands.
LMMS_EXPORT void registerNoteEditorWindowCommands(ControlRegistry& registry);
//! The note editor's state (NoteEditor::describe), or {} with no interface.
LMMS_EXPORT QJsonObject noteEditorState();

//! window.select - the selection the Inspector and the Device panel follow
//! (src/gui/ControlCommandsSelection.cpp); registered by registerWindowCommands.
LMMS_EXPORT void registerSelectionWindowCommands(ControlRegistry& registry);
//! The selection and what the two panels show (window.select's reply), or {} with no interface.
LMMS_EXPORT QJsonObject shellSelectionState();

//! 0.5.0 L13 customisation (registered by registerWindowCommands): shortcuts.* (rebindable
//! keys, src/gui/ControlCommandsShortcuts.cpp), window.layout_* / detach / attach / header_*
//! (src/gui/ControlCommandsLayouts.cpp) and macro.* (src/gui/ControlCommandsMacros.cpp).
LMMS_EXPORT void registerShortcutCommands(ControlRegistry& registry);
//! 0.5.0 L13, the context-aware keyboard map's stage 1: automation.editor_mode, the automation
//! editor's four edit modes (src/gui/ControlCommandsAutomationMode.cpp); registered by
//! registerWindowCommands.
LMMS_EXPORT void registerAutomationEditorCommands(ControlRegistry& registry);
//! 0.5.0 L09 (MX-049/066): window.mixer_view (src/gui/ControlCommandsMixerView.cpp).
LMMS_EXPORT void registerMixerViewCommands(ControlRegistry& registry);
//! 0.5.0 L14: app.help / app.tours / app.tour / window.appearance (ControlCommandsHelp.cpp).
LMMS_EXPORT void registerHelpCommands(ControlRegistry& registry);
LMMS_EXPORT void registerLayoutCommands(ControlRegistry& registry);
LMMS_EXPORT void registerMacroCommands(ControlRegistry& registry);
//! arrangement.zoom / zoom_back / fit_tracks / set_tool and window.tool_menu
//! (src/gui/ControlCommandsArrangerView.cpp).
LMMS_EXPORT void registerArrangerViewCommands(ControlRegistry& registry);
//! 0.5.0 L04 (ED-053): arrangement.scroll / arrangement.navigator (src/gui/ArrangerNavigator.cpp).
LMMS_EXPORT void registerArrangerNavigatorCommands(ControlRegistry& registry);
//! 0.5.0 L04 (ED-014): arrangement.modifier_tools (src/gui/ModifierTools.cpp).
LMMS_EXPORT void registerModifierToolCommands(ControlRegistry& registry);

//! What a View-menu item or toolbar button does: window.toggle through the
//! registry, exactly as an agent on the socket would.
LMMS_EXPORT void dispatchWindowToggle(const QString& editor);

} // namespace lmms

#endif // LMMS_CONTROL_WINDOW_COMMANDS_H
