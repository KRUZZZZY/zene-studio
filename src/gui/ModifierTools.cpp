/*
 * ModifierTools.cpp - a second arranger tool while a modifier key is held
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
#include "ModifierTools.h"

#include <QApplication>
#include <QCursor>
#include <QJsonObject>
#include <QKeyEvent>

#include "ArrangerOverlays.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "GuiApplication.h"
#include "SongEditor.h"

namespace lmms::gui::modifiertools
{

namespace
{

QString modifierOf(int key)
{
	switch (key)
	{
	case Qt::Key_Alt: return QStringLiteral("alt");
	case Qt::Key_Control: return QStringLiteral("ctrl");
	case Qt::Key_Shift: return QStringLiteral("shift");
	default: return {};
	}
}

//! The arranger's window when the pointer is over it.
SongEditor* arrangerUnderPointer()
{
	GuiApplication* g = getGUI();
	SongEditorWindow* window = g != nullptr ? g->songEditor() : nullptr;
	if (window == nullptr || !window->isVisible()) { return nullptr; }
	return window->rect().contains(window->mapFromGlobal(QCursor::pos())) ? window->m_editor : nullptr;
}

void setTool(const QString& tool)
{
	ControlRegistry::instance()->invoke(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), tool}});
}

//! Swaps the tool in while its key is held and puts the previous one back when the key, or the window, is let go.
class Filter : public QObject
{
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
		{
			const auto* key = static_cast<QKeyEvent*>(event);
			if (!key->isAutoRepeat()) { onKey(modifierOf(key->key()), event->type() == QEvent::KeyPress); }
		}
		else if (event->type() == QEvent::ApplicationDeactivate) { restore(); }
		return QObject::eventFilter(watched, event);
	}

private:
	void onKey(const QString& modifier, bool pressed)
	{
		if (modifier.isEmpty()) { return; }
		if (!pressed) { if (modifier == m_held) { restore(); } return; }
		const QString tool = bindings().value(modifier);
		SongEditor* editor = m_held.isEmpty() ? arrangerUnderPointer() : nullptr;
		if (tool.isEmpty() || editor == nullptr) { return; }
		m_previous = currentArrangerTool(editor);
		m_held = modifier;
		setTool(tool);
	}

	void restore()
	{
		if (m_held.isEmpty()) { return; }
		m_held.clear();
		setTool(m_previous);
	}

	QString m_held;
	QString m_previous;
};

ControlResult modifierTools(const QJsonObject& args)
{
	QMap<QString, QString> current = bindings();
	for (const QString& modifier : {QStringLiteral("alt"), QStringLiteral("ctrl"), QStringLiteral("shift")})
	{
		if (!args.contains(modifier)) { continue; }
		const QString tool = args.value(modifier).toString();
		if (!tool.isEmpty() && !arrangerToolNames().contains(tool))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("no arranger tool '%1'").arg(tool));
		}
		if (tool.isEmpty()) { current.remove(modifier); } else { current.insert(modifier, tool); }
	}
	setBindings(current);
	QJsonObject result;
	for (auto it = current.constBegin(); it != current.constEnd(); ++it) { result.insert(it.key(), it.value()); }
	return ControlResult::success({{QStringLiteral("bindings"), result}});
}

} // namespace

QMap<QString, QString> bindings()
{
	QMap<QString, QString> out;
	const QString saved = ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("modifiertools"));
	for (const QString& pair : saved.split(QLatin1Char(';'), Qt::SkipEmptyParts))
	{
		const QStringList parts = pair.split(QLatin1Char('='));
		if (parts.size() == 2 && arrangerToolNames().contains(parts[1])) { out.insert(parts[0], parts[1]); }
	}
	return out;
}

void setBindings(const QMap<QString, QString>& bindings)
{
	QStringList pairs;
	for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) { pairs << it.key() + QLatin1Char('=') + it.value(); }
	ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("modifiertools"), pairs.join(QLatin1Char(';')));
}

void install()
{
	static Filter* filter = nullptr;
	if (filter != nullptr || qApp == nullptr) { return; }
	filter = new Filter(qApp);
	qApp->installEventFilter(filter);
}

} // namespace lmms::gui::modifiertools

namespace lmms
{

void registerModifierToolCommands(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("arrangement", "modifier_tools");
	cmd.description = QStringLiteral("Bind a second arranger tool to a modifier key: while 'alt', 'ctrl' or 'shift' is held over the "
		"arranger, its tool is the one named (draw, knife, select, range, audition, zoom, smart), and letting go puts the "
		"previous tool back. An empty name unbinds the key. A preference (config ui/modifiertools); none by default. Reports "
		"the bindings.");
	cmd.argsSchema = control::objectSchema({{QStringLiteral("alt"), control::stringProperty()}, {QStringLiteral("ctrl"), control::stringProperty()},
		{QStringLiteral("shift"), control::stringProperty()}});
	cmd.resultSchema = control::objectProperty();
	cmd.handler = gui::modifiertools::modifierTools;
	registry.registerCommand(cmd);
}

} // namespace lmms
