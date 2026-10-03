/*
 * ControlCommandsSurface.cpp - the control.surface_report command (SPEC A15).
 *
 * The anti-drift gate needs to see the REAL user-facing surface, not a list
 * someone maintains by hand. This command walks the live MainWindow - every
 * menu in the menu bar (recursively, including submenus) and the main toolbar -
 * and reports one entry per user-visible action, together with the command id
 * that action declares (if any).
 *
 * The declaration contract is documented in tests/agent-surface-gate.py:
 * a QAction / QToolButton declares the command it implements by setting its
 * objectName() or the dynamic property "controlCommand" to a "group.verb"
 * command id, and a QAction may also carry it in data(). Anything else is an
 * unregistered action and the gate fails on it unless it is grandfathered in
 * tests/agent-surface-baseline.txt.
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
 */

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms
{

namespace
{

//! Action text as a human reads it: mnemonic ampersands removed, the
//! "\tCtrl+1" shortcut suffix cut and the whitespace normalised, so a baseline
//! entry reads like the menu item it grandfathers.
QString displayText(const QString& raw)
{
	QString out = raw;
	const int tab = out.indexOf(QLatin1Char('\t'));
	if (tab >= 0)
	{
		out.truncate(tab);
	}
	out.remove(QLatin1Char('&'));
	return out.simplified();
}

//! A container name for a QMenu. TemplatesMenu and RecentProjectsMenu set no
//! title (they are reached through the File menu and the toolbar button), so
//! fall back to the object name and then to the class name - never to an
//! ordinal, which would move every time the tree changes.
QString containerLabel(const QMenu* menu)
{
	const QString title = displayText(menu->title());
	if (!title.isEmpty())
	{
		return title;
	}
	if (!menu->objectName().isEmpty())
	{
		return menu->objectName();
	}
	QString className = QString::fromLatin1(menu->metaObject()->className());
	const int separator = className.lastIndexOf(QStringLiteral("::"));
	return separator >= 0 ? className.mid(separator + 2) : className;
}

//! True for a string that could be a "group.verb" command id. Guards the
//! declaration channels against values that are somebody else's payload - a
//! template path in QAction::data(), a widget object name - which would
//! otherwise be reported as a declaration that does not resolve.
bool looksLikeCommandId(const QString& text)
{
	return text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('/'))
		&& !text.contains(QLatin1Char('\\')) && !text.contains(QLatin1Char(' '));
}

//! The command id \p candidates declare, and whether it resolves. Only the
//! first candidate that looks like a command id counts: a lower-priority
//! channel must not be able to "register" an action whose own declared id is
//! wrong.
QString declaredId(const QStringList& candidates, bool* unknown)
{
	*unknown = false;
	for (const QString& candidate : candidates)
	{
		if (!looksLikeCommandId(candidate))
		{
			continue;
		}
		if (!ControlRegistry::instance()->hasCommand(candidate))
		{
			*unknown = true;
			return candidate;
		}
		return candidate;
	}
	return QString();
}

QJsonObject makeItem(const QString& surface, const QString& containerPath, const QString& containerClass,
	const QString& text, const QString& objectName, const QStringList& declaredCandidates,
	const QString& dataCandidate, bool dataLive)
{
	bool unknown = false;
	QString id = declaredId(declaredCandidates, &unknown);
	if (id.isEmpty() && dataLive && !dataCandidate.isEmpty())
	{
		// data() is the weakest channel: it only counts when it already resolved
		// to a live command, so a template path in data() is never a declaration.
		id = dataCandidate;
	}

	QJsonObject item;
	item.insert(QStringLiteral("surface"), surface);
	item.insert(QStringLiteral("container_path"), containerPath);
	item.insert(QStringLiteral("container_class"), containerClass);
	item.insert(QStringLiteral("text"), text);
	item.insert(QStringLiteral("object_name"), objectName);
	item.insert(QStringLiteral("command"), id);
	item.insert(QStringLiteral("declared_unknown"), unknown);
	return item;
}

//! One user-visible action of a menu, with the container it lives in.
QJsonObject actionItem(const QAction* action, const QString& containerPath, const QString& containerClass)
{
	const QStringList candidates{action->objectName(),
		action->property("controlCommand").toString()};
	// data() is the weakest channel: it is already used for template paths and
	// for config keys, so it only counts when it resolves to a live command.
	const QString data = action->data().toString();
	const bool dataLive = looksLikeCommandId(data) && ControlRegistry::instance()->hasCommand(data);
	return makeItem(QStringLiteral("menu"), containerPath, containerClass,
		displayText(action->text()), action->objectName(), candidates, data, dataLive);
}

void collectActions(const QList<QAction*>& actions, const QString& containerPath,
	const QString& containerClass, QJsonArray& out);

void collectMenu(QMenu* menu, QJsonArray& out)
{
	collectActions(menu->actions(), containerLabel(menu),
		QString::fromLatin1(menu->metaObject()->className()), out);
}

void collectActions(const QList<QAction*>& actions, const QString& containerPath,
	const QString& containerClass, QJsonArray& out)
{
	for (QAction* action : actions)
	{
		if (action == nullptr || action->isSeparator())
		{
			continue;
		}
		if (QMenu* submenu = action->menu())
		{
			collectActions(submenu->actions(), containerLabel(submenu),
				QString::fromLatin1(submenu->metaObject()->className()), out);
			continue;
		}
		out.append(actionItem(action, containerPath, containerClass));
	}
}

// The toolbar items. LMMS's "toolbar" is a plain QWidget (objectName
// "mainToolbar") holding ToolButtons that connect clicked() straight to a
// slot, so most of them carry no QAction at all; a button with a popup menu
// (New from template, Recently opened) brings its menu along.

//! One toolbar button: the label a human sees, the command it declares, and
//! its popup menu if it has one.
void collectToolButton(QToolButton* button, const QString& bar, const QString& barClass,
	QJsonArray& out)
{
	QAction* action = button->defaultAction();
	QObject* owner = action != nullptr ? static_cast<QObject*>(action) : button;
	const QString objectName = owner->objectName();
	QString text = action != nullptr ? displayText(action->text()) : QString();
	if (text.isEmpty())
	{
		text = displayText(button->toolTip());
	}
	if (text.isEmpty())
	{
		return; // a pure icon with no label is not nameable
	}
	const QStringList candidates{objectName, owner->property("controlCommand").toString()};
	const QString data = action != nullptr ? action->data().toString() : QString();
	const bool dataLive = looksLikeCommandId(data) && ControlRegistry::instance()->hasCommand(data);
	out.append(makeItem(QStringLiteral("toolbar"), bar, barClass, text, objectName, candidates,
		data, dataLive));
	if (QMenu* menu = button->menu())
	{
		collectMenu(menu, out);
	}
}

QString stripName(const QWidget* widget)
{
	return widget->objectName().isEmpty() ? QStringLiteral("toolbar") : widget->objectName();
}

void collectToolbar(QWidget* toolbar, QJsonArray& out)
{
	const QString bar = stripName(toolbar);
	const QString barClass = QString::fromLatin1(toolbar->metaObject()->className());
	for (QToolButton* button : toolbar->findChildren<QToolButton*>())
	{
		if (button != nullptr)
		{
			collectToolButton(button, bar, barClass, out);
		}
	}
	// A real QToolBar would carry actions rather than buttons; support it so the
	// reflection does not go blind if the toolbar is ever converted to one.
	for (QToolBar* strip : toolbar->findChildren<QToolBar*>())
	{
		collectActions(strip->actions(), stripName(strip),
			QString::fromLatin1(strip->metaObject()->className()), out);
	}
}

ControlResult surfaceReport()
{
	gui::GuiApplication* application = gui::getGUI();
	gui::MainWindow* window = application != nullptr ? application->mainWindow() : nullptr;
	if (window == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no GUI: the surface report reflects the "
				"main window, which a --no-gui / render-only run never constructs"));
	}

	QJsonArray items;
	if (QMenuBar* bar = window->menuBar())
	{
		for (QAction* action : bar->actions())
		{
			if (action != nullptr && action->menu() != nullptr)
			{
				collectMenu(action->menu(), items);
			}
		}
	}
	if (QWidget* toolbar = window->findChild<QWidget*>(QStringLiteral("mainToolbar")))
	{
		collectToolbar(toolbar, items);
	}

	int unregistered = 0;
	for (const QJsonValue& value : items)
	{
		if (value.toObject().value(QStringLiteral("command")).toString().isEmpty())
		{
			++unregistered;
		}
	}

	QJsonObject result;
	result.insert(QStringLiteral("actions"), items);
	result.insert(QStringLiteral("action_count"), items.size());
	result.insert(QStringLiteral("unregistered_count"), unregistered);
	result.insert(QStringLiteral("reflection"),
		QStringLiteral("menus: menuBar() walked recursively; toolbar: the "
			"\"mainToolbar\" widget's QToolButtons and any QToolBar. "
			"Actions created at runtime (user templates, recent projects, "
			"plugin-provided tools) appear with their container class/path so "
			"the gate can account for them."));
	return ControlResult::success(result);
}

} // namespace

void registerSurfaceCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("control.surface_report");
	cmd.group = QStringLiteral("control");
	cmd.verb = QStringLiteral("surface_report");
	cmd.description = QStringLiteral("Reflect the live menu/toolbar surface: every "
		"user-visible action and the command id it declares (SPEC A15).");
	cmd.requiresEngine = false;
	cmd.argsSchema = QJsonObject{
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), QJsonObject()},
		{QStringLiteral("required"), QJsonArray()},
		{QStringLiteral("additionalProperties"), false}};
	cmd.resultSchema = QJsonObject{
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), QJsonObject{
			{QStringLiteral("actions"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("action_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("unregistered_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("reflection"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
		{QStringLiteral("required"), QJsonArray()},
		{QStringLiteral("additionalProperties"), false}};
	cmd.handler = [](const QJsonObject&) { return surfaceReport(); };
	registry.registerCommand(cmd);
}

} // namespace lmms
