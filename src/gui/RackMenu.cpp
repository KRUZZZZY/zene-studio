/*
 * RackMenu.cpp - a mixer channel's Rack: parallel chains, the chain it routes to, and its macros
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

#include "RackMenu.h"

#include <QActionGroup>
#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

std::function<QString()> s_namePrompt;
std::function<std::optional<double>(double)> s_valuePrompt;

QString promptName()
{
	if (s_namePrompt) { return s_namePrompt(); }
	return QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("Add Macro"), QMenu::tr("Macro name:")).trimmed();
}

std::optional<double> promptValue(double current)
{
	if (s_valuePrompt) { return s_valuePrompt(current); }
	bool ok = false;
	const double value = QInputDialog::getDouble(QApplication::activeWindow(), QMenu::tr("Set Macro"),
		QMenu::tr("Value (0 to 1):"), current, 0.0, 1.0, 2, &ok);
	return ok ? std::optional<double>(value) : std::nullopt;
}

//! Runs @a command on the channel; a refusal is shown (or logged when nobody is there).
void run(const QString& channel, const QString& command, QJsonObject args = QJsonObject{})
{
	args.insert(QStringLiteral("channel"), channel);
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Rack"), result.errorMessage); }
}

QAction* commandItem(QMenu* menu, const QString& text, const char* command)
{
	QAction* action = menu->addAction(text);
	action->setData(QString::fromLatin1(command));
	return action;
}

void addRouting(QMenu* rack, const QString& channel, int chains, int selected)
{
	QMenu* route = rack->addMenu(QMenu::tr("Route to"));
	auto* group = new QActionGroup(route);
	for (int chain = -1; chain < chains; ++chain)
	{
		const QString text = chain < 0 ? QMenu::tr("Parallel (every chain, summed)")
			: chain == 0 ? QMenu::tr("Chain 0 (the channel's own)") : QMenu::tr("Chain %1").arg(chain);
		QAction* action = commandItem(route, text, "rack.set_selected");
		action->setCheckable(true);
		action->setChecked(chain == selected);
		group->addAction(action);
		QObject::connect(action, &QAction::triggered, route, [channel, chain] {
			run(channel, QStringLiteral("rack.set_selected"), {{QStringLiteral("chain"), chain}});
		});
	}
}

void addMacros(QMenu* rack, const QString& channel, const QJsonArray& macros)
{
	for (const QJsonValue& value : macros)
	{
		const QJsonObject macro = value.toObject();
		const QString id = macro.value(QStringLiteral("macro")).toString();
		const double current = macro.value(QStringLiteral("value")).toDouble();
		QAction* set = commandItem(rack, QMenu::tr("Macro %1: %2 (%3 targets)...")
			.arg(macro.value(QStringLiteral("name")).toString()).arg(current, 0, 'f', 2)
			.arg(macro.value(QStringLiteral("target_count")).toInt()), "rack.macro_set");
		QObject::connect(set, &QAction::triggered, rack, [channel, id, current] {
			if (const auto wanted = promptValue(current))
			{
				run(channel, QStringLiteral("rack.macro_set"), {{QStringLiteral("macro"), id}, {QStringLiteral("value"), *wanted}});
			}
		});
	}
	QAction* add = commandItem(rack, QMenu::tr("Add macro..."), "rack.macro_add");
	QObject::connect(add, &QAction::triggered, rack, [channel] {
		const QString name = promptName();
		if (!name.isEmpty()) { run(channel, QStringLiteral("rack.macro_add"), {{QStringLiteral("name"), name}}); }
	});
	if (macros.isEmpty()) { return; }
	QMenu* remove = rack->addMenu(QMenu::tr("Remove macro"));
	for (const QJsonValue& value : macros)
	{
		const QJsonObject macro = value.toObject();
		const QString id = macro.value(QStringLiteral("macro")).toString();
		QAction* action = commandItem(remove, macro.value(QStringLiteral("name")).toString(), "rack.macro_remove");
		QObject::connect(action, &QAction::triggered, remove, [channel, id] {
			run(channel, QStringLiteral("rack.macro_remove"), {{QStringLiteral("macro"), id}});
		});
	}
}

} // namespace

void setRackMacroNamePrompt(std::function<QString()> prompt) { s_namePrompt = std::move(prompt); }
void setRackMacroValuePrompt(std::function<std::optional<double>(double)> prompt) { s_valuePrompt = std::move(prompt); }

QString rackStatusText(const QJsonObject& state)
{
	const int chains = state.value(QStringLiteral("chain_count")).toInt();
	const int selected = state.value(QStringLiteral("selected")).toInt();
	const QString routing = selected < 0 ? QMenu::tr("parallel") : QMenu::tr("chain %1").arg(selected);
	// One chain is the channel's own whatever the selector says: parallel over one chain is that chain.
	return chains <= 1 ? QMenu::tr("1 chain (the channel's own)")
		: QMenu::tr("%1 chains, routed to %2").arg(chains).arg(routing);
}

void addRackMenu(QMenu* menu, const MixerChannel* channel)
{
	if (channel == nullptr) { return; }
	const QString id = control::channelIdOf(channel);
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("rack.get_state"),
		{{QStringLiteral("channel"), id}});
	QMenu* rack = menu->addMenu(QMenu::tr("Rack"));
	QAction* line = rack->addAction(state.ok ? rackStatusText(state.result) : state.errorMessage);
	line->setEnabled(false);
	if (!state.ok) { return; }
	const int chains = state.result.value(QStringLiteral("chain_count")).toInt();
	rack->addSeparator();
	addRouting(rack, id, chains, state.result.value(QStringLiteral("selected")).toInt());
	QAction* addChain = commandItem(rack, QMenu::tr("Add parallel chain"), "rack.add_chain");
	QObject::connect(addChain, &QAction::triggered, rack, [id] { run(id, QStringLiteral("rack.add_chain")); });
	if (chains > 1)
	{
		QMenu* remove = rack->addMenu(QMenu::tr("Remove chain"));
		for (int chain = 1; chain < chains; ++chain)
		{
			QAction* action = commandItem(remove, QMenu::tr("Chain %1").arg(chain), "rack.remove_chain");
			QObject::connect(action, &QAction::triggered, remove, [id, chain] {
				run(id, QStringLiteral("rack.remove_chain"), {{QStringLiteral("chain"), chain}});
			});
		}
	}
	rack->addSeparator();
	addMacros(rack, id, state.result.value(QStringLiteral("macros")).toArray());
}

} // namespace lmms::gui
