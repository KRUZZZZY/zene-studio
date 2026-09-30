/*
 * AutomationModeMenu.cpp - the "Automation mode" submenu of every control's context menu
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

#include "AutomationModeMenu.h"

#include <QActionGroup>
#include <QJsonObject>
#include <QMenu>

#include "AutomatableModel.h"
#include "ControlAutomationSupport.h"
#include "ControlRegistry.h"

namespace lmms::gui
{

void addAutomationModeMenu(QMenu* menu, AutomatableModel* model)
{
	control::ParameterAddress address;
	if (!control::addressableParameterForModel(model, &address)) { return; }
	const QString target = address.target;
	const QString parameter = address.parameter.id();
	const QString current = control::automationModeName(model->automationMode());

	QMenu* modes = menu->addMenu(QObject::tr("Automation mode"));
	modes->menuAction()->setData(QStringLiteral("automation.mode_set"));
	auto* group = new QActionGroup(modes);
	const QList<QPair<QString, QString>> items{
		{QStringLiteral("off"), QObject::tr("Off - ignore the automation")},
		{QStringLiteral("read"), QObject::tr("Read - play the automation")},
		{QStringLiteral("touch"), QObject::tr("Touch - write while held")},
		{QStringLiteral("latch"), QObject::tr("Latch - write from the first touch")},
		{QStringLiteral("write"), QObject::tr("Write - overwrite while playing")},
	};
	for (const auto& [mode, label] : items)
	{
		QAction* action = modes->addAction(label);
		action->setCheckable(true);
		action->setChecked(mode == current);
		action->setData(QStringLiteral("automation.mode_set"));
		group->addAction(action);
		QObject::connect(action, &QAction::triggered, modes, [target, parameter, mode = mode] {
			ControlRegistry::instance()->invoke(QStringLiteral("automation.mode_set"),
				{{QStringLiteral("track"), target}, {QStringLiteral("parameter"), parameter},
					{QStringLiteral("mode"), mode}});
		});
	}
}

} // namespace lmms::gui
