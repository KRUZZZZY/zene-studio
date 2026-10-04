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
#include <QToolButton>

#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "LayoutMenu.h"
#include "MainWindow.h"
#include "MenuBarExtras.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "ShortcutMap.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

const QString IndicatorName = QStringLiteral("workflowRecording");

QJsonObject reply(const char* id)
{
	return ControlRegistry::instance()->invoke(QLatin1String(id)).result;
}

QString recordingText(const QString& name, int steps)
{
	return MacroMenu::tr("Recording workflow \"%1\" - %n step(s) so far", "", steps).arg(name);
}

//! The header's recording indicator - made when @a make and there is none yet; nullptr without a shell.
QToolButton* indicator(QWidget* window, bool make)
{
	auto* main = qobject_cast<MainWindow*>(window);
	Shell* shell = main != nullptr ? main->shell() : nullptr;
	if (shell == nullptr || shell->header() == nullptr) { return nullptr; }
	auto* found = shell->header()->strip()->findChild<QToolButton*>(IndicatorName, Qt::FindDirectChildrenOnly);
	if (found != nullptr || !make) { return found; }
	auto* button = new QToolButton;
	button->setObjectName(IndicatorName);
	button->setProperty("controlCommand", QStringLiteral("workflow.record_stop"));  // the agent surface: a registered command
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	button->setToolTip(MacroMenu::tr("Stop recording and save the workflow"));
	QObject::connect(button, &QToolButton::clicked, button, [button] {
		runCommand(button->window(), CommandCall{QStringLiteral("workflow.record_stop"), {}, {}, QString(), false});
	});
	shell->header()->addToStrip(button);
	return button;
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
	const QJsonObject recording = reply("workflow.status").value(QStringLiteral("recording")).toObject();
	if (recording.value(QStringLiteral("on")).toBool())
	{
		// Theme 19: the first thing the menu says while recording; a status line, not an action.
		m_status = addAction(recordingText(recording.value(QStringLiteral("name")).toString(),
			recording.value(QStringLiteral("steps")).toInt()));
		m_status->setEnabled(false);
		m_status->setProperty("controlCommand", QStringLiteral("workflow.status"));  // the line shows it: a registry item (A15)
		addSeparator();
	}
	addRuns(listed.value(QStringLiteral("workflows")).toArray());
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
	const QString text = recordingText(name, steps);
	if (QToolButton* button = indicator(window, on))
	{
		button->setText(QStringLiteral("● ") + text);
		button->setAccessibleName(text);
		button->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-weight: bold; }").arg(tokens::palette().record.name()));
		button->setVisible(on);
	}
	auto* menu = window->findChild<MacroMenu*>();
	if (menu != nullptr && !menu->m_status.isNull()) { menu->m_status->setText(text); }
}

} // namespace lmms::gui
