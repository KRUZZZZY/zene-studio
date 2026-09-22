/*
 * FocusDeskWorkspaces.h - declarative workspaces for the Focus Desk (UI plan
 *                        §6.1 / §10 item 7, claims C1 and C2).
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

#ifndef LMMS_GUI_FOCUS_DESK_WORKSPACES_H
#define LMMS_GUI_FOCUS_DESK_WORKSPACES_H

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

#include "FocusDeskModules.h"

namespace lmms::gui
{

//! Where a workspace's selection is stored, and this is the scope the UI must
//! always state on screen (C2: "two layout scopes, always named: this project
//! and this machine"). A layout whose scope is ambiguous is the thing C2
//! forbids, so the enum value travels with every record and every menu entry
//! renders it.
enum class WorkspaceScope
{
	Machine, //!< this machine - the selection lives in this machine's config
	Project, //!< this project - pending: the project-file write path (engine)
};

//! One workspace: a DECLARATIVE arrangement, not a visibility list (C1).
//!
//! The register answers "what may exist" (§4.2); a workspace answers "where
//! does each member sit, how wide are the rails, which module owns the stage
//! by default" - as data, so it can be read, diffed and replaced without a
//! line of layout code (§6.1, C1's whole claim).
struct FocusWorkspace
{
	QString id;                        //!< stable id: the config value the setting writes
	QString title;                     //!< the label a user reads (always on screen with its scope)
	WorkspaceScope scope = WorkspaceScope::Machine;
	//! The register ids that are members - each module's membership is
	//! DECLARED here, never inferred from a widget list (C1).
	QStringList members;
	int leftRail = 300;               //!< the rail widths this workspace sets (§6.1 split sizes)
	int rightRail = 300;
	//! The stage's default module (§6.1 "plus the focus module where the
	//! direction has one") - applied only when the current focus module leaves
	//! the workspace, see FocusDesk::applyWorkspace().
	QString flagship;
};

//! The v1 workspaces and the questions asked of them.
//!
//! Deliberately widget-free, like FocusDeskModules: what a workspace IS is
//! decidable without a QApplication, and only applying one needs the shell.
class FocusDeskWorkspaces
{
	Q_DECLARE_TR_FUNCTIONS(FocusDeskWorkspaces)

public:
	//! The direction of record's five workspaces (§6.1: Compose, Record, Mix,
	//! Design, Perform - a starting set, not a schema), with the membership
	//! and rail sizes the B-desk mockup declares (research/ui/mockups/
	//! B-desk.html `data-in` attributes and `[data-ws]` column widths) and the
	//! flagship its STAGE map names.
	static QList<FocusWorkspace> v1Workspaces();

	static const FocusWorkspace* find(const QList<FocusWorkspace>& workspaces, const QString& id);

	//! Every rule a workspace list must satisfy, as violations - the same
	//! contract shape FocusDeskModules::validate() uses, so a workspace that
	//! names a module the register does not have is reported by id rather than
	//! rendered as a phantom card.
	static QStringList validate(const QList<FocusWorkspace>& workspaces,
		const QList<FocusModule>& rows);
};

//! The human name of a scope, for the on-screen statement C2 requires.
QString workspaceScopeName(WorkspaceScope scope);

} // namespace lmms::gui

#endif // LMMS_GUI_FOCUS_DESK_WORKSPACES_H
