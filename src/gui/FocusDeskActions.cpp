/*
 * FocusDeskActions.cpp - the Focus Desk's command record, its generated menus
 *                         and the `mod:` dispatch seam (UI plan §4.1, §7.3,
 *                         §9.4; work-list rows 4 and 5, and the strip controls
 *                         rows 3's density contract hangs its reveal-hint on).
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

#include "FocusDeskActions.h"

#include <QAction>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QToolButton>

#include "Accessibility.h"
#include "ControlRegistry.h"
#include "FocusDesk.h"
#include "FocusDeskModules.h"

namespace lmms::gui
{

namespace
{

//! The two config keys this file's `settings.set` records write, spelled the
//! same way FocusDeskPane.cpp spells them - a key written under two spellings
//! is two settings, and the pane would follow only one of them.
QString densityConfigKey() { return QStringLiteral("ui/focusdesk.density"); }
QString workspaceConfigKey() { return QStringLiteral("ui/focusdesk.workspace"); }

FocusCommandRecord makeRecord(const QString& id, const QString& name,
	const QString& where, const QString& owner = QStringLiteral("global"),
	FocusCommandKind kind = FocusCommandKind::Verb, const QJsonObject& args = QJsonObject())
{
	FocusCommandRecord r;
	r.id = id;
	r.name = name;
	r.owner = owner;
	r.where = where;
	r.kind = kind;
	r.args = args;
	return r;
}

//! A `settings.set` choice record: one command id, five (or three) arg sets -
//! which is why the density presets and the workspaces are records dispatching
//! ONE registry command rather than nine invented ones.
FocusCommandRecord settingChoice(const QString& name, const QString& where,
	const QString& key, const QString& value)
{
	return makeRecord(QStringLiteral("settings.set"), name, where,
		QStringLiteral("global"), FocusCommandKind::Choice,
		QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}});
}

//! The args `record` does not fill from the command's own `argsSchema.required`
//! - the mechanical half of X4. Empty when the command needs nothing this
//! mount does not carry, and empty when there is no command to ask (the
//! caller has already reported that case by id).
QString missingRequiredArgs(const FocusCommandRecord& record)
{
	const ControlCommand* cmd = ControlRegistry::instance()->command(record.id);
	if (cmd == nullptr) { return {}; }

	QStringList missing;
	const auto required = cmd->argsSchema.value(QStringLiteral("required")).toArray();
	for (const auto& entry : required)
	{
		const QString key = entry.toString();
		if (!key.isEmpty() && !record.args.contains(key)) { missing.append(key); }
	}
	if (missing.isEmpty()) { return {}; }
	return QObject::tr("needs argument(s) this menu has no selection to pass: %1")
		.arg(missing.join(QLatin1String(", ")));
}

//! Whether a `Choice` record is the value the desk currently holds. The
//! record's own id/args are the comparison - a density record is "checked"
//! exactly when the desk's density IS its value.
bool choiceChecked(const FocusCommandRecord& record, const FocusDesk& desk)
{
	if (record.id != QStringLiteral("settings.set")) { return false; }
	const QString key = record.args.value(QStringLiteral("key")).toString();
	const QString value = record.args.value(QStringLiteral("value")).toString();
	if (key == densityConfigKey()) { return value == FocusDeskModules::densityName(desk.density()); }
	if (key == workspaceConfigKey()) { return value == desk.workspace(); }
	return false;
}

//! The dispatch every registry-backed mount shares: refuse what is greyed
//! (twice-checked on purpose - availability can move between open and click),
//! then one invoke. This is the "menus call the registry entry" half of A11.
bool dispatchRecord(const FocusCommandRecord& record)
{
	if (!focusCommandUnavailable(record).isEmpty()) { return false; }
	return ControlRegistry::instance()->invoke(record.id, record.args).ok;
}

} // namespace

QString focusCommandUnavailable(const FocusCommandRecord& record)
{
	if (!record.unavailable.isEmpty()) { return record.unavailable; }
	if (record.id.startsWith(QStringLiteral("todo.")))
	{
		return QObject::tr("'%1' is not on the control surface yet").arg(record.id);
	}
	// The `mod:` ids are the desk's OWN seam (FocusDesk::dispatchAction:
	// registry-first, desk-local while that group is staged - see
	// FocusDeskModules.h), so registry presence is not what makes one
	// available: the register's preset reasons above already say when the
	// shell must refuse it, and everything else dispatches.
	if (record.id.startsWith(QStringLiteral("mod:"))) { return QString(); }
	if (!ControlRegistry::instance()->hasCommand(record.id))
	{
		return QObject::tr("'%1' is not registered in this build").arg(record.id);
	}
	return missingRequiredArgs(record);
}

QList<FocusCommandRecord> focusDeskCommandRecords()
{
	QList<FocusCommandRecord> out;

	// Row 3 - the three named density presets as `settings.set` choices: the
	// strip button cycles them locally, this menu and an agent on the socket
	// send the same command, and FocusDeskPane's observer applies either.
	for (const auto& value : {QStringLiteral("minimal"), QStringLiteral("standard"),
			QStringLiteral("complete")})
	{
		out.append(settingChoice(QObject::tr("Density: %1").arg(QString(value).left(1).toUpper() + value.mid(1)),
			QObject::tr("View ▸ Density"), densityConfigKey(), value));
	}

	// Row 7 - the five workspaces of §6.1, also `settings.set` choices. The
	// scope C2 requires is stated by the strip's workspace switcher label; the
	// id is the data value FocusDeskWorkspaces::find() answers to.
	for (const auto& value : {QStringLiteral("compose"), QStringLiteral("record"),
			QStringLiteral("mix"), QStringLiteral("design"), QStringLiteral("perform")})
	{
		out.append(settingChoice(QObject::tr("Workspace: %1").arg(QString(value).left(1).toUpper() + value.mid(1)),
			QObject::tr("Workspace"), workspaceConfigKey(), value));
	}

	// The wiring rule: one entry per on-train group the desk reaches. Reads
	// dispatch live; verbs whose argsSchema.required this mount cannot fill
	// render greyed with that exact list (X4) instead of firing a typed error.
	out.append(makeRecord(QStringLiteral("transport.play"), QObject::tr("Play"), QObject::tr("Transport")));
	out.append(makeRecord(QStringLiteral("transport.stop"), QObject::tr("Stop"), QObject::tr("Transport")));
	out.append(makeRecord(QStringLiteral("track.list"), QObject::tr("List tracks"), QObject::tr("Track")));
	out.append(makeRecord(QStringLiteral("track.get_state"), QObject::tr("Track state"), QObject::tr("Track")));
	out.append(makeRecord(QStringLiteral("clip.link_get_state"), QObject::tr("Linked-clip state"), QObject::tr("Clip")));
	out.append(makeRecord(QStringLiteral("clip.split"), QObject::tr("Split clip"), QObject::tr("Clip")));
	out.append(makeRecord(QStringLiteral("note.random_seed_get"), QObject::tr("Randomisation seed"), QObject::tr("Note")));
	out.append(makeRecord(QStringLiteral("note.transpose"), QObject::tr("Transpose notes"), QObject::tr("Note")));
	out.append(makeRecord(QStringLiteral("sample.generate"), QObject::tr("Generate a buffer"), QObject::tr("Sample")));
	out.append(makeRecord(QStringLiteral("sample.reverse"), QObject::tr("Reverse a clip"), QObject::tr("Sample")));
	out.append(makeRecord(QStringLiteral("livecode.get_state"), QObject::tr("Scheduled-script state"), QObject::tr("Live code")));
	out.append(makeRecord(QStringLiteral("livecode.schedule"), QObject::tr("Schedule a script"), QObject::tr("Live code")));
	out.append(makeRecord(QStringLiteral("feedback.get_state"), QObject::tr("Cycle-permitted state"), QObject::tr("Routing")));
	out.append(makeRecord(QStringLiteral("feedback.enable"), QObject::tr("Allow feedback cycles"), QObject::tr("Routing")));
	out.append(makeRecord(QStringLiteral("feedback.disable"), QObject::tr("Forbid feedback cycles"), QObject::tr("Routing")));
	out.append(makeRecord(QStringLiteral("mixer.get_state"), QObject::tr("Mixer state"), QObject::tr("Routing")));
	out.append(makeRecord(QStringLiteral("mixer.route_to"), QObject::tr("Route a channel"), QObject::tr("Routing")));
	out.append(makeRecord(QStringLiteral("routing.get_state"), QObject::tr("Signal graph"), QObject::tr("Routing")));

	// UI-FREEZE (2026-09-24): the two placeholder features LANDED on the train
	// (#712's mts.* group, S7's take lanes under comp.lane_*) - the stubs are
	// wired to the real commands now, and NOTHING is left in flight at freeze.
	out.append(makeRecord(QStringLiteral("mts.get_state"), QObject::tr("MTS-ESP tuning state"), QObject::tr("Tuning")));
	out.append(makeRecord(QStringLiteral("mts.load_scale"), QObject::tr("Load a Scala scale"), QObject::tr("Tuning")));
	out.append(makeRecord(QStringLiteral("mts.reset"), QObject::tr("Reset to 12-TET"), QObject::tr("Tuning")));
	out.append(makeRecord(QStringLiteral("comp.lane_list"), QObject::tr("List take lanes"), QObject::tr("Track")));
	out.append(makeRecord(QStringLiteral("comp.lane_add"), QObject::tr("Add a take lane"), QObject::tr("Track")));
	out.append(makeRecord(QStringLiteral("comp.select"), QObject::tr("Select the composite take"), QObject::tr("Track")));

	return out;
}

QList<FocusCommandRecord> focusDeskModuleRecords(const QList<FocusModule>& rows)
{
	QList<FocusCommandRecord> out;
	out.reserve(rows.size());
	for (const auto& row : rows)
	{
		FocusCommandRecord r = makeRecord(focusActionCommandId(row.id), row.title,
			QObject::tr("View ▸ Modules"), row.id, FocusCommandKind::Mode);
		r.glyph = row.glyph;
		// Availability IS the register (§4.2): the register's own stated
		// reason for an unmounted row, or the focus contract's for a row that
		// may not take the stage. Never "hidden because the menu says so".
		if (!row.unmounted.isEmpty())
		{
			r.unavailable = QObject::tr("not shown: %1").arg(row.unmounted);
		}
		else if (!row.focusable)
		{
			r.unavailable = QObject::tr("cannot take the stage - lives in the %1")
				.arg(FocusDeskModules::regionName(row.home));
		}
		out.append(r);
	}
	return out;
}

void populateFocusCommandMenu(QMenu* menu, const QList<FocusCommandRecord>& records,
	const std::function<bool(const FocusCommandRecord&)>& dispatch,
	const std::function<bool(const FocusCommandRecord&)>& isChecked)
{
	if (menu == nullptr) { return; }
	for (const auto& record : records)
	{
		const QString unavailable = focusCommandUnavailable(record);
		const QString text = record.glyph.isEmpty()
			? record.name : record.glyph + QStringLiteral("  ") + record.name;
		QAction* action = menu->addAction(text);
		// The A11/A15 declaration: the action names the registry command it
		// implements, so menu and socket are one implementation and the
		// agent-surface gate reads the id off the action itself.
		action->setProperty("controlCommand", record.id);
		action->setProperty("wherePath", record.where);

		const bool checkable = record.kind == FocusCommandKind::Toggle
			|| record.kind == FocusCommandKind::Choice
			|| record.kind == FocusCommandKind::Mode;
		action->setCheckable(checkable);
		if (checkable && isChecked) { action->setChecked(isChecked(record)); }

		action->setDisabled(!unavailable.isEmpty());
		const QString tip = unavailable.isEmpty()
			? QObject::tr("%1 - %2").arg(record.name, record.where)
			: QObject::tr("%1 - not available: %2").arg(record.name, unavailable);
		action->setToolTip(tip);
		action->setStatusTip(tip);
		QObject::connect(action, &QAction::triggered, action,
			[dispatch, record]() { dispatch(record); });
	}
}

void addModulesMenu(QMenu* menu, FocusDesk* desk)
{
	if (menu == nullptr) { return; }

	// A real SUBMENU, not a section in View: `View ▸ Modules` is what §7.2
	// names, and it gives the agent-surface gate a container narrow enough to
	// exempt (tests/agent-surface-exempt.txt: `path:Modules`) - this menu's
	// content is generated from the register, the same category as `path:Tools.
	QMenu* modules = menu->addMenu(QObject::tr("Modules"));
	modules->setObjectName(QStringLiteral("focusDeskModulesMenu"));

	const auto rows = desk != nullptr ? desk->registerRows() : FocusDeskModules::v1Register();
	auto records = focusDeskModuleRecords(rows);
	for (auto& record : records)
	{
		if (desk == nullptr)
		{
			// No desk, no promotion: present, greyed, one stated reason - the
			// same rule addFocusDeskToggle's null pane follows (X4).
			record.unavailable = QObject::tr("the Focus Desk is not built in this window");
			continue;
		}
		if (record.unavailable.isEmpty() && desk->mountedWidget(record.id.mid(4)) == nullptr)
		{
			// The row is showable in this build but no editor sits on the desk
			// yet (the pane claims them when the desk is activated): the menu
			// says so instead of offering a promotion that would be refused.
			record.unavailable = QObject::tr(
				"not on the desk yet - activate the Focus Desk so it can claim the editors");
		}
	}

	populateFocusCommandMenu(modules, records,
		[desk](const FocusCommandRecord& record)
		{ return desk != nullptr && desk->dispatchAction(record.id); },
		[desk](const FocusCommandRecord& record)
		{ return desk != nullptr && desk->focused() == record.id.mid(4); });
}

void FocusDesk::buildCommandsButton()
{
	m_commandsButton = new QToolButton(m_strip);
	m_commandsButton->setObjectName(QStringLiteral("focusDeskCommands"));
	m_commandsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_commandsButton->setAutoRaise(true);
	m_commandsButton->setPopupMode(QToolButton::InstantPopup);
	m_commandsButton->setFocusPolicy(Qt::TabFocus);
	const QString tip = tr("Commands the Focus Desk mounts: density presets, "
		"workspaces and one entry per command group. An entry that cannot run "
		"is shown greyed with the reason, never removed.");
	m_commandsButton->setToolTip(tip);
	lmms::a11y::describe(m_commandsButton, tip);

	auto* menu = new QMenu(m_commandsButton);
	// Rebuilt on every open: checkable choices tick against the desk's LIVE
	// state, and availability is asked of the registry at render time.
	QObject::connect(menu, &QMenu::aboutToShow, this,
		[this, menu]()
		{
			menu->clear();
			populateFocusCommandMenu(menu, focusDeskCommandRecords(), dispatchRecord,
				[this](const FocusCommandRecord& record) { return choiceChecked(record, *this); });
		});
	m_commandsButton->setMenu(menu);

	if (auto* layout = m_strip->layout(); layout != nullptr) { layout->addWidget(m_commandsButton); }
}

void FocusDesk::buildDensityHint()
{
	m_revealHint = new QLabel(m_strip);
	m_revealHint->setObjectName(QStringLiteral("focusDeskRevealHint"));
	m_revealHint->setText(tr("Minimal hides the rail bodies - switch density "
		"back to reveal them."));
	m_revealHint->hide();
	const QString tip = tr("The reveal-hint §8.1 requires: where a preset hides "
		"something, this says what and offers the one-click way back.");
	m_revealHint->setToolTip(tip);
	lmms::a11y::describe(m_revealHint, tip);
	if (auto* layout = m_strip->layout(); layout != nullptr) { layout->addWidget(m_revealHint); }
}

bool FocusDesk::dispatchAction(const QString& actionId)
{
	// §9.4's one seam - it lives in the actions TU beside the record type the
	// menus render, so dispatch and menu cannot drift apart (the id itself has
	// exactly one spelling, focusActionCommandId). The registry's `mod:`
	// action IS the command, so a registered id wins and every mount shares
	// one implementation (A11); while that group is staged (see
	// FocusDeskModules.h), the desk answers its own `mod:<id>` ids with
	// focusModule(), which carries the register's refusal reasons verbatim.
	if (ControlRegistry::instance()->hasCommand(actionId))
	{
		const ControlResult result = ControlRegistry::instance()->invoke(actionId);
		if (!result.ok)
		{
			emit moduleRefused(actionId, result.errorMessage);
			return false;
		}
		return true;
	}
	if (actionId.startsWith(QStringLiteral("mod:"))) { return focusModule(actionId.mid(4)); }
	emit moduleRefused(actionId,
		tr("'%1' is not a Focus Desk action and is not on the control surface")
			.arg(actionId));
	return false;
}

} // namespace lmms::gui
