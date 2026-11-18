/*
 * ControlCommandsAutomationMode.cpp - automation.editor_mode: the automation editor's edit mode, over the socket
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

/*! 0.5.0 L13, stage 1 of the context-aware keyboard map: the automation editor's four edit modes
 *  (draw, erase, draw out-values, edit tangents) were only reachable by the toolbar's own buttons
 *  and their Shift+letter keys. A scoped panel shortcut needs a command to run (SPEC A11: the key,
 *  the button and the agent surface are one implementation), so this is that command. It triggers
 *  the toolbar action itself, which is what keeps the checked button in step with the mode - a
 *  handler that only called setEditMode would leave the toolbar showing the previous mode.
 *  The mode is interface state: nothing is written to the project, so nothing is journalled. */

#include <QAction>
#include <QJsonObject>

#include "AutomationEditor.h"
#include "AutomationClip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "GuiApplication.h"

namespace lmms
{

using namespace control;

namespace
{

const QStringList kModes{QStringLiteral("draw"), QStringLiteral("erase"),
	QStringLiteral("draw_out_values"), QStringLiteral("edit_tangents")};

//! The editor window, or nullptr: automation.editor_mode needs the interface that owns the toolbar.
gui::AutomationEditorWindow* editorWindow()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr ? g->automationEditor() : nullptr;
}

ControlResult editorMode(const QJsonObject& args)
{
	gui::AutomationEditorWindow* window = editorWindow();
	if (window == nullptr) { return noInterface(QStringLiteral("automation.editor_mode")); }
	const QString mode = args.value(QStringLiteral("mode")).toString();
	const int index = static_cast<int>(kModes.indexOf(mode));
	if (index < 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not an automation editor mode (draw, erase, draw_out_values, edit_tangents)").arg(mode));
	}
	const QString previous = window->editorModeName();
	if (!window->setEditorMode(static_cast<gui::AutomationEditor::EditMode>(index)))
	{
		// Edit Tangents needs a cubic-Hermite curve: on a discrete or linear clip the toolbar's own
		// button is disabled, and a command must refuse the same way rather than pretend.
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the automation editor cannot edit tangents on this clip's progression; "
				"set a cubic Hermite progression first"));
	}
	return ControlResult::success({{QStringLiteral("mode"), window->editorModeName()},
		{QStringLiteral("previous"), previous},
		{QStringLiteral("changed"), previous != window->editorModeName()}});
}

} // namespace

void registerAutomationEditorCommands(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("automation", "editor_mode");
	cmd.description = QStringLiteral("Set the automation editor's edit mode: draw (add and move points), "
		"erase (remove them), draw_out_values (draw a curve's out-values directly) or edit_tangents (shape "
		"the tangents of a cubic Hermite curve - refused, typed, for any other progression, exactly as the "
		"toolbar's own disabled button says). The toolbar's matching button is checked afterwards and "
		"'previous' reports the mode it was in. The mode is interface state: it is not saved with the "
		"project and has no undo.");
	cmd.argsSchema = objectSchema({{QStringLiteral("mode"), enumProperty(kModes)}}, {QStringLiteral("mode")});
	cmd.resultSchema = objectSchema({{QStringLiteral("mode"), stringProperty()},
		{QStringLiteral("previous"), stringProperty()}, {QStringLiteral("changed"), booleanProperty()}});
	cmd.handler = editorMode;
	registry.registerCommand(cmd);
}

} // namespace lmms
