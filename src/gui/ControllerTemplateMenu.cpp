/*
 * ControllerTemplateMenu.cpp - Edit > Controller Templates: saved MIDI mapping sets
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

#include "ControllerTemplateMenu.h"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

ControllerTemplatePrompt& prompt()
{
	static ControllerTemplatePrompt s_prompt;
	return s_prompt;
}

std::optional<QString> askForName()
{
	if (prompt()) { return prompt()(); }
	bool ok = false;
	const QString name = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("Save mapping template"),
		QMenu::tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !name.isEmpty() ? std::optional<QString>(name) : std::nullopt;
}

void run(const char* command, const QString& name)
{
	ControlRegistry::instance()->invoke(QString::fromLatin1(command), {{QStringLiteral("name"), name}});
}

} // namespace

void setControllerTemplatePrompt(ControllerTemplatePrompt replacement)
{
	prompt() = std::move(replacement);
}

void populateControllerTemplateMenu(QMenu* menu)
{
	menu->clear();
	const QJsonArray templates = ControlRegistry::instance()->invoke(QStringLiteral("controller.template_list"),
		QJsonObject{}).result.value(QStringLiteral("templates")).toArray();
	QStringList names;
	for (const QJsonValue& value : templates)
	{
		const QJsonObject entry = value.toObject();
		const QString name = entry.value(QStringLiteral("name")).toString();
		names << name;
		QAction* apply = menu->addAction(QMenu::tr("%1 (%2/%3 mappings)").arg(name)
			.arg(entry.value(QStringLiteral("resolvable")).toInt()).arg(entry.value(QStringLiteral("bindings")).toInt()));
		apply->setData(QStringLiteral("controller.template_apply"));
		QObject::connect(apply, &QAction::triggered, menu, [name] { run("controller.template_apply", name); });
	}
	if (!names.isEmpty()) { menu->addSeparator(); }
	QAction* save = menu->addAction(QMenu::tr("Save current mappings as..."));
	save->setData(QStringLiteral("controller.template_save"));
	QObject::connect(save, &QAction::triggered, menu, [] {
		if (const std::optional<QString> name = askForName()) { run("controller.template_save", *name); }
	});
	QMenu* remove = menu->addMenu(QMenu::tr("Delete"));
	remove->setEnabled(!names.isEmpty());
	for (const QString& name : names)
	{
		QAction* drop = remove->addAction(name);
		drop->setData(QStringLiteral("controller.template_delete"));
		QObject::connect(drop, &QAction::triggered, menu, [name] { run("controller.template_delete", name); });
	}
}

} // namespace lmms::gui
