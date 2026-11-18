/*
 * ToolWindowMemory.cpp - tool windows open where the user last left them
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

#include "ToolWindowMemory.h"

#include <algorithm>
#include <utility>

#include <QEvent>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QScreen>
#include <QWidget>

#include "ConfigManager.h"
#include "ControllerRackView.h"
#include "GuiApplication.h"
#include "MicrotunerConfig.h"
#include "PianoRoll.h"
#include "ProjectNotes.h"

namespace lmms::gui::toolwindows
{

namespace
{

const QString kSection = QStringLiteral("ui");
const QString kKey = QStringLiteral("tools.geometry");
const char* const kWatcherName = "zeneToolGeometry";
//! Room kept above a placed window for its title bar, which lies outside the geometry a window reports.
constexpr int kTitleRoom = 32;

void store(const QString& key, const QWidget* window)
{
	QJsonObject all = remembered();
	const QRect g = window->geometry();
	const QScreen* screen = window->screen();
	all.insert(key, QJsonObject{{QStringLiteral("screen"), screen != nullptr ? screen->name() : QString()},
		{QStringLiteral("x"), g.x()}, {QStringLiteral("y"), g.y()},
		{QStringLiteral("width"), g.width()}, {QStringLiteral("height"), g.height()}});
	ConfigManager::inst()->setValue(kSection, kKey, QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
}

//! Keeps a tool window's place: a move or a resize of the shown window, not maximised, is remembered.
class Watcher : public QObject
{
public:
	Watcher(QString key, QObject* parent) : QObject(parent), m_key(std::move(key)) { setObjectName(QLatin1String(kWatcherName)); }

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
		{
			const auto* window = static_cast<QWidget*>(watched);
			const bool normal = (window->windowState() & (Qt::WindowMaximized | Qt::WindowMinimized | Qt::WindowFullScreen)) == 0;
			if (window->isWindow() && window->isVisible() && normal) { store(m_key, window); }
		}
		return QObject::eventFilter(watched, event);
	}

private:
	QString m_key;
};

} // namespace

const QScreen* screenFor(const QString& name, const QPoint& centre)
{
	for (const QScreen* screen : QGuiApplication::screens())
	{
		if (screen->name() == name) { return screen; }
	}
	const QScreen* under = QGuiApplication::screenAt(centre);
	return under != nullptr ? under : QGuiApplication::primaryScreen();
}

QString keyOf(const QWidget* content)
{
	GuiApplication* gui = getGUI();
	if (content == nullptr || gui == nullptr) { return {}; }
	if (content == gui->getControllerRackView()) { return QStringLiteral("controller_rack"); }
	if (content == gui->getProjectNotes()) { return QStringLiteral("project_notes"); }
	if (content == gui->getMicrotunerConfig()) { return QStringLiteral("microtuner"); }
	if (content == gui->pianoRoll()) { return QStringLiteral("classic_piano_roll"); }
	return {};
}

QJsonObject remembered()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(kSection, kKey).toUtf8()).object();
}

QRect fitted(const QRect& saved, const QRect& area)
{
	if (area.isEmpty()) { return saved; }
	QRect out(saved.topLeft(), saved.size().boundedTo(area.size()));
	out.moveLeft(std::clamp(out.left(), area.left(), area.right() + 1 - out.width()));
	out.moveTop(std::clamp(out.top(), area.top(), area.bottom() + 1 - out.height()));
	return out;
}

bool placeRemembered(QWidget* content)
{
	const QString key = keyOf(content);
	const QJsonObject saved = key.isEmpty() ? QJsonObject() : remembered().value(key).toObject();
	const QRect rect(saved.value(QStringLiteral("x")).toInt(), saved.value(QStringLiteral("y")).toInt(),
		saved.value(QStringLiteral("width")).toInt(), saved.value(QStringLiteral("height")).toInt());
	if (saved.isEmpty() || rect.width() < 1 || rect.height() < 1) { return false; }
	const QScreen* screen = screenFor(saved.value(QStringLiteral("screen")).toString(), rect.center());
	const QRect area = screen != nullptr ? screen->availableGeometry().adjusted(0, kTitleRoom, 0, 0) : QRect();
	content->setGeometry(fitted(rect, area));
	return true;
}

void remember(QWidget* content)
{
	const QString key = keyOf(content);
	if (key.isEmpty() || content->findChild<QObject*>(QLatin1String(kWatcherName), Qt::FindDirectChildrenOnly) != nullptr)
	{
		return;
	}
	content->installEventFilter(new Watcher(key, content));
}

} // namespace lmms::gui::toolwindows
