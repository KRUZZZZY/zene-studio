/*
 * FocusDeskModules.h - the Focus Desk module register (UI plan §4.2).
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

#ifndef LMMS_GUI_FOCUS_DESK_MODULES_H
#define LMMS_GUI_FOCUS_DESK_MODULES_H

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

namespace lmms::gui
{

//! A region of the Focus Desk shell (UI plan §5.1).
//!
//! The values are a bitmask on purpose: a row's *allowed* set is then one int,
//! so "may this module live here?" is `allowed & regionBit(region)` and the
//! register needs no container per row.
enum class FocusRegion
{
	Left   = 1,
	Centre = 2,
	Right  = 4,
	Bottom = 8,
};

//! A module's density contract (UI plan §4.3(4), §8.1).
enum class FocusDensity
{
	Minimal,
	Standard,
	Complete,
};

//! One row of the module register (UI plan §4.2).
//!
//! The register is the answer to "what may exist": a module that is not in it
//! cannot appear in the shell at all (§4.2). Every column §4.2 names is here,
//! except the owning-command count and the region bitmask, which are reshaped
//! and are documented at their field.
struct FocusModule
{
	QString id;                        //!< stable id; the `mod:` action's argument
	QString title;                     //!< the label a user reads
	QString glyph;                     //!< one glyph, so the strip is scannable
	int allowed = 0;                   //!< FocusRegion bitmask, not a container
	FocusRegion home = FocusRegion::Centre;  //!< the region it starts in
	//! May it be the focus (stage) module? §4.3(6)'s focus contract. The default
	//! here is only a struct default: v1Register() states the column on every
	//! row, which is why the invariant below is checkable rather than assumed.
	//! Invariant: `focusable` implies FocusRegion::Centre is in `allowed`, since
	//! the stage is the centre region - `validate()` enforces it.
	bool focusable = true;
	bool tearOff = false;              //!< may it leave the desk? (§4.3(7)); no (P1)
	FocusDensity minDensity = FocusDensity::Standard;
	int order = 0;                     //!< default order within its region
	QString menuId;                    //!< the menu it owns (M1); menus are §10 item 4
	QStringList stateControls;         //!< state, exempt from density hiding (§4.3(5))
	//! Why no live widget mounts for this row, empty when one does. This is the
	//! string the shell shows on a chip it must grey out (§4.1 X4: greyed with a
	//! reason, never removed) - so a staged module is visible and explained
	//! rather than silently absent, which is §4.2's "written down in the
	//! product" claim made checkable.
	QString unmounted;
};

//! The register and the pure questions the shell asks of it.
//!
//! Deliberately widget-free: the shell's layout can only be exercised under a
//! QApplication, but every rule about what may exist is testable without one.
class FocusDeskModules
{
	Q_DECLARE_TR_FUNCTIONS(FocusDeskModules)

public:
	//! The v1 register: eleven rows, capped by UI plan §4.2 and justified there
	//! against Bitwig's twelve panels and Ableton's wider set. `commandCount` is
	//! absent on purpose: it is a property of the command record (§10 item 4),
	//! which is staged, and a number here would be invented rather than measured.
	static QList<FocusModule> v1Register();

	static int indexOf(const QList<FocusModule>& rows, const QString& id);
	static const FocusModule* find(const QList<FocusModule>& rows, const QString& id);

	//! Rows that may be promoted to the stage, in register order.
	static QList<FocusModule> focusable(const QList<FocusModule>& rows);
	//! Rows whose home is `region`, in register order.
	static QList<FocusModule> homedIn(const QList<FocusModule>& rows, FocusRegion region);

	static bool canOccupy(const FocusModule& row, FocusRegion region);

	//! Every rule the register must satisfy, as a list of violations. An empty
	//! list is a valid register. Used by the test and by the shell's
	//! construction, so a bad row is reported by name rather than mis-rendered.
	static QStringList validate(const QList<FocusModule>& rows);

	static QString regionName(FocusRegion region);
	static QString densityName(FocusDensity density);
	static bool densityFromName(const QString& name, FocusDensity* out);
	static FocusDensity nextDensity(FocusDensity density);
};

//! The id of the one action the FOCUS strip dispatches (UI plan §9.4).
//!
//! §9.4 says switching "reuses the registry's `mod:` action". The action id is
//! built here so the strip and a future ControlRegistry registration cannot
//! disagree about its spelling - but the registration itself is staged: adding
//! a command id obliges an A16 reversibility row, and the A16 histogram is
//! published in docs/RELEASE-NOTES-v0.3.0-alpha.md, which is a shipped record.
//! See src/gui/FocusDesk.cpp for the seam this id is dispatched through.
QString focusActionCommandId(const QString& moduleId);

} // namespace lmms::gui

#endif // LMMS_GUI_FOCUS_DESK_MODULES_H
