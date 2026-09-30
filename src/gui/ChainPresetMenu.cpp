/*
 * ChainPresetMenu.cpp - a track menu's Effect chain presets: save the chain, apply or delete a preset
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

#include "ChainPresetMenu.h"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Track.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

std::function<QString()> s_prompt;
std::function<bool(const QString&)> s_ask;

QString promptName()
{
	if (s_prompt) { return s_prompt(); }
	return QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("Save Chain as Preset"),
		QMenu::tr("Preset name:")).trimmed();
}

bool ask(const QString& question)
{
	if (s_ask) { return s_ask(question); }
	return QMessageBox::question(QApplication::activeWindow(), QMenu::tr("Effect Chain Presets"), question,
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

//! Runs @a command; a refusal is shown (or logged when nobody is there).
ControlResult run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return result; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Effect Chain Presets"), result.errorMessage); }
	return result;
}

QStringList presetNames()
{
	QStringList names;
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("chain.list"), QJsonObject{});
	for (const QJsonValue& preset : listed.result.value(QStringLiteral("presets")).toArray())
	{
		names << preset.toObject().value(QStringLiteral("name")).toString();
	}
	return names;
}

void saveChain(const QString& target)
{
	const QString name = promptName();
	if (name.isEmpty()) { return; }
	const bool exists = presetNames().contains(name);
	if (exists && !ask(QMenu::tr("A preset named \"%1\" exists. Replace it?").arg(name))) { return; }
	run(QStringLiteral("chain.save"), {{QStringLiteral("target"), target}, {QStringLiteral("name"), name},
		{QStringLiteral("overwrite"), exists}});
}

void rebuild(QMenu* menu, const QString& target)
{
	for (QAction* action : menu->actions())
	{
		menu->removeAction(action);
		if (action->menu() != nullptr && action->menu()->parent() == menu) { action->menu()->deleteLater(); }
		else if (action->parent() == menu) { action->deleteLater(); }
	}
	QAction* save = menu->addAction(QMenu::tr("Save chain as preset..."));
	save->setData(QStringLiteral("chain.save"));
	QObject::connect(save, &QAction::triggered, menu, [target] { saveChain(target); });

	const QStringList names = presetNames();
	if (names.isEmpty()) { return; }
	menu->addSeparator();
	for (const QString& name : names)
	{
		QAction* apply = menu->addAction(QMenu::tr("Apply %1").arg(name));
		apply->setData(QStringLiteral("chain.apply"));
		QObject::connect(apply, &QAction::triggered, menu, [target, name] {
			run(QStringLiteral("chain.apply"), {{QStringLiteral("name"), name}, {QStringLiteral("target"), target}});
		});
	}
	QMenu* remove = new QMenu(QMenu::tr("Delete"), menu);
	menu->addMenu(remove);
	for (const QString& name : names)
	{
		QAction* item = remove->addAction(name);
		item->setData(QStringLiteral("chain.remove"));
		QObject::connect(item, &QAction::triggered, menu, [name] {
			if (!ask(QMenu::tr("Delete the preset \"%1\"?").arg(name))) { return; }
			run(QStringLiteral("chain.remove"), {{QStringLiteral("name"), name}});
		});
	}
}

} // namespace

void setChainPresetNamePrompt(std::function<QString()> prompt) { s_prompt = std::move(prompt); }
void setChainPresetQuestion(std::function<bool(const QString&)> askFn) { s_ask = std::move(askFn); }

void addChainPresetMenu(QMenu* menu, Track* track)
{
	if (track == nullptr || (track->type() != Track::Type::Instrument && track->type() != Track::Type::Sample)) { return; }
	const QString target = control::trackIdOf(track);
	QMenu* presets = menu->addMenu(QMenu::tr("Effect chain presets"));
	QObject::connect(presets, &QMenu::aboutToShow, presets, [presets, target] { rebuild(presets, target); });
	rebuild(presets, target);
}

} // namespace lmms::gui
