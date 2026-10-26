/*
 * RecordLaunchMenus.cpp - the Record and Launcher menus of the menu bar
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
#include "RecordLaunchMenus.h"

#include <QActionGroup>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MenuBarExtras.h"
#include "RecordOptions.h"
#include "SampleTrack.h"
#include "ShellSelection.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

QAction* run(QMenu* menu, const QString& label, const QString& id, const QJsonObject& args = {}, bool ask = false)
{
	return addCommandAction(menu, label, id, [id, args, ask] { return CommandCall{id, args, {}, QString(), ask}; });
}

//! comp.cycle on the selected track at the play position, in @a direction; with no track
//! selected, the form asks which track.
void cycleEntry(QMenu* menu, const QString& label, const QString& direction)
{
	addCommandAction(menu, label, QStringLiteral("comp.cycle"), [label, direction] {
		Track* track = ShellSelection::instance()->track();
		CommandCall call{QStringLiteral("comp.cycle"), {{QStringLiteral("direction"), direction}}, {}, label, track == nullptr};
		if (track != nullptr) { call.fixed.insert(QStringLiteral("track"), control::trackIdOf(track)); }
		return call;
	});
}

//! WF-074: arm (or disarm) every audio track - W1: ONE track.set_arm_many naming them all. Undo refuses the arming (its take
//! files outlive the captures), so disarming through this menu is the way back. A track already in that state is not a
//! failure; the others' refusals are shown together, the last one's reason with them.
void armAllEntry(QMenu* menu, const QString& label, bool armed)
{
	QAction* action = menu->addAction(label);
	action->setProperty("controlCommand", QStringLiteral("track.set_arm_many"));
	action->setEnabled(ControlRegistry::instance()->hasCommand(QStringLiteral("track.set_arm_many")));
	QObject::connect(action, &QAction::triggered, menu, [menu, label, armed] {
		QJsonArray tracks;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (dynamic_cast<SampleTrack*>(track) != nullptr) { tracks.append(control::trackIdOf(track)); }
		}
		if (tracks.isEmpty()) { return; }
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("track.set_arm_many"),
			{{QStringLiteral("tracks"), tracks}, {QStringLiteral("armed"), armed}});
		const CommandCall call{QStringLiteral("track.set_arm_many"), {}, {}, label, false};
		if (!result.ok)
		{
			if (result.errorKind != ControlErrorKind::Busy) { announce(menu->parentWidget(), call, result); }  // all already armed: fine
			return;
		}
		const QJsonArray refused = result.result.value(QStringLiteral("refused")).toArray();
		if (refused.isEmpty()) { return; }
		announce(menu->parentWidget(), call, ControlResult::failure(ControlErrorKind::Refused,
			QObject::tr("%n audio track(s) refused: %1", nullptr, refused.size())
				.arg(refused.last().toObject().value(QStringLiteral("reason")).toString())));
	});
}

//! Theme 7: Record Mode as two choices that each state their scope ("Replace notes (erase the notes it overlaps,
//! any pitch; keep controllers and automation)"), the one in use checked, each running transport.record_options.
void recordModeMenu(QMenu* menu)
{
	QMenu* sub = menu->addMenu(QObject::tr("Record Mode (MIDI)"));
	sub->setToolTipsVisible(true);
	auto* group = new QActionGroup(sub);
	for (const recordoptions::Mode mode : {recordoptions::Mode::Overdub, recordoptions::Mode::Replace})
	{
		const QString name = recordoptions::modeNames().at(static_cast<int>(mode));
		QAction* action = addCommandAction(sub, recordoptions::modeChoice(mode), QStringLiteral("transport.record_options"),
			[name] { return CommandCall{QStringLiteral("transport.record_options"), {{QStringLiteral("mode"), name}}, {}, QString(), false}; });
		action->setCheckable(true);
		action->setToolTip(recordoptions::modeScope(mode));
		group->addAction(action);
	}
	QObject::connect(sub, &QMenu::aboutToShow, sub, [group] {
		const auto current = static_cast<int>(recordoptions::options().mode);
		for (int i = 0; i < group->actions().size(); ++i) { group->actions().at(i)->setChecked(i == current); }
	});
}

//! The Launch Scene items stay enabled only for the scenes the launcher's grid has.
void followSceneCount(QMenu* menu, const QList<QAction*>& scenes)
{
	const bool registered = ControlRegistry::instance()->hasCommand(QStringLiteral("session.launch_scene"));
	QObject::connect(menu, &QMenu::aboutToShow, menu, [scenes, registered] {
		const int count = Engine::getSong()->sessionModel().sceneCount();
		for (int scene = 0; scene < scenes.size(); ++scene) { scenes[scene]->setEnabled(registered && scene < count); }
	});
}

void fillRecord(QMenu* menu)
{
	run(menu, QObject::tr("Record"), QStringLiteral("transport.record"));
	run(menu, QObject::tr("Count-In and Pre-Roll..."), QStringLiteral("transport.record_options"), {}, true);
	run(menu, QObject::tr("Metronome Sound and Level..."), QStringLiteral("transport.metronome_options"), {}, true);  // WF-081
	addCommandAction(menu, QObject::tr("Input Level"), QStringLiteral("meter.input"),  // MX-134
		[] { return CommandCall{QStringLiteral("meter.input"), {}, {}, QString(), false, true}; });
	run(menu, QObject::tr("Record From the Master or a Channel..."), QStringLiteral("record.route_source"), {}, true);  // WF-072/086
	run(menu, QObject::tr("Punch In Here"), QStringLiteral("transport.punch_here"), {{QStringLiteral("edge"), QStringLiteral("in")}});  // WF-079
	run(menu, QObject::tr("Punch Out Here"), QStringLiteral("transport.punch_here"), {{QStringLiteral("edge"), QStringLiteral("out")}});
	recordModeMenu(menu);
	menu->addSeparator();
	armAllEntry(menu, QObject::tr("Arm All Audio Tracks"), true);
	run(menu, QObject::tr("Disarm All Tracks"), QStringLiteral("record.disarm_all"));
	menu->addSeparator();
	cycleEntry(menu, QObject::tr("Next Take Here"), QStringLiteral("next"));
	cycleEntry(menu, QObject::tr("Previous Take Here"), QStringLiteral("previous"));
}

void fillLauncher(QMenu* menu)
{
	QList<QAction*> scenes;
	for (int scene = 0; scene < 8; ++scene)
	{
		scenes << run(menu, QObject::tr("Launch Scene %1").arg(scene + 1), QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), scene}});
	}
	followSceneCount(menu, scenes);
	run(menu, QObject::tr("Stop All Clips"), QStringLiteral("session.stop_all"));
	run(menu, QObject::tr("Record What I Played Into a Cell..."), QStringLiteral("session.capture_to_slot"), {}, true);
	// WF-071: perform mode, the launcher's live show window.
	run(menu, QObject::tr("Perform Mode"), QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("perform")}});
	menu->addSeparator();
	QMenu* controller = menu->addMenu(QObject::tr("Controller"));
	run(controller, QObject::tr("Connect..."), QStringLiteral("session.bindings_connect"), {}, true);
	run(controller, QObject::tr("Bind the Next Note..."), QStringLiteral("session.bind_learn"), {}, true);
	run(controller, QObject::tr("Bind a Note..."), QStringLiteral("session.bind"), {}, true);
	run(controller, QObject::tr("Unbind a Note..."), QStringLiteral("session.unbind"), {}, true);
	addCommandAction(controller, QObject::tr("Bindings"), QStringLiteral("session.bindings"),
		[] { return CommandCall{QStringLiteral("session.bindings"), {}, {}, QString(), false, true}; });
	// Q050-217.1: a note or a control change runs a built-in command that needs no arguments.
	controller->addSeparator();
	run(controller, QObject::tr("Bind a Command to a Note or Control Change..."), QStringLiteral("session.bind_command"), {}, true);
	run(controller, QObject::tr("Unbind a Command..."), QStringLiteral("session.unbind_command"), {}, true);
	addCommandAction(controller, QObject::tr("Command Bindings"), QStringLiteral("session.command_bindings"),
		[] { return CommandCall{QStringLiteral("session.command_bindings"), {}, {}, QString(), false, true}; });
}

QMenu* install(QMenuBar* bar, const QString& title, const QString& name, void (*fill)(QMenu*))
{
	auto* menu = new QMenu(title, bar->window());
	menu->setObjectName(name);
	QMenu* placed = insertBeforeHelp(bar, menu);
	if (placed == menu) { fill(menu); }
	return placed;
}

} // namespace

void installRecordLaunchMenus(QMenuBar* bar)
{
	install(bar, QObject::tr("Re&cord"), QStringLiteral("recordMenu"), fillRecord);
	install(bar, QObject::tr("La&uncher"), QStringLiteral("launcherMenu"), fillLauncher);
}

} // namespace lmms::gui
