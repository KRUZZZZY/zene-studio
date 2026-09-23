/*
 * FocusDeskActions.h - the Focus Desk's command record, its generated menus
 *                       and the `mod:` dispatch seam (UI plan §4.1, §7.3, §9.4;
 *                       work-list rows 4 and 5).
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

#ifndef LMMS_GUI_FOCUS_DESK_ACTIONS_H
#define LMMS_GUI_FOCUS_DESK_ACTIONS_H

#include <functional>

#include <QJsonObject>
#include <QList>
#include <QString>

class QMenu;

namespace lmms::gui
{

class FocusDesk;
struct FocusModule;

//! How a record renders (§4.1's `kind` column). Only the four values this
//! shell can honestly render are named: `Insert` has no insertion surface on
//! the desk yet, so naming it here would be a promise rather than a type.
enum class FocusCommandKind
{
	Verb,    //!< does one thing; shows no state
	Toggle,  //!< on/off; renders checkable and MUST show its state (§7.3(3))
	Choice,  //!< one of three named presets; renders checkable (§7.3(3))
	Mode,    //!< navigation among equals (the `mod:` records); renders checkable
};

/*! One command record - §4.1's row, reshaped to what this shell renders.
 *
 *  The registry (`ControlRegistry`) remains the single source of truth for the
 *  COMMAND; this record is the menu's view of it: label, glyph, owner, `where`
 *  path (X5) and the args this mount dispatches with. An id that no mount can
 *  fill the required args for is still a record - greyed with the reason, never
 *  removed (X4).
 */
struct FocusCommandRecord
{
	//! The registry id dispatched (`transport.play`, `settings.set`), or a
	//! `todo.*` placeholder for a command that is still in flight. A `todo.*`
	//! id is NEVER dispatched: it exists so the gap is visible in the menu and
	//! greppable in the tree.
	QString id;
	QString name;   //!< the label a user reads
	QString glyph;  //!< one glyph where the register supplies one; never the only label (P4)
	QString owner;  //!< `global` or the register module id that owns the mount
	QString where;  //!< the human path, e.g. `View ▸ Modules` (X5)
	FocusCommandKind kind = FocusCommandKind::Verb;
	QJsonObject args;  //!< dispatched WITH the id, so one id serves every preset
	//! Non-empty => the item renders greyed with exactly this reason (X4).
	//! Preset for `todo.*` stubs and for register rows the desk refuses.
	QString unavailable;
};

/*! The reason `record` cannot run right now, or an empty string.
 *
 *  Mechanical, in this order: a preset reason wins (stubs, refused rows); an
 *  id no mount can reach is "not registered in this build"; and a record whose
 *  args do not fill the command's `argsSchema.required` names the missing
 *  arguments - which is how a context-free desk menu can offer `clip.split`
 *  honestly instead of dispatching it into a typed error.
 */
QString focusCommandUnavailable(const FocusCommandRecord& record);

/*! The records the desk's own `Commands` menu mounts: the three density
 *  presets and the five workspaces as `settings.set` choices (rows 3 and 7 -
 *  the same command an agent sends, one implementation), plus one entry per
 *  on-train command group the Focus Desk reaches (the wiring rule), plus the
 *  named `todo.*` stubs for the two features still in flight.
 */
QList<FocusCommandRecord> focusDeskCommandRecords();

//! One record per register row: the `mod:` action (§9.4), as it mounts in
//! `View ▸ Modules`. Availability comes from the register itself: an unmounted
//! row keeps the register's own stated reason, a row that may not take the
//! stage says so rather than offering an action the shell would refuse.
QList<FocusCommandRecord> focusDeskModuleRecords(const QList<FocusModule>& rows);

/*! Render `records` into `menu` (append; the caller clears when it rebuilds).
 *
 *  X1's generated menu in Qt: every item is icon-or-label plus its `where`
 *  path on the tooltip, checkable kinds are ticked by `isChecked`, and an
 *  unavailable item is present, disabled and carries its reason. Each action
 *  declares the registry id it implements in the `controlCommand` property -
 *  the same declaration `addFocusDeskToggle` makes (SPEC A11/A15), so the
 *  agent-surface gate sees menu and socket as one implementation.
 */
void populateFocusCommandMenu(QMenu* menu, const QList<FocusCommandRecord>& records,
	const std::function<bool(const FocusCommandRecord&)>& dispatch,
	const std::function<bool(const FocusCommandRecord&)>& isChecked = {});

/*! `View ▸ Modules` - the register rendered as menu entries (work-list row 5).
 *
 *  `desk` may be null (MainWindow's first View-menu build runs before the
 *  pane exists); every entry is then greyed with that reason instead of
 *  offering promotions this window cannot perform.
 */
void addModulesMenu(QMenu* menu, FocusDesk* desk);

} // namespace lmms::gui

#endif // LMMS_GUI_FOCUS_DESK_ACTIONS_H
