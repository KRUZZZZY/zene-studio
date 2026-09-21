/*
 * FocusDeskModules.cpp - the Focus Desk module register (UI plan §4.2).
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

#include "FocusDeskModules.h"

#include <QSet>

#include <initializer_list>

namespace lmms::gui
{

namespace
{

constexpr int regionBit(FocusRegion region)
{
	return static_cast<int>(region);
}

//! OR a row's allowed regions into the one bitmask `FocusModule::allowed` is.
//! An initializer_list rather than defaulted parameters: a default chosen to be
//! "the identity" silently changes which regions a two-region call allows.
int bits(std::initializer_list<FocusRegion> regions)
{
	int mask = 0;
	for (const auto region : regions) { mask |= regionBit(region); }
	return mask;
}

//! Build a row, so the eleven below stay readable as a table.
//!
//! `focusable` has no default on purpose. §4.3(6) makes it a declared column of
//! the register, and a row that inherited `true` from the struct would be a row
//! whose focus contract nobody wrote down - which is how a module the table
//! forbids from the centre region ends up advertised as promotable.
FocusModule row(const QString& id, const QString& title, const QString& glyph,
	int allowed, FocusRegion home, int order, const QStringList& stateControls,
	bool focusable, const QString& unmounted = QString())
{
	FocusModule m;
	m.id = id;
	m.title = title;
	m.glyph = glyph;
	m.allowed = allowed;
	m.home = home;
	m.order = order;
	m.stateControls = stateControls;
	m.focusable = focusable;
	m.menuId = QStringLiteral("menu.") + id;
	m.unmounted = unmounted;
	return m;
}

//! The rules about a row's identity and its route to its own commands (§4.2,
//! M1). Split out of validate() because the complexity gate counts branches per
//! function, and a validator that trips that gate is a register nobody can
//! extend - the rules would be right and unaffordable.
void checkRowIdentity(const FocusModule& r, QSet<QString>& seen, QStringList& bad)
{
	if (r.id.isEmpty())
	{
		bad.append(FocusDeskModules::tr("a row has an empty id"));
		return;
	}
	if (seen.contains(r.id))
	{
		bad.append(FocusDeskModules::tr("duplicate row id '%1'").arg(r.id));
	}
	seen.insert(r.id);

	if (r.title.isEmpty()) { bad.append(FocusDeskModules::tr("row '%1' has an empty title").arg(r.id)); }
	if (r.glyph.isEmpty()) { bad.append(FocusDeskModules::tr("row '%1' has no glyph").arg(r.id)); }
	if (r.allowed == 0) { bad.append(FocusDeskModules::tr("row '%1' allows no region").arg(r.id)); }
	// §4.2: a module that is not in the register cannot exist, so every row owns
	// a menu id or it has no route to its own commands (M1).
	if (r.menuId.isEmpty()) { bad.append(FocusDeskModules::tr("row '%1' owns no menu id").arg(r.id)); }
}

//! The rules about where a row may live, and what that implies for the stage.
void checkRowPlacement(const FocusModule& r, QStringList& bad)
{
	if (!FocusDeskModules::canOccupy(r, r.home))
	{
		bad.append(FocusDeskModules::tr("row '%1' is homed in %2, which it may not occupy")
			.arg(r.id, FocusDeskModules::regionName(r.home)));
	}
	// A focusable row must be promotable to the stage, and the stage is the
	// centre region - so `focusable` without Centre is a row whose chip would
	// promise something the layout cannot deliver.
	if (r.focusable && !FocusDeskModules::canOccupy(r, FocusRegion::Centre))
	{
		bad.append(FocusDeskModules::tr("row '%1' is focusable but may not occupy the stage")
			.arg(r.id));
	}
}

} // namespace

QList<FocusModule> FocusDeskModules::v1Register()
{
	// The eleven rows of UI plan §4.2, in that table's order. `allowed` is that
	// table's "May occupy" column verbatim - the region sets are the plan's, not
	// this build's convenience, which is what makes the table the authority on
	// what may exist and the validator worth having. `home` is its "Default"
	// column.
	//
	// `focusable` is §4.3(6)'s focus contract, and it is written out per row
	// because it is not free: the stage *is* the centre region (§5.1, §9.4), so
	// a row the table keeps out of the centre cannot be promoted, and saying so
	// here is what stops a chip promising a placement the layout cannot deliver.
	// Six rows are therefore not focusable: browser, inspector, device chain,
	// modulation, automation and project. They are not second-class - they have
	// a rail or the bottom dock to live in - they simply cannot take the stage.
	//
	// `unmounted` is set only where this build has no widget to mount the row
	// onto; the shell shows it on the chip, so a reserved slot is visible and
	// explained (§4.2). The rows without a reason are the ones MainWindow can
	// mount today: arrangement, mixer, detail editor, automation, device chain
	// and project. The browser is the one exception worth reading twice: it *is*
	// implemented, as the product's own sidebar, and the reason says so rather
	// than implying the module is missing.
	return {
		row(QStringLiteral("browser"), tr("Browser"), QStringLiteral("\U0001F5C2"),
			bits({FocusRegion::Left, FocusRegion::Bottom}), FocusRegion::Left, 1,
			{QStringLiteral("favourites")}, false,
			tr("the browser is the product's sidebar and stays docked outside the desk")),
		row(QStringLiteral("arrangement"), tr("Arrangement"), QStringLiteral("\u25A4"),
			bits({FocusRegion::Centre, FocusRegion::Bottom}), FocusRegion::Centre, 2,
			{QStringLiteral("mute"), QStringLiteral("solo"), QStringLiteral("record-arm")}, true),
		row(QStringLiteral("session"), tr("Session grid"), QStringLiteral("\u25A6"),
			bits({FocusRegion::Centre}), FocusRegion::Centre, 3,
			{QStringLiteral("clip-launch"), QStringLiteral("record-arm")}, true,
			tr("the session grid's model is a reserved slot, not a deliverable (plan §5.5)")),
		row(QStringLiteral("inspector"), tr("Inspector"), QStringLiteral("\u24D8"),
			bits({FocusRegion::Right, FocusRegion::Bottom}), FocusRegion::Right, 4,
			{}, false,
			tr("the selection-driven inspector (plan D1) is not yet a module in the tree")),
		row(QStringLiteral("devicechain"), tr("Device chain"), QStringLiteral("\u26D3"),
			bits({FocusRegion::Right, FocusRegion::Bottom}), FocusRegion::Right, 5,
			{QStringLiteral("device-enable")}, false),
		row(QStringLiteral("modulation"), tr("Modulation"), QStringLiteral("\u223F"),
			bits({FocusRegion::Right}), FocusRegion::Right, 6,
			{QStringLiteral("routing-mode")}, false,
			tr("the modulation module lands with the modulation work (plan §5.4)")),
		row(QStringLiteral("detaileditor"), tr("Detail editor"), QStringLiteral("\U0001F3B9"),
			bits({FocusRegion::Centre, FocusRegion::Bottom}), FocusRegion::Bottom, 7,
			{QStringLiteral("record-arm")}, true),
		row(QStringLiteral("mixer"), tr("Mixer"), QStringLiteral("\U0001D162"),
			bits({FocusRegion::Centre, FocusRegion::Bottom}), FocusRegion::Bottom, 8,
			{QStringLiteral("mute"), QStringLiteral("solo"), QStringLiteral("record-arm")}, true),
		row(QStringLiteral("automation"), tr("Automation"), QStringLiteral("\u223F"),
			bits({FocusRegion::Bottom}), FocusRegion::Bottom, 9,
			{QStringLiteral("automation-mode"), QStringLiteral("record-arm")}, false),
		row(QStringLiteral("project"), tr("Project & mappings"), QStringLiteral("\U0001F4CB"),
			bits({FocusRegion::Left, FocusRegion::Bottom}), FocusRegion::Left, 10,
			{}, false),
		row(QStringLiteral("learn"), tr("Learn"), QStringLiteral("\u266B"),
			bits({FocusRegion::Centre, FocusRegion::Left}), FocusRegion::Left, 11,
			{QStringLiteral("learn-mode")}, true,
			tr("the learn module is the pull surface and ships with plan §10 item 11")),
	};
}

int FocusDeskModules::indexOf(const QList<FocusModule>& rows, const QString& id)
{
	for (int i = 0; i < rows.size(); ++i)
	{
		if (rows.at(i).id == id) { return i; }
	}
	return -1;
}

const FocusModule* FocusDeskModules::find(const QList<FocusModule>& rows, const QString& id)
{
	const int i = indexOf(rows, id);
	return (i < 0) ? nullptr : &rows.at(i);
}

QList<FocusModule> FocusDeskModules::focusable(const QList<FocusModule>& rows)
{
	QList<FocusModule> out;
	for (const auto& r : rows)
	{
		if (r.focusable) { out.append(r); }
	}
	return out;
}

QList<FocusModule> FocusDeskModules::homedIn(const QList<FocusModule>& rows, FocusRegion region)
{
	QList<FocusModule> out;
	for (const auto& r : rows)
	{
		if (r.home == region) { out.append(r); }
	}
	return out;
}

bool FocusDeskModules::canOccupy(const FocusModule& row, FocusRegion region)
{
	return (row.allowed & regionBit(region)) != 0;
}

QStringList FocusDeskModules::validate(const QList<FocusModule>& rows)
{
	QStringList bad;
	if (rows.isEmpty()) { bad.append(tr("the register is empty")); }

	// A row with no id is reported once and then skipped: every other rule needs
	// to name the row it is about, so the message would be about nothing.
	QSet<QString> seen;
	for (const auto& r : rows)
	{
		checkRowIdentity(r, seen, bad);
		if (!r.id.isEmpty()) { checkRowPlacement(r, bad); }
	}
	return bad;
}

QString FocusDeskModules::regionName(FocusRegion region)
{
	switch (region)
	{
		case FocusRegion::Left:   return tr("left rail");
		case FocusRegion::Right:  return tr("right rail");
		case FocusRegion::Bottom: return tr("bottom");
		case FocusRegion::Centre: return tr("stage");
	}
	return tr("unknown");
}

QString FocusDeskModules::densityName(FocusDensity density)
{
	switch (density)
	{
		case FocusDensity::Minimal:  return tr("minimal");
		case FocusDensity::Standard: return tr("standard");
		case FocusDensity::Complete: return tr("complete");
	}
	return tr("standard");
}

bool FocusDeskModules::densityFromName(const QString& name, FocusDensity* out)
{
	const QString key = name.trimmed().toLower();
	if (key == QStringLiteral("minimal"))  { *out = FocusDensity::Minimal;  return true; }
	if (key == QStringLiteral("standard")) { *out = FocusDensity::Standard; return true; }
	if (key == QStringLiteral("complete")) { *out = FocusDensity::Complete; return true; }
	return false;
}

FocusDensity FocusDeskModules::nextDensity(FocusDensity density)
{
	switch (density)
	{
		case FocusDensity::Minimal:  return FocusDensity::Standard;
		case FocusDensity::Standard: return FocusDensity::Complete;
		case FocusDensity::Complete: return FocusDensity::Minimal;
	}
	return FocusDensity::Standard;
}

QString focusActionCommandId(const QString& moduleId)
{
	return QStringLiteral("mod:") + moduleId;
}

} // namespace lmms::gui
