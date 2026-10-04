/*
 * MacroMenu.cpp - the menu bar's Workflows menu: one action per saved workflow, and the recording state
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

#include "MacroMenu.h"

#include <QActionGroup>
#include <QMainWindow>
#include <QMenuBar>

#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "LayoutMenu.h"
#include "MenuBarExtras.h"
#include "ShortcutMap.h"
#include "Song.h"
#include "WorkflowRecordingIndicator.h"

namespace lmms::gui
{

namespace
{

QJsonObject reply(const char* id)
{
	return ControlRegistry::instance()->invoke(QLatin1String(id)).result;
}

//! A line of the menu that says something rather than does something (a registry item: it shows @a command's state).
QAction* addStatusLine(QMenu* menu, const QString& text, const char* command)
{
	QAction* line = menu->addAction(text);
	line->setEnabled(false);
	line->setProperty("controlCommand", QLatin1String(command));
	return line;
}

} // namespace

MacroMenu::MacroMenu(QWidget* parent) :
	QMenu(tr("Wor&kflows"), parent)
{
	setObjectName(QStringLiteral("workflowMenu"));
	refresh();
	if (Song* song = Engine::getSong())
	{
		connect(song, &Song::projectLoaded, this, [this] { followProject(true); });
		connect(song, &Song::projectFileNameChanged, this, [this] { followProject(false); });
	}
}

void MacroMenu::refresh()
{
	clearRebuiltMenu(this);
	m_status = nullptr;
	const QJsonObject listed = reply("workflow.list");
	const QJsonObject status = reply("workflow.status");
	const QJsonObject recording = status.value(QStringLiteral("recording")).toObject();
	if (recording.value(QStringLiteral("on")).toBool())
	{
		// Theme 19: the first thing the menu says while recording; a status line, not an action.
		m_status = addStatusLine(this, WorkflowRecordingIndicator::recordingText(recording.value(QStringLiteral("name")).toString(),
			recording.value(QStringLiteral("steps")).toInt()), "workflow.status");
		addSeparator();
	}
	addRuns(listed.value(QStringLiteral("workflows")).toArray());
	addRecovery(status);
	addRecording();
	addScopeMenu(listed.value(QStringLiteral("scope")).toString());
	QAction* edit = addAction(tr("Edit Workflows..."));
	edit->setProperty("controlCommand", QStringLiteral("workflow.list"));
	connect(edit, &QAction::triggered, this, [this] { showCommandList(parentWidget(), CommandLists::macros()); });
}

void MacroMenu::addRuns(const QJsonArray& workflows)
{
	for (const QJsonValue& value : workflows)
	{
		const QString name = value.toObject().value(QStringLiteral("name")).toString();
		// A '&' in the name is shown as itself, not taken as a mnemonic (the run keeps the name as saved).
		addCommandAction(this, QString(name).replace(QLatin1Char('&'), QStringLiteral("&&")), QStringLiteral("workflow.run"), [name] {
			return CommandCall{QStringLiteral("workflow.run"), {{QStringLiteral("name"), name}}, {}, name, false};
		});
	}
	if (!workflows.isEmpty()) { addSeparator(); }
}

void MacroMenu::addRecovery(const QJsonObject& status)
{
	// Theme 19: a run that stopped at a failing step says where, and offers the step back or the rest without it.
	if (!status.value(QStringLiteral("resumable")).toBool()) { return; }
	const QJsonObject failed = status.value(QStringLiteral("failed")).toObject();
	const int step = failed.value(QStringLiteral("step")).toInt();
	addStatusLine(this, tr("\"%1\" stopped at step %2: %3").arg(status.value(QStringLiteral("name")).toString()).arg(step)
		.arg(failed.value(QStringLiteral("name")).toString()), "workflow.status");
	addCommandAction(this, tr("Retry Step %1").arg(step), QStringLiteral("workflow.resume"),
		[] { return CommandCall{QStringLiteral("workflow.resume"), {{QStringLiteral("skip"), false}}, {}, QString(), false}; });
	addCommandAction(this, tr("Skip Step %1 and Continue").arg(step), QStringLiteral("workflow.resume"),
		[] { return CommandCall{QStringLiteral("workflow.resume"), {{QStringLiteral("skip"), true}}, {}, QString(), false}; });
	addSeparator();
}

void MacroMenu::addRecording()
{
	// 0.5.0 ED-056: record what you do as a workflow. Both stay enabled (so both can be given a key); the
	// one that does not apply now says why it is refused.
	addCommandAction(this, tr("Record a Workflow..."), QStringLiteral("workflow.record_start"), [] {
		return CommandCall{QStringLiteral("workflow.record_start"), {}, {{QStringLiteral("name"), tr("My workflow")}},
			tr("Record a Workflow"), true};
	});
	addCommandAction(this, tr("Stop Recording the Workflow"), QStringLiteral("workflow.record_stop"),
		[] { return CommandCall{QStringLiteral("workflow.record_stop"), {}, {}, QString(), false}; });
}

//! Where workflows are kept (workflow.set_scope): the choice in use is checked.
void MacroMenu::addScopeMenu(const QString& scope)
{
	QMenu* where = addMenu(tr("Keep Workflows"));
	auto* group = new QActionGroup(where);  // exclusive: choosing the checked one keeps it checked
	const QList<QPair<QString, QString>> choices{{QStringLiteral("shared"), tr("Shared by Every Project")},
		{QStringLiteral("project"), tr("With Each Project")}};
	for (const auto& [value, label] : choices)
	{
		QAction* action = addCommandAction(where, label, QStringLiteral("workflow.set_scope"), [value = value] {
			return CommandCall{QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), value}}, {}, QString(), false};
		});
		action->setCheckable(true);
		action->setChecked(value == scope);
		group->addAction(action);
	}
}

void MacroMenu::followProject(bool loaded)
{
	refresh();
	if (auto* main = qobject_cast<QMainWindow*>(parentWidget())) { ShortcutMap::apply(main->menuBar()); }
	if (loaded) { LayoutMenu::recallProjectLayout(this); }
}

MacroMenu* MacroMenu::install(QMenuBar* bar)
{
	if (auto* existing = bar->window()->findChild<MacroMenu*>()) { return existing; }
	auto* menu = static_cast<MacroMenu*>(insertBeforeHelp(bar, new MacroMenu(bar->window())));
	LayoutMenu::recallProjectLayout(menu);  // a project opened before the window was shown
	return menu;
}

void MacroMenu::showRecording(QWidget* window, bool on, const QString& name, int steps)
{
	if (WorkflowRecordingIndicator* shown = WorkflowRecordingIndicator::inWindow(window, on)) { shown->setRecording(on, name, steps); }
	auto* menu = window->findChild<MacroMenu*>();
	if (menu != nullptr && !menu->m_status.isNull()) { menu->m_status->setText(WorkflowRecordingIndicator::recordingText(name, steps)); }
}

void MacroMenu::refreshIn(QMainWindow* window)
{
	install(window->menuBar())->refresh();
	ShortcutMap::apply(window->menuBar());  // the rebuilt actions get the keys bound to them back
}

} // namespace lmms::gui
