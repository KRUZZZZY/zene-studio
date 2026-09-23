/*
 * FocusDeskWorkspaces.cpp - §6.1 workspaces as declarative arrangements, and
 *                           the Focus Desk members that read them (work-list
 *                           row 7; row 6's layout-as-data half lands in
 *                           FocusDesk.cpp's save/restore).
 *
 * The data half: v1Workspaces() restates the B-desk mockup declaration -
 * membership from the `data-in` attributes, rail widths from the `[data-ws]`
 * column widths, the flagship from the STAGE map - and validate() answers
 * whether a list holds together against the register.
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

#include "FocusDeskWorkspaces.h"

#include <initializer_list>

#include <QAction>
#include <QHash>
#include <QLayout>
#include <QMenu>
#include <QSplitter>
#include <QToolButton>

#include "Accessibility.h"
#include "ControlRegistry.h"
#include "FocusDesk.h"
#include "FocusDeskWorkspaces.h"

namespace lmms::gui
{

namespace
{

//! Map a B-desk panel id to the register row it is, so membership below is
//! the mockup's `data-in` declaration read literally. The three register rows
//! the mockup has no panel for - automation, project, learn - are members of
//! no v1 workspace by that same declaration: membership is read from the
//! mockup, never inferred from what happens to be mounted.
QString registerIdOfPanel(const QString& panelId)
{
	static const QHash<QString, QString> panelToRegister{
		{QStringLiteral("p-browser"), QStringLiteral("browser")},
		{QStringLiteral("p-arr"), QStringLiteral("arrangement")},
		{QStringLiteral("p-detail"), QStringLiteral("detaileditor")},
		{QStringLiteral("p-session"), QStringLiteral("session")},
		{QStringLiteral("p-mix"), QStringLiteral("mixer")},
		{QStringLiteral("p-mod"), QStringLiteral("modulation")},
		{QStringLiteral("p-insp"), QStringLiteral("inspector")},
		{QStringLiteral("p-rack"), QStringLiteral("devicechain")},
	};
	const auto it = panelToRegister.constFind(panelId);
	return it != panelToRegister.constEnd() ? it.value() : panelId;
}

QStringList membersOf(const std::initializer_list<QString>& panels)
{
	QStringList out;
	for (const auto& panel : panels) { out.append(registerIdOfPanel(panel)); }
	return out;
}

FocusWorkspace makeWorkspace(const QString& id, const QString& title,
	int leftRail, int rightRail, const QStringList& members, const QString& flagship)
{
	FocusWorkspace w;
	w.id = id;
	w.title = title;
	w.leftRail = leftRail;
	w.rightRail = rightRail;
	w.members = members;
	w.flagship = flagship;
	w.scope = WorkspaceScope::Machine; // pending: the project write path
	return w;
}

void report(QStringList* violations, const QString& text)
{
	if (violations != nullptr) { violations->append(text); }
}

void checkIdentity(const FocusWorkspace& workspace, QStringList* violations)
{
	if (workspace.id.isEmpty() || workspace.title.isEmpty())
	{
		report(violations, QObject::tr("workspace with an empty id or title"));
	}
	if (workspace.leftRail <= 0 || workspace.rightRail <= 0)
	{
		report(violations, QObject::tr("workspace '%1' has a non-positive rail width")
			.arg(workspace.id));
	}
	if (workspace.members.isEmpty())
	{
		report(violations, QObject::tr("workspace '%1' declares no members")
			.arg(workspace.id));
	}
}

void checkMembers(const FocusWorkspace& workspace, const QList<FocusModule>& rows,
	QStringList* violations)
{
	for (const auto& member : workspace.members)
	{
		if (!FocusDeskModules::find(rows, member))
		{
			report(violations, QObject::tr("workspace '%1' names module '%2', "
				"which is not in the register").arg(workspace.id, member));
		}
	}
	if (!workspace.flagship.isEmpty() && !workspace.members.contains(workspace.flagship))
	{
		report(violations, QObject::tr("workspace '%1' stages flagship '%2', "
			"which it does not declare a member of").arg(workspace.id, workspace.flagship));
	}
}

} // namespace

QList<FocusWorkspace> FocusDeskWorkspaces::v1Workspaces()
{
	// B-desk `data-in` sets, panel ids exactly as the mockup declares them:
	//   browser/detail/session/mix/mod/insp are in all five workspaces;
	//   p-arr only in compose+record, p-rack only in compose/design/mix.
	// Rail widths are the `[data-ws]` columns; the flagship is the STAGE map
	// (compose/record -> detail, mix -> mix, design -> mod, perform -> session).
	const QStringList allButArrangement = membersOf(
		{QStringLiteral("p-browser"), QStringLiteral("p-detail"), QStringLiteral("p-session"),
			QStringLiteral("p-mix"), QStringLiteral("p-mod"), QStringLiteral("p-insp"),
			QStringLiteral("p-rack")});
	const QStringList allButArrangementNoRack = membersOf(
		{QStringLiteral("p-browser"), QStringLiteral("p-detail"), QStringLiteral("p-session"),
			QStringLiteral("p-mix"), QStringLiteral("p-mod"), QStringLiteral("p-insp")});
	const QStringList composeAndRecord = membersOf(
		{QStringLiteral("p-browser"), QStringLiteral("p-arr"), QStringLiteral("p-detail"),
			QStringLiteral("p-session"), QStringLiteral("p-mix"), QStringLiteral("p-mod"),
			QStringLiteral("p-insp"), QStringLiteral("p-rack")});

	return {
		makeWorkspace(QStringLiteral("compose"), QStringLiteral("Compose"), 300, 300,
			composeAndRecord, QStringLiteral("detaileditor")),
		makeWorkspace(QStringLiteral("record"), QStringLiteral("Record"), 300, 300,
			composeAndRecord, QStringLiteral("detaileditor")),
		makeWorkspace(QStringLiteral("mix"), QStringLiteral("Mix"), 300, 392,
			allButArrangement, QStringLiteral("mixer")),
		makeWorkspace(QStringLiteral("design"), QStringLiteral("Design"), 280, 336,
			allButArrangement, QStringLiteral("modulation")),
		makeWorkspace(QStringLiteral("perform"), QStringLiteral("Perform"), 268, 300,
			allButArrangementNoRack, QStringLiteral("session")),
	};
}

const FocusWorkspace* FocusDeskWorkspaces::find(const QList<FocusWorkspace>& workspaces,
	const QString& id)
{
	for (const auto& workspace : workspaces)
	{
		if (workspace.id == id) { return &workspace; }
	}
	return nullptr;
}

QString workspaceScopeName(WorkspaceScope scope)
{
	return scope == WorkspaceScope::Project
		? QObject::tr("this project") : QObject::tr("this machine");
}

QStringList FocusDeskWorkspaces::validate(const QList<FocusWorkspace>& workspaces,
	const QList<FocusModule>& rows)
{
	QStringList violations;
	if (workspaces.isEmpty()) { violations.append(QObject::tr("no workspaces declared")); }
	QStringList seen;
	for (const auto& workspace : workspaces)
	{
		checkIdentity(workspace, &violations);
		if (seen.contains(workspace.id))
		{
			report(&violations, QObject::tr("workspace id '%1' declared twice").arg(workspace.id));
		}
		seen.append(workspace.id);
		checkMembers(workspace, rows, &violations);
	}
	return violations;
}

QString FocusDesk::workspace() const { return m_workspace; }

bool FocusDesk::workspaceContains(const QString& moduleId) const
{
	const auto workspaces = FocusDeskWorkspaces::v1Workspaces();
	const FocusWorkspace* workspace = FocusDeskWorkspaces::find(workspaces, m_workspace);
	return workspace != nullptr && workspace->members.contains(moduleId);
}

void FocusDesk::refreshWorkspaceLabel()
{
	if (m_workspaceButton == nullptr) { return; }
	const auto workspaces = FocusDeskWorkspaces::v1Workspaces();
	const FocusWorkspace* workspace = FocusDeskWorkspaces::find(workspaces, m_workspace);
	const QString title = workspace != nullptr ? workspace->title : tr("(none)");
	const QString scope = workspaceScopeName(
		workspace != nullptr ? workspace->scope : WorkspaceScope::Machine);
	// C2: the scope is stated on screen at the switcher itself, every time.
	m_workspaceButton->setText(tr("Workspace: %1 (%2)").arg(title, scope));
	const QString tip = tr("Switch the workspace: where each member sits, the "
		"rail widths and the stage's module. The current one's scope is shown "
		"beside its name.");
	m_workspaceButton->setToolTip(tip);
	lmms::a11y::announce(m_workspaceButton, m_workspaceButton->text(), tip);
}

void FocusDesk::buildWorkspaceSwitcher()
{
	m_workspaceButton = new QToolButton(m_strip);
	m_workspaceButton->setObjectName(QStringLiteral("focusDeskWorkspace"));
	m_workspaceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_workspaceButton->setAutoRaise(true);
	m_workspaceButton->setPopupMode(QToolButton::InstantPopup);
	m_workspaceButton->setFocusPolicy(Qt::TabFocus);
	refreshWorkspaceLabel();

	auto* menu = new QMenu(m_workspaceButton);
	QObject::connect(menu, &QMenu::aboutToShow, this, [this, menu]()
	{
		menu->clear();
		// Built fresh each open: checkable items tick against the LIVE state
		// (§7.3(3)) and the data comes from the same v1Workspaces() list the
		// desk applies - one declaration for the menu and for the layout.
		const auto workspaces = FocusDeskWorkspaces::v1Workspaces();
		for (const auto& workspace : workspaces)
		{
			const QString id = workspace.id;
			const QString scope = workspaceScopeName(workspace.scope);
			const QString rails = QStringLiteral("%1/%2")
				.arg(workspace.leftRail).arg(workspace.rightRail);
			QAction* action = menu->addAction(
				tr("%1 - %2 (%3)").arg(workspace.title, id, scope));
			action->setProperty("controlCommand", QStringLiteral("settings.set"));
			action->setToolTip(tr("Switch to the %1 workspace - scope: %2, rail "
				"widths %3px").arg(workspace.title, scope, rails));
			action->setCheckable(true);
			action->setChecked(m_workspace == id);
			QObject::connect(action, &QAction::triggered, this, [this, id]()
			{
				// Apply locally (the desk works stand-alone, like the density
				// button), then publish through settings.set so config and an
				// agent on the socket see the same command - the pane's
				// observer routes external writes through this same
				// applyWorkspace, so there is one implementation either way.
				if (!applyWorkspace(id)) { return; }
				ControlRegistry::instance()->invoke(QStringLiteral("settings.set"),
					{{QStringLiteral("key"), QStringLiteral("ui/focusdesk.workspace")},
						{QStringLiteral("value"), id}});
			});
		}
	});
	m_workspaceButton->setMenu(menu);
	if (auto* layout = m_strip->layout(); layout != nullptr) { layout->addWidget(m_workspaceButton); }
}

void FocusDesk::applyRailWidths(int leftRail, int rightRail)
{
	if (m_splitter == nullptr) { return; }
	int total = 0;
	for (const int width : m_splitter->sizes()) { total += width; }
	if (total <= 0) { total = leftRail + rightRail + 600; }
	const int stage = qMax(120, total - leftRail - rightRail);
	m_splitter->setSizes({leftRail, stage, rightRail});
}

bool FocusDesk::restoreWorkspaceRails(const QVariantMap& layout)
{
	bool ok = true;
	if (layout.contains(QStringLiteral("workspace")))
	{
		const QString workspace = layout.value(QStringLiteral("workspace")).toString();
		if (!workspace.isEmpty() && !applyWorkspace(workspace)) { ok = false; }
	}
	// The explicit rail keys win over the workspace's own widths: they are
	// the user's later resize of those same rails (row 6's layout-as-data).
	if (layout.contains(QStringLiteral("leftRail")) && layout.contains(QStringLiteral("rightRail")))
	{
		applyRailWidths(layout.value(QStringLiteral("leftRail")).toInt(),
			layout.value(QStringLiteral("rightRail")).toInt());
	}
	return ok;
}

void FocusDesk::applyWorkspaceFlagship(const FocusWorkspace& workspace)
{
	// §6.1: the flagship takes the stage ONLY when the current focus leaves
	// the workspace. Promotion goes through dispatchAction - the `mod:` seam,
	// same as chips and menu - so a flagship the register forbids the stage
	// (Design/Perform) refuses with the register's reason and the stage stays
	// put, said out loud. An empty desk holds the choice for the first mount.
	if (!m_focused.isEmpty() && !workspace.members.contains(m_focused))
	{
		if (!workspace.flagship.isEmpty())
		{
			dispatchAction(focusActionCommandId(workspace.flagship));
		}
		return;
	}
	if (m_focused.isEmpty() && m_pendingFocus.isEmpty() && !workspace.flagship.isEmpty())
	{
		m_pendingFocus = workspace.flagship;
	}
}

void FocusDesk::parkWorkspaceNonMembers(const FocusWorkspace& workspace)
{
	// Park mounted non-members EXCEPT the current focus (a refused flagship
	// keeps its stage slot). "Park" is the honest non-member state: the chip
	// still exists and still promotes - never removed, never hidden without a
	// word (X4).
	for (const auto& row : m_rows)
	{
		if (!m_modules.contains(row.id) || workspace.members.contains(row.id)) { continue; }
		if (row.id == m_focused) { continue; }
		placeCard(row.id, Destination::Park);
	}
}

bool FocusDesk::applyWorkspace(const QString& workspaceId)
{
	if (workspaceId.isEmpty()) { return false; }
	const auto workspaces = FocusDeskWorkspaces::v1Workspaces();
	const FocusWorkspace* workspace = FocusDeskWorkspaces::find(workspaces, workspaceId);
	if (workspace == nullptr)
	{
		emit moduleRefused(workspaceId,
			tr("'%1' is not one of this build's workspaces").arg(workspaceId));
		return false;
	}
	if (m_workspace == workspace->id) { return true; } // idempotent: an observer answering our own write must be a no-op
	m_workspace = workspace->id;

	// 1. Re-measure the rails (B-desk "[data-ws] column widths").
	applyRailWidths(workspace->leftRail, workspace->rightRail);
	// 2. The flagship, only if the focus left the workspace.
	applyWorkspaceFlagship(*workspace);
	// 3. Park the mounted members it does not declare.
	parkWorkspaceNonMembers(*workspace);

	refreshChips();
	refreshWorkspaceLabel();
	emit workspaceChanged(workspace->id);
	return true;
}

} // namespace lmms::gui
