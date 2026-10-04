/*
 * CommandRun.cpp - the ways into a registered command: run at once, through its form, from a menu
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

// Split out of CommandForm.cpp (0.5.0 themes 4 and 5) when the form grew its labels, pickers and
// answer panel: the file-length ratchet reads a file as a unit. The functions are unchanged but
// for announce(), which now reads a result as "Label: value" lines and names the command.

#include "CommandForm.h"

#include <QAction>
#include <QJsonArray>
#include <QMenu>

#include "CommandMetadata.h"
#include "ControlVocabulary.h"
#include "GuiApplication.h"
#include "ShellSelection.h"
#include "TextFloat.h"

namespace lmms::gui
{

namespace
{

//! The widget a menu belongs to: a form or message must outlive the menu (often a stack
//! object closed the moment an action runs), so it is parented past every QMenu.
QWidget* hostOf(QWidget* widget)
{
	while (qobject_cast<QMenu*>(widget) != nullptr) { widget = widget->parentWidget(); }
	return widget;
}

} // namespace

QString schemaType(const QJsonObject& spec)
{
	const QJsonValue type = spec.value(QStringLiteral("type"));
	if (!type.isArray()) { return type.toString(); }
	for (const QJsonValue& name : type.toArray())
	{
		if (name.toString() != QStringLiteral("null")) { return name.toString(); }
	}
	return QString();
}

QJsonObject selectionContext(const QString& id)
{
	const QJsonObject properties = commandProperties(id);
	const ShellSelection* selection = ShellSelection::instance();
	const QString track = control::trackIdOf(selection->track());
	const QString clip = selection->clip() != nullptr ? control::clipIdOf(selection->clip()) : QString();
	const QString channel = selection->channel();
	QJsonObject out;
	auto offer = [&](const char* key, const QString& value) {
		const QJsonObject spec = properties.value(QLatin1String(key)).toObject();
		if (!value.isEmpty() && schemaType(spec) == QStringLiteral("string")) { out.insert(QLatin1String(key), value); }
	};
	offer("track", track);
	offer("clip", clip);
	offer("channel", channel);
	offer("target", channel.isEmpty() ? track : channel);
	return out;
}

void announce(QWidget* parent, const CommandCall& call, const ControlResult& result)
{
	// No interface to float a message over (a tool embedding the registry): nothing to show.
	if (ControlRegistry::instance()->isHeadless() || getGUI() == nullptr || (result.ok && !call.showResult)) { return; }
	constexpr int MaxChars = 400;
	constexpr int MaxLines = 8;
	QString text = result.ok ? resultSummary(result.result, MaxLines) : result.errorMessage;
	if (text.size() > MaxChars) { text = text.left(MaxChars - 1) + QStringLiteral("…"); }
	TextFloat::displayMessage(call.title.isEmpty() ? commandName(call.id) : call.title, text, QPixmap(),
		result.ok ? 8000 : 4000, parent);
}

ControlResult runCommand(QWidget* parent, const CommandCall& call, CommandForm** form)
{
	if (form != nullptr) { *form = nullptr; }
	bool nothingToAsk = true;
	const QJsonObject properties = commandProperties(call.id);
	for (auto it = properties.begin(); it != properties.end() && nothingToAsk; ++it)
	{
		nothingToAsk = call.fixed.contains(it.key());
	}
	if (!call.ask || nothingToAsk)
	{
		const ControlResult result = ControlRegistry::instance()->invoke(call.id, call.fixed);
		announce(parent, call, result);
		return result;
	}
	auto* shown = new CommandForm(call, parent);
	shown->setAttribute(Qt::WA_DeleteOnClose);
	shown->setModal(false);
	shown->show();
	if (form != nullptr) { *form = shown; }
	return ControlResult::failure(ControlErrorKind::Busy, QObject::tr("waiting for the form"));
}

QAction* addCommandAction(QMenu* menu, const QString& label, const QString& id, std::function<CommandCall()> make)
{
	QAction* action = menu->addAction(label);
	action->setProperty("controlCommand", id);
	action->setEnabled(ControlRegistry::instance()->hasCommand(id));
	QObject::connect(action, &QAction::triggered, menu, [menu, make = std::move(make)] {
		runCommand(hostOf(menu), make());
	});
	return action;
}

QMenu* addCommandChoices(QMenu* menu, const QString& label, const QString& id, const QString& key,
	std::function<CommandCall()> make)
{
	QMenu* sub = menu->addMenu(label);
	const QJsonObject spec = commandProperties(id).value(key).toObject();
	const bool flag = schemaType(spec) == QStringLiteral("boolean");
	const QJsonArray values = flag ? QJsonArray{true, false} : spec.value(QStringLiteral("enum")).toArray();
	sub->setEnabled(!values.isEmpty());
	for (const QJsonValue& value : values)
	{
		const QString text = flag ? (value.toBool() ? QObject::tr("On") : QObject::tr("Off")) : valueText(value);
		QAction* action = sub->addAction(text);
		action->setProperty("controlCommand", id);
		QObject::connect(action, &QAction::triggered, sub, [sub, key, value, make] {
			CommandCall call = make();
			call.fixed.insert(key, value);
			call.ask = false;
			runCommand(hostOf(sub), call);
		});
	}
	return sub;
}

} // namespace lmms::gui
