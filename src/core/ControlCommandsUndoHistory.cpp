/*
 * ControlCommandsUndoHistory.cpp - WF-033: project.undo_history and
 *                                  project.set_undo_history
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

/*
 * The per-project half of WF-033's settings (docs/UNDO-HISTORY-PERSISTENCE.md).
 * The global defaults are ordinary config keys - settings.set with
 * undohistory/persist, undohistory/steps, undohistory/capmb - and the Settings
 * dialog's General tab; this group reads them back beside the project's own
 * choice and states what a save would keep right now.
 */

#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "UndoHistory.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject historyState()
{
	UndoHistory& history = UndoHistory::instance();
	const ProjectJournal* journal = Engine::projectJournal();
	const ProjectJournal::PersistPlan plan = journal == nullptr ? ProjectJournal::PersistPlan()
		: ProjectJournal::planPersist(journal->stepSnapshots(), UndoHistory::maxSteps(), UndoHistory::capBytes());
	return QJsonObject{
		{QStringLiteral("enabled"), history.enabled()},
		{QStringLiteral("setting"), UndoHistory::settingName(history.projectSetting())},
		{QStringLiteral("default_enabled"), UndoHistory::defaultEnabled()},
		{QStringLiteral("max_steps"), UndoHistory::maxSteps()},
		{QStringLiteral("cap_bytes"), static_cast<double>(UndoHistory::capBytes())},
		{QStringLiteral("saveable_steps"), history.enabled() ? plan.saved : 0},
		{QStringLiteral("saveable_bytes"), history.enabled() ? static_cast<double>(plan.bytes) : 0.0},
		{QStringLiteral("oversize"), history.enabled() && plan.oversize},
		{QStringLiteral("last_save"), history.lastSave().toJson()},
		{QStringLiteral("last_load"), history.lastLoad().toJson()},
	};
}

QJsonObject stateSchema()
{
	return objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("setting"), stringProperty()},
		{QStringLiteral("default_enabled"), booleanProperty()},
		{QStringLiteral("max_steps"), integerProperty()},
		{QStringLiteral("cap_bytes"), numberProperty()},
		{QStringLiteral("saveable_steps"), integerProperty()},
		{QStringLiteral("saveable_bytes"), numberProperty()},
		{QStringLiteral("oversize"), booleanProperty()},
		{QStringLiteral("last_save"), objectProperty()},
		{QStringLiteral("last_load"), objectProperty()},
		{QStringLiteral("previous_setting"), stringProperty()},
	});
}

void registerGet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.undo_history");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("undo_history");
	cmd.description = QStringLiteral("Whether this project saves its undo history with it (WF-033): the "
		"project's own setting (on / off / default), the global default and caps (settings "
		"undohistory/persist, undohistory/steps, undohistory/capmb), what a save would keep NOW "
		"(saveable_steps, saveable_bytes, oversize), and what the last save and load did.");
	cmd.mutating = false;
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = stateSchema();
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(historyState()); };
	registry.registerCommand(cmd);
}

ControlResult handleSet(const QJsonObject& args)
{
	UndoHistory::Setting setting = UndoHistory::Setting::Default;
	if (!UndoHistory::parseSetting(args.value(QStringLiteral("setting")).toString(), &setting))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'setting' is one of on, off, default"));
	}
	UndoHistory& history = UndoHistory::instance();
	const QString previous = UndoHistory::settingName(history.projectSetting());
	history.setProjectSetting(setting);
	if (Engine::projectJournal() != nullptr) { Engine::projectJournal()->applySnapshotPolicy(); }
	Engine::getSong()->setModified();

	QJsonObject result = historyState();
	result.insert(QStringLiteral("previous_setting"), previous);
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), QJsonObject{{QStringLiteral("setting"), previous}}},
			{QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("project.set_undo_history")},
					{QStringLiteral("applies"), QStringLiteral("command")},
					{QStringLiteral("args"), QJsonObject{{QStringLiteral("setting"), previous}}}}},
			{QStringLiteral("reversible"), true},
			{QStringLiteral("mechanism"),
				QStringLiteral("snapshot: the previous setting is in before and the recorded inverse is the "
					"command itself. Turning persistence OFF drops the snapshots the session's steps carried; "
					"turning it back on does not recapture them (docs/UNDO-HISTORY-PERSISTENCE.md)")}});
	return ControlResult::success(result);
}

void registerSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.set_undo_history");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("set_undo_history");
	cmd.description = QStringLiteral("Choose whether THIS project saves its undo history with it (WF-033): "
		"'on' or 'off' overrides the global default (setting undohistory/persist, off unless changed), "
		"'default' follows it. Saved with the project. The newest undo steps (undohistory/steps, 20) are "
		"written by project.save and the interface's Save as compressed whole-project snapshots under a "
		"cap (undohistory/capmb, 64 MB); steps made before it was turned on cannot be saved.");
	cmd.mutating = true;
	cmd.argsSchema = objectSchema({{QStringLiteral("setting"), stringProperty()}}, {QStringLiteral("setting")});
	cmd.resultSchema = stateSchema();
	cmd.handler = [](const QJsonObject& args) { return handleSet(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerUndoHistoryCommands(ControlRegistry& registry)
{
	registerGet(registry);
	registerSet(registry);
}

} // namespace lmms
