/*
 * MidiReconnectAction.cpp - Edit > MIDI Controller Reconnect: arm it and see what it holds
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

#include "MidiReconnectAction.h"

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QSignalBlocker>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

void showStatus(QAction* action)
{
	const ControlResult status = ControlRegistry::instance()->invoke(QStringLiteral("midi.reconnect_status"), QJsonObject{});
	const QSignalBlocker block(action);
	action->setEnabled(status.ok);
	action->setChecked(status.ok && status.result.value(QStringLiteral("enabled")).toBool());
	action->setText(midiReconnectText(status.ok ? status.result : QJsonObject{}));
}

} // namespace

QString midiReconnectText(const QJsonObject& status)
{
	const int bound = status.value(QStringLiteral("assignment_count")).toInt();
	if (bound == 0) { return QMenu::tr("MIDI Controller Reconnect"); }
	return QMenu::tr("MIDI Controller Reconnect (%1 bound, %2 live, %3 lost)").arg(bound)
		.arg(status.value(QStringLiteral("live_count")).toInt()).arg(status.value(QStringLiteral("lost_count")).toInt());
}

QAction* addMidiReconnectAction(QMenu* menu)
{
	QAction* action = menu->addAction(QMenu::tr("MIDI Controller Reconnect"));
	action->setCheckable(true);
	action->setData(QStringLiteral("midi.reconnect_arm"));
	action->setToolTip(QMenu::tr("Re-attach a MIDI controller that was unplugged and plugged back in to the "
		"tracks it was bound to"));
	QObject::connect(action, &QAction::toggled, action, [action](bool on) {
		ControlRegistry::instance()->invoke(QStringLiteral("midi.reconnect_arm"), {{QStringLiteral("enabled"), on}});
		showStatus(action);
	});
	QObject::connect(menu, &QMenu::aboutToShow, action, [action] { showStatus(action); });
	showStatus(action);
	return action;
}

} // namespace lmms::gui
