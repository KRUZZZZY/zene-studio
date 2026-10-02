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

#include "CommandForm.h"

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

} // namespace

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
	const auto set = [where, title] { return call(QStringLiteral("session.set_slot"), where, title); };
	addCommandChoices(menu, QObject::tr("Launch Mode"), QStringLiteral("session.set_slot"), QStringLiteral("mode"), set);
	addCommandChoices(menu, QObject::tr("Launch Quantisation"), QStringLiteral("session.set_slot"),
		QStringLiteral("quantisation"), set);
	menu->addSeparator();
	addCommandChoices(menu, QObject::tr("Follow Actions"), QStringLiteral("session.follow_set"), QStringLiteral("enabled"),
		[where, title] { return call(QStringLiteral("session.follow_set"), where, title); });
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
}

} // namespace lmms::gui::SessionGridMenus
