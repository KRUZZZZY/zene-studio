/*
 * SessionSyncAction.cpp - the transport's session-sync toggle, with its peers and tempo
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

#include "SessionSyncAction.h"

#include <QAction>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QTimer>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

QJsonObject linkState()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("link.get_state"), QJsonObject{}).result;
}

void show(QAction* action, const QJsonObject& state)
{
	const QSignalBlocker block(action);
	action->setChecked(state.value(QStringLiteral("enabled")).toBool());
	action->setText(sessionSyncText(state));
}

} // namespace

QString sessionSyncText(const QJsonObject& state)
{
	if (!state.value(QStringLiteral("enabled")).toBool()) { return QAction::tr("Sync"); }
	return QAction::tr("Sync: %n peer(s), %1 BPM", nullptr, state.value(QStringLiteral("peer_count")).toInt())
		.arg(state.value(QStringLiteral("session_tempo")).toDouble(), 0, 'f', 1);
}

QAction* makeSessionSyncAction(QObject* parent)
{
	auto* action = new QAction(QAction::tr("Sync"), parent);
	action->setCheckable(true);
	action->setProperty("controlCommand", QStringLiteral("link.set_enabled"));
	action->setToolTip(QAction::tr("Session sync: share a tempo and a beat phase with other Zene Studio "
		"instances on this machine or network (this project's own sync, not Ableton Link)"));
	QObject::connect(action, &QAction::toggled, action, [action](bool on) {
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("link.set_enabled"),
			{{QStringLiteral("enabled"), on}});
		if (!result.ok) { action->setToolTip(result.errorMessage); }
		show(action, linkState());
	});
	auto* poll = new QTimer(action);
	QObject::connect(poll, &QTimer::timeout, action, [action] {
		if (action->isChecked()) { show(action, linkState()); }
	});
	poll->start(1000);
	return action;
}

} // namespace lmms::gui
