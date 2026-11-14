/*
 * ControlCommandsStepGrid.cpp - window.step_grid: open a MIDI clip in the melodic step grid and
 *                               answer what the grid shows (Q050-294.2)
 *
 * Edit > Step Grid... and the note editor's Notes > Tools > Open in Step Grid dispatch this, and an
 * agent sends the same command. The grid's edits are note.step_set calls (each its own undo step);
 * this command only opens the view, so it is not mutating.
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

#include <QApplication>
#include <QJsonObject>
#include <QPointer>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "ShellSelection.h"
#include "StepGridPanel.h"

namespace lmms
{

using namespace control;

namespace
{

const QString kId = QStringLiteral("window.step_grid");

//! The one grid window: a second window.step_grid re-targets it instead of stacking another.
QPointer<gui::StepGridPanel>& gridPanel()
{
	static QPointer<gui::StepGridPanel> panel;
	return panel;
}

//! The clip to open: the one named, else the shell's selected MIDI clip, else the control
//! surface's selected clip, else the clip the grid already shows. Empty when there is none.
QString targetClip(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("clip"))) { return args.value(QStringLiteral("clip")).toString(); }
	if (gui::ShellSelection* shell = gui::ShellSelection::instance(); shell != nullptr && dynamic_cast<MidiClip*>(shell->clip()) != nullptr)
	{
		return clipIdOf(shell->clip());
	}
	if (!selectedClipId().isEmpty()) { return selectedClipId(); }
	return gridPanel() != nullptr ? gridPanel()->clipId() : QString();
}

//! The view settings the call names, applied in order; a refusal names the bad one.
ControlResult applyView(gui::StepGridPanel* panel, const QJsonObject& args)
{
	if (args.contains(QStringLiteral("step_ticks")))
	{
		const int ticks = args.value(QStringLiteral("step_ticks")).toInt();
		if (ticks < 1 || ticks > 3072) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'step_ticks' must be in [1, 3072]")); }
		panel->setStepTicks(ticks);
	}
	if (args.contains(QStringLiteral("scale"))
		&& !panel->setScaleChoice(args.value(QStringLiteral("scale")).toString(), args.value(QStringLiteral("root")).toInt()))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no scale called '%1' in the grid's list (\"\" follows the project key, \"none\" is no scale)")
				.arg(args.value(QStringLiteral("scale")).toString()));
	}
	if (args.contains(QStringLiteral("scale_rows_only"))) { panel->setScaleRowsOnly(args.value(QStringLiteral("scale_rows_only")).toBool()); }
	return ControlResult::success({});
}

ControlResult openStepGrid(const QJsonObject& args)
{
	// A widget application is the interface this needs; a main window is optional (it parents the grid).
	if (qobject_cast<QApplication*>(QCoreApplication::instance()) == nullptr) { return noInterface(kId); }
	const QString clip = targetClip(args);
	if (clip.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name a 'clip' or select a MIDI clip first"));
	}
	ControlResult error;
	ClipRef ref;
	if (resolveMidiClip(clip, &ref, &error) == nullptr) { return error; }
	if (gridPanel() == nullptr)
	{
		gridPanel() = new gui::StepGridPanel(controlMainWindow());
		gridPanel()->setAttribute(Qt::WA_DeleteOnClose);
		gridPanel()->setModal(false);
	}
	gui::StepGridPanel* panel = gridPanel();
	const ControlResult view = applyView(panel, args);
	if (!view.ok) { return view; }
	const QString refusal = panel->setClip(clip);
	if (!refusal.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, refusal); }
	panel->show();
	panel->raise();
	return ControlResult::success(panel->describe());
}

} // namespace

void registerStepGridWindowCommand(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("window", "step_grid");
	cmd.description = QStringLiteral("Open a MIDI clip in the melodic step grid (Edit > Step Grid..., or the note editor's "
		"Notes > Tools > Open in Step Grid) and answer what the grid shows. Columns are steps of 'step_ticks' ticks "
		"(default 12, a sixteenth), rows are pitches; every lit cell is one ordinary note of the clip, so the piano roll "
		"(roll.get_state) and note.step_get_state read the same notes. 'scale' picks the scale the rows are read against: "
		"\"\" the project key (scale.set), \"none\" no scale, or a scale.list name on 'root' (0 = C); "
		"'scale_rows_only' hides the out-of-scale rows that hold no note. With no 'clip' it opens the selected MIDI clip, "
		"or keeps the grid's own. The grid's clicks and fields each run ONE note.step_set (one undo step); this command "
		"only opens the view and edits nothing. The reply: 'clip', 'step_ticks', 'steps', 'rows' (keys, highest first), "
		"'cells' (each note's id, step, key, velocity, chance, ratchet, length, in_scale) and 'selected'.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("step_ticks"), integerProperty(1, 3072)},
		{QStringLiteral("scale"), stringProperty()}, {QStringLiteral("root"), integerProperty(0, 11)},
		{QStringLiteral("scale_rows_only"), booleanProperty()}});
	cmd.resultSchema = objectSchema({{QStringLiteral("shown"), booleanProperty()}, {QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("step_ticks"), integerProperty()}, {QStringLiteral("steps"), integerProperty()},
		{QStringLiteral("scale_choice"), stringProperty()}, {QStringLiteral("scale_rows_only"), booleanProperty()},
		{QStringLiteral("scale"), objectProperty()}, {QStringLiteral("rows"), arrayProperty()},
		{QStringLiteral("cells"), arrayProperty()}, {QStringLiteral("selected"), stringProperty()}});
	cmd.handler = openStepGrid;
	registry.registerCommand(cmd);
}

} // namespace lmms
