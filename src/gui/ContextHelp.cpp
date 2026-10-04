/*
 * ContextHelp.cpp - hover and What's This help from the commands every control runs
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
#include "ContextHelp.h"

#include <QAction>
#include <QApplication>
#include <QHelpEvent>
#include <QMenu>
#include <QToolTip>
#include <QWhatsThis>
#include <QWidget>

#include "CommandMetadata.h"
#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

//! The command id a widget (or one of its two nearest parents) runs; a menu's actions: helpAt().
QString commandOf(const QWidget* widget)
{
	for (int level = 0; widget != nullptr && level < 3; ++level, widget = widget->parentWidget())
	{
		const QString id = widget->property("controlCommand").toString();
		if (!id.isEmpty()) { return id; }
	}
	return {};
}

QString describe(const QString& id, bool full)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	if (command == nullptr || command->description.isEmpty()) { return {}; }
	if (full)
	{
		return QStringLiteral("<b>%1</b> <small>(%2)</small><br>%3").arg(commandName(id).toHtmlEscaped(), id,
			command->description.toHtmlEscaped());
	}
	const int stop = command->description.indexOf(QLatin1String(". "));
	return stop > 0 ? command->description.left(stop + 1) : command->description;
}

} // namespace

void ContextHelp::install()
{
	static ContextHelp* filter = nullptr;
	if (filter != nullptr || qApp == nullptr) { return; }
	filter = new ContextHelp;
	filter->setParent(qApp);
	qApp->installEventFilter(filter);
}

QString ContextHelp::helpFor(const QWidget* widget, bool full)
{
	const QString id = commandOf(widget);
	return id.isEmpty() ? QString() : describe(id, full);
}

QString ContextHelp::commandFor(const QWidget* widget)
{
	return commandOf(widget);
}

namespace
{

//! What help event @a event over @a widget shows: over a menu, the command of the action under the pointer
//! (addCommandAction puts it on the QAction, which receives no events of its own); else helpFor().
QString helpAt(const QWidget* widget, const QEvent* event, bool full)
{
	// Only ToolTip and WhatsThis are relied on to carry a position (a QHelpEvent); the query is answered by the menu.
	const auto* help = event->type() == QEvent::QueryWhatsThis ? nullptr : static_cast<const QHelpEvent*>(event);
	if (const auto* menu = qobject_cast<const QMenu*>(widget); menu != nullptr && help != nullptr)
	{
		const QAction* action = menu->actionAt(help->pos());
		const QString id = action != nullptr ? action->property("controlCommand").toString() : QString();
		if (!id.isEmpty()) { return describe(id, full); }
	}
	return ContextHelp::helpFor(widget, full);
}

} // namespace

bool ContextHelp::eventFilter(QObject* watched, QEvent* event)
{
	const auto type = event->type();
	if (type != QEvent::ToolTip && type != QEvent::WhatsThis && type != QEvent::QueryWhatsThis) { return false; }
	auto* widget = qobject_cast<QWidget*>(watched);
	if (widget == nullptr) { return false; }
	if (type == QEvent::ToolTip && !widget->toolTip().isEmpty()) { return false; }  // its own tooltip wins
	const QString text = helpAt(widget, event, type != QEvent::ToolTip);
	if (text.isEmpty()) { return false; }
	if (type == QEvent::QueryWhatsThis) { event->accept(); return true; }
	const auto* help = static_cast<QHelpEvent*>(event);
	if (type == QEvent::ToolTip) { QToolTip::showText(help->globalPos(), text, widget); }
	else { QWhatsThis::showText(help->globalPos(), text, widget); }
	return true;
}

} // namespace lmms::gui
