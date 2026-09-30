/*
 * MpeInputAction.cpp - Edit > MPE Input: the MIDI Polyphonic Expression input switch
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

#include "MpeInputAction.h"

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QSignalBlocker>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

void showState(QAction* action)
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("device.mpe_get_state"), QJsonObject{});
	const QSignalBlocker block(action);
	action->setEnabled(state.ok);
	action->setChecked(state.ok && state.result.value(QStringLiteral("enabled")).toBool());
}

} // namespace

QAction* addMpeInputAction(QMenu* menu)
{
	QAction* action = menu->addAction(QMenu::tr("MPE Input"));
	action->setCheckable(true);
	action->setData(QStringLiteral("device.mpe_set"));
	action->setToolTip(QMenu::tr("MIDI Polyphonic Expression: a controller's per-note bend, pressure and "
		"timbre play as each note's own expression"));
	QObject::connect(action, &QAction::toggled, action, [action](bool on) {
		ControlRegistry::instance()->invoke(QStringLiteral("device.mpe_set"), {{QStringLiteral("enabled"), on}});
		showState(action);
	});
	QObject::connect(menu, &QMenu::aboutToShow, action, [action] { showState(action); });
	showState(action);
	return action;
}

} // namespace lmms::gui
