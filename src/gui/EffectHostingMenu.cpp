/*
 * EffectHostingMenu.cpp - an effect's Hosting submenu: in-process or a separate process, and restart
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

#include "EffectHostingMenu.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "PluginNativeEditor.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

//! This effect's entry in oop.get_state, or an empty object.
QJsonObject deviceEntry(const QString& target, const QString& device)
{
	const QJsonArray chains = ControlRegistry::instance()->invoke(QStringLiteral("oop.get_state"), QJsonObject{})
		.result.value(QStringLiteral("chains")).toArray();
	for (const QJsonValue& chain : chains)
	{
		for (const QJsonValue& entry : chain.toObject().value(QStringLiteral("devices")).toArray())
		{
			const QJsonObject object = entry.toObject();
			if (object.value(QStringLiteral("target")).toString() == target
				&& object.value(QStringLiteral("device")).toString() == device) { return object; }
		}
	}
	return QJsonObject{};
}

void run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Hosting"), result.errorMessage); }
}

} // namespace

QString hostingStatusText(const QJsonObject& device)
{
	const QJsonObject hosting = device.value(QStringLiteral("hosting")).toObject();
	const QString state = hosting.value(QStringLiteral("state")).toString();
	return state.isEmpty() ? QMenu::tr("Hosting: unknown") : QMenu::tr("Hosting: %1").arg(state);
}

void addEffectHostingMenu(QMenu* menu, Effect* effect)
{
	if (effect == nullptr) { return; }
	const QString target = targetIdOfChain(effect->effectChain());
	const QString device = control::effectIdOf(effect);
	if (target.isEmpty()) { return; }
	const QJsonObject entry = deviceEntry(target, device);
	const QJsonObject hosting = entry.value(QStringLiteral("hosting")).toObject();
	QMenu* sub = menu->addMenu(QMenu::tr("Hosting"));
	QAction* line = sub->addAction(hostingStatusText(entry));
	line->setEnabled(false);
	line->setData(QStringLiteral("oop.get_state"));

	const bool separate = hosting.value(QStringLiteral("state")).toString() == QStringLiteral("separate-process");
	QAction* mode = sub->addAction(QMenu::tr("Run in a separate process"));
	mode->setCheckable(true);
	mode->setChecked(separate);
	mode->setData(QStringLiteral("oop.set_mode"));
	const bool canChoose = hosting.value(QStringLiteral("can_choose_mode")).toBool();
	mode->setEnabled(canChoose);
	mode->setToolTip(canChoose ? QMenu::tr("Host this effect in its own client process, so its crash cannot take the DAW down")
		: QMenu::tr("%1 cannot run in a separate process in this build").arg(entry.value(QStringLiteral("label")).toString()));
	QObject::connect(mode, &QAction::triggered, menu, [target, device](bool on) {
		run(QStringLiteral("oop.set_mode"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), device},
			{QStringLiteral("mode"), on ? QStringLiteral("separate-process") : QStringLiteral("in-process")}});
	});
	QAction* restart = sub->addAction(QMenu::tr("Restart its process"));
	restart->setData(QStringLiteral("oop.restart"));
	restart->setEnabled(separate && hosting.value(QStringLiteral("can_restart")).toBool());
	QObject::connect(restart, &QAction::triggered, menu, [target, device] {
		run(QStringLiteral("oop.restart"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), device}});
	});
}

} // namespace lmms::gui
