/*
 * MidiControllerMenu.cpp - soft takeover and LED feedback on a MIDI-driven control's menu
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

#include "MidiControllerMenu.h"

#include <QAction>
#include <QJsonObject>
#include <QMenu>

#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControllerConnection.h"
#include "MidiController.h"

namespace lmms::gui
{

void addMidiControllerToggles(QMenu* menu, AutomatableModel* model)
{
	ControllerConnection* connection = model != nullptr ? model->controllerConnection() : nullptr;
	auto* midi = connection != nullptr ? dynamic_cast<MidiController*>(connection->getController()) : nullptr;
	if (midi == nullptr) { return; }
	const QString control = model->fullDisplayName();
	const auto toggle = [menu, control](const QString& text, bool on, const QString& command) {
		QAction* action = menu->addAction(text);
		action->setCheckable(true);
		action->setChecked(on);
		action->setData(command);
		QObject::connect(action, &QAction::toggled, menu, [control, command](bool enabled) {
			ControlRegistry::instance()->invoke(command,
				{{QStringLiteral("control"), control}, {QStringLiteral("enabled"), enabled}});
		});
	};
	menu->addSeparator();
	toggle(QMenu::tr("Soft takeover"), midi->softTakeoverEnabled(), QStringLiteral("controller.soft_takeover"));
	toggle(QMenu::tr("LED feedback"), midi->feedbackEnabled(), QStringLiteral("controller.feedback"));
}

} // namespace lmms::gui
