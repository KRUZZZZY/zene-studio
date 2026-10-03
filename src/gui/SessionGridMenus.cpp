/*
 * SessionGridMenus.cpp - the clip launcher's context menus: cells, scenes and the grid
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

#include "SessionGridMenus.h"

#include <algorithm>

#include <QMenu>

#include <QJsonArray>

#include "CommandForm.h"
#include "FollowActionEditor.h"
#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "Engine.h"

namespace lmms::gui::SessionGridMenus
{

namespace
{

QJsonObject cellArgs(int track, int scene)
{
	return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}};
}

CommandCall call(const QString& id, const QJsonObject& fixed, const QString& title, bool ask = true)
{
	return CommandCall{id, fixed, {}, title, ask};
}

//! WF-054: put a pattern into the cell by name (session.pattern_list), not by its number. It sets the cell's
//! clip and launches nothing, so the entry says Set, not Play.
void addPatternPicker(QMenu* menu, const QJsonObject& where, const QString& title)
{
	QMenu* patterns = menu->addMenu(QObject::tr("Set Pattern"));
	const QJsonArray listed = ControlRegistry::instance()->invoke(QStringLiteral("session.pattern_list")).result
		.value(QStringLiteral("patterns")).toArray();
	for (const QJsonValue& value : listed)
	{
		const QJsonObject pattern = value.toObject();
		const QString name = pattern.value(QStringLiteral("name")).toString();
		QJsonObject args = where;
		args.insert(QStringLiteral("type"), QStringLiteral("midi"));
		args.insert(QStringLiteral("pattern"), pattern.value(QStringLiteral("pattern")));
		args.insert(QStringLiteral("name"), name);
		addCommandAction(patterns, name.isEmpty() ? QObject::tr("Pattern %1").arg(pattern.value(QStringLiteral("pattern")).toInt() + 1) : name,
			QStringLiteral("session.set_slot"), [args, title] { return call(QStringLiteral("session.set_slot"), args, title, false); });
	}
	patterns->setEnabled(!listed.isEmpty());
}

//! WF-071: a step through the setlist. It opens another project, so unsaved work is asked about first, as every
//! other way of opening a project asks (MainWindow::mayChangeProject); session.setlist_go itself has nobody to ask.
void addSongStep(QMenu* menu, const QString& label, const QString& to)
{
	QAction* step = menu->addAction(label);
	step->setProperty("controlCommand", QStringLiteral("session.setlist_go"));
	step->setEnabled(ControlRegistry::instance()->hasCommand(QStringLiteral("session.setlist_go")));
	QObject::connect(step, &QAction::triggered, menu, [menu, to] {
		MainWindow* window = getGUI() != nullptr ? getGUI()->mainWindow() : nullptr;
		if (window != nullptr && !window->mayChangeProject(true)) { return; }
		// Parented past every QMenu, as addCommandAction does: a message must outlive the menu.
		QWidget* host = menu;
		while (qobject_cast<QMenu*>(host) != nullptr) { host = host->parentWidget(); }
		runCommand(host, call(QStringLiteral("session.setlist_go"), {{QStringLiteral("to"), to}}, QString(), false));
	});
}

} // namespace

//! WF-061: an entry opening the follow-action editor on @a chain, written back through @a command.
void chainEntry(QMenu* menu, const QString& command, const QJsonObject& where, const QJsonArray& chain, const QString& title)
{
	QAction* edit = menu->addAction(QObject::tr("Follow Action Chain..."));
	edit->setProperty("controlCommand", command);
	QObject::connect(edit, &QAction::triggered, menu, [menu, command, where, chain, title] {
		(new FollowActionEditor(command, where, chain, title, menu->parentWidget()))->show();
	});
}

void populateCell(QMenu* menu, int track, int scene, const QJsonObject& slot)
{
	const QJsonObject where = cellArgs(track, scene);
	const QString title = QObject::tr("Track %1, scene %2").arg(track + 1).arg(scene + 1);
	addCommandAction(menu, QObject::tr("Edit Cell..."), QStringLiteral("session.set_slot"), [where, slot, title] {
		CommandCall edit = call(QStringLiteral("session.set_slot"), where, title);
		for (const char* key : {"type", "name", "mode", "quantisation", "pattern", "source", "legato"})
		{
			const QString name = QLatin1String(key);
			if (slot.contains(name)) { edit.defaults.insert(name, slot.value(name)); }
		}
		return edit;
	});
	addPatternPicker(menu, where, title);
	addCommandAction(menu, QObject::tr("Audio File..."), QStringLiteral("session.set_slot"), [where, title] {
		QJsonObject audio = where;
		audio.insert(QStringLiteral("type"), QStringLiteral("audio"));
		return call(QStringLiteral("session.set_slot"), audio, title);
	});
	const auto set = [where, title] { return call(QStringLiteral("session.set_slot"), where, title); };
	addCommandChoices(menu, QObject::tr("Launch Mode"), QStringLiteral("session.set_slot"), QStringLiteral("mode"), set);
	addCommandChoices(menu, QObject::tr("Launch Quantisation"), QStringLiteral("session.set_slot"),
		QStringLiteral("quantisation"), set);
	menu->addSeparator();
	addCommandChoices(menu, QObject::tr("Follow Actions"), QStringLiteral("session.follow_set"), QStringLiteral("enabled"),
		[where, title] { return call(QStringLiteral("session.follow_set"), where, title); });
	chainEntry(menu, QStringLiteral("session.set_slot"), where, slot.value(QStringLiteral("follow_actions")).toArray(), title);  // WF-061
	addCommandAction(menu, QObject::tr("Jump To..."), QStringLiteral("session.jump_slot"), [where, title] {  // WF-070
		CommandCall jump = call(QStringLiteral("session.jump_slot"), where, title);
		jump.defaults.insert(QStringLiteral("position_ticks"), 0);
		return jump;
	});
	menu->addSeparator();
	QAction* clear = addCommandAction(menu, QObject::tr("Clear Cell"), QStringLiteral("session.clear_slot"),
		[where, title] { return call(QStringLiteral("session.clear_slot"), where, title, false); });
	clear->setEnabled(clear->isEnabled() && !slot.isEmpty());
}

void populateScene(QMenu* menu, int scene, const QJsonObject& sceneState)
{
	const QJsonObject where{{QStringLiteral("scene"), scene}};
	const QString title = QObject::tr("Scene %1").arg(scene + 1);
	addCommandAction(menu, QObject::tr("Edit Scene..."), QStringLiteral("session.set_scene"), [where, sceneState, title] {
		CommandCall edit = call(QStringLiteral("session.set_scene"), where, title);
		edit.defaults.insert(QStringLiteral("name"), sceneState.value(QStringLiteral("name")));
		edit.defaults.insert(QStringLiteral("color"), sceneState.value(QStringLiteral("color")));
		// A supplied tempo or metre switches its override ON: pre-fill one only while it is
		// already on, so editing the name alone never turns an override back on.
		if (sceneState.value(QStringLiteral("tempo_enabled")).toBool())
		{
			edit.defaults.insert(QStringLiteral("tempo"), sceneState.value(QStringLiteral("tempo")));
		}
		if (sceneState.value(QStringLiteral("timesig_enabled")).toBool())
		{
			edit.defaults.insert(QStringLiteral("timesig_numerator"), sceneState.value(QStringLiteral("numerator")));
			edit.defaults.insert(QStringLiteral("timesig_denominator"), sceneState.value(QStringLiteral("denominator")));
		}
		return edit;
	});
	const auto off = [where, title](const char* key) {
		QJsonObject args = where;
		args.insert(QLatin1String(key), false);
		return call(QStringLiteral("session.set_scene"), args, title, false);
	};
	addCommandAction(menu, QObject::tr("Tempo Override Off"), QStringLiteral("session.set_scene"),
		[off] { return off("tempo_override"); })->setEnabled(sceneState.value(QStringLiteral("tempo_enabled")).toBool());
	addCommandAction(menu, QObject::tr("Metre Override Off"), QStringLiteral("session.set_scene"),
		[off] { return off("timesig_override"); })->setEnabled(sceneState.value(QStringLiteral("timesig_enabled")).toBool());
	chainEntry(menu, QStringLiteral("session.set_scene"), where, sceneState.value(QStringLiteral("follow_actions")).toArray(), title);  // WF-061
	addCommandAction(menu, QObject::tr("Play Scenes in Order..."), QStringLiteral("session.set_playlist"), [scene, title] {  // WF-067
		CommandCall playlist = call(QStringLiteral("session.set_playlist"), {}, title);
		const int last = std::max(0, Engine::getSong()->sessionModel().sceneCount() - 1);
		playlist.defaults = {{QStringLiteral("scenes"), scene < last ? QJsonArray{scene, scene + 1} : QJsonArray{scene}}, {QStringLiteral("loop"), false}};
		return playlist;
	});
	addCommandAction(menu, QObject::tr("Launch a Folder's Cells Here..."), QStringLiteral("session.launch_group"),  // WF-064
		[where, title] { return call(QStringLiteral("session.launch_group"), where, title); });
	menu->addSeparator();
	addCommandAction(menu, QObject::tr("Launch Scene"), QStringLiteral("session.launch_scene"),
		[where, title] { return call(QStringLiteral("session.launch_scene"), where, title, false); });
}

void populateGrid(QMenu* menu, int tracks, int scenes)
{
	addCommandAction(menu, QObject::tr("Grid Size..."), QStringLiteral("session.set_grid"), [tracks, scenes] {
		CommandCall size = call(QStringLiteral("session.set_grid"), {}, QObject::tr("Grid Size"));
		size.defaults = {{QStringLiteral("tracks"), std::max(tracks, 1)}, {QStringLiteral("scenes"), std::max(scenes, 1)}};
		return size;
	});
	addCommandChoices(menu, QObject::tr("Launch Quantisation"), QStringLiteral("session.set_quantisation"),
		QStringLiteral("quantisation"), [] { return call(QStringLiteral("session.set_quantisation"), {}, QString()); });
	addCommandChoices(menu, QObject::tr("Follow Actions"), QStringLiteral("session.set_follow_actions"),
		QStringLiteral("enabled"), [] { return call(QStringLiteral("session.set_follow_actions"), {}, QString()); });
	menu->addSeparator();
	addCommandAction(menu, QObject::tr("Capture Playing Cells to a Scene"), QStringLiteral("session.capture_scene"),  // WF-068
		[] { return call(QStringLiteral("session.capture_scene"), {}, QString(), false); });
	// WF-071: the setlist - projects to play through.
	addCommandAction(menu, QObject::tr("Setlist..."), QStringLiteral("session.setlist"),
		[] { return call(QStringLiteral("session.setlist"), {}, QObject::tr("Setlist")); });
	addSongStep(menu, QObject::tr("Next Song"), QStringLiteral("next"));
	addSongStep(menu, QObject::tr("Previous Song"), QStringLiteral("previous"));
}

} // namespace lmms::gui::SessionGridMenus
