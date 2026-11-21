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
#include <QPixmap>

#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "TextFloat.h"

namespace lmms::gui
{

namespace
{

ControllerTemplatePrompt& prompt()
{
	static ControllerTemplatePrompt s_prompt;
	return s_prompt;
}

ControllerTemplateNotifier& notifier()
{
	static ControllerTemplateNotifier s_notifier;
	return s_notifier;
}

std::optional<QString> askForName()
{
	if (prompt()) { return prompt()(); }
	bool ok = false;
	const QString name = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("Save mapping template"),
		QMenu::tr("Name:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !name.isEmpty() ? std::optional<QString>(name) : std::nullopt;
}

//! The float the user reads when a template command refused, or only half worked.
void showFloat(const QString& title, const QString& text)
{
	// No interface to float a message over (a tool embedding the registry).
	if (getGUI() == nullptr) { return; }
	TextFloat::displayMessage(title, text, QPixmap(), 5000);
}

//! The one way a template command's answer reaches the user.
void notify(const QString& title, const QString& text)
{
	if (notifier()) { notifier()(title, text); return; }
	showFloat(title, text);
}

//! What the message about a template command is called.
QString titleOf(const QString& command)
{
	if (command == QLatin1String("controller.template_list")) { return QMenu::tr("Mapping templates"); }
	if (command == QLatin1String("controller.template_save")) { return QMenu::tr("Save mapping template"); }
	if (command == QLatin1String("controller.template_apply")) { return QMenu::tr("Apply mapping template"); }
	return QMenu::tr("Delete mapping template");
}

//! What the user is told when a template applied only partly: the bindings whose
//! target is not in this project were skipped, and the report names them.
void reportPartialApply(const QString& name, const ControlResult& result)
{
	const int skipped = result.result.value(QStringLiteral("skipped")).toInt();
	if (skipped <= 0) { return; }
	const int bound = result.result.value(QStringLiteral("bound")).toInt();
	QStringList missing;
	for (const QJsonValue& value : result.result.value(QStringLiteral("missing")).toArray())
	{
		missing << value.toString();
	}
	if (missing.size() > 5) { missing = missing.mid(0, 5) << QMenu::tr("…"); }
	notify(titleOf(QStringLiteral("controller.template_apply")), QMenu::tr(
		"Applied '%1': %2 of %3 mappings were bound; %4 could not be resolved in this "
		"project and were skipped (%5).")
		.arg(name, QString::number(bound), QString::number(bound + skipped),
			QString::number(skipped), missing.join(QStringLiteral(", "))));
}

//! What the user is told when the template was not there to delete: the command
//! answers 'removed: false' rather than failing, so the menu has to say it.
void reportNotRemoved(const QString& name, const ControlResult& result)
{
	if (result.result.value(QStringLiteral("removed")).toBool()) { return; }
	notify(titleOf(QStringLiteral("controller.template_delete")),
		QMenu::tr("There was no template named '%1' to delete.").arg(name));
}

//! A template command's answer reaches the user: a refusal, a partial apply, or a
//! delete of something already gone. A menu closes either way, so without this the
//! failure is silent.
void report(const QString& command, const QString& name, const ControlResult& result)
{
	if (!result.ok)
	{
		notify(titleOf(command), result.errorMessage);
		return;
	}
	if (command == QLatin1String("controller.template_apply")) { reportPartialApply(name, result); return; }
	if (command == QLatin1String("controller.template_delete")) { reportNotRemoved(name, result); }
}

void run(const char* command, const QString& name)
{
	const ControlResult result = ControlRegistry::instance()->invoke(QString::fromLatin1(command),
		{{QStringLiteral("name"), name}});
	report(QString::fromLatin1(command), name, result);
}

} // namespace

void setControllerTemplatePrompt(ControllerTemplatePrompt replacement)
{
	prompt() = std::move(replacement);
}

void setControllerTemplateNotifier(ControllerTemplateNotifier replacement)
{
	notifier() = std::move(replacement);
}

void populateControllerTemplateMenu(QMenu* menu)
{
	menu->clear();
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("controller.template_list"),
		QJsonObject{});
	if (!listed.ok) { notify(titleOf(QStringLiteral("controller.template_list")), listed.errorMessage); }
	const QJsonArray templates = listed.result.value(QStringLiteral("templates")).toArray();
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
