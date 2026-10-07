/*
 * MainWindowGeometry.cpp - the main window opens where, and as large as, the user last left it
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

#include "MainWindowGeometry.h"

#include <QJsonDocument>
#include <QScreen>
#include <QWidget>

#include "ConfigManager.h"
#include "ToolWindowMemory.h"

namespace lmms::gui::mainwindowgeometry
{

namespace
{

const QString kSection = QStringLiteral("ui");
const QString kKey = QStringLiteral("main.geometry");
//! Room kept above the window for its title bar, which lies outside the geometry a window reports
//! (the tool windows keep the same, ToolWindowMemory.cpp).
constexpr int kTitleRoom = 32;

QRect rectOf(const QJsonObject& saved)
{
	return QRect(saved.value(QStringLiteral("x")).toInt(), saved.value(QStringLiteral("y")).toInt(),
		saved.value(QStringLiteral("width")).toInt(), saved.value(QStringLiteral("height")).toInt());
}

} // namespace

void save(const QWidget* window)
{
	const bool maximized = window->isMaximized() || window->isFullScreen();  // full screen comes back maximised
	// The normal place, so un-maximising after the next start lands where the window was before it was maximised.
	const QRect normal = maximized && !window->normalGeometry().isEmpty() ? window->normalGeometry() : window->geometry();
	const QScreen* screen = window->screen();
	const QJsonObject place{{QStringLiteral("screen"), screen != nullptr ? screen->name() : QString()},
		{QStringLiteral("x"), normal.x()}, {QStringLiteral("y"), normal.y()},
		{QStringLiteral("width"), normal.width()}, {QStringLiteral("height"), normal.height()},
		{QStringLiteral("maximized"), maximized}};
	ConfigManager::inst()->setValue(kSection, kKey, QString::fromUtf8(QJsonDocument(place).toJson(QJsonDocument::Compact)));
	ConfigManager::inst()->saveConfigFile();
}

QJsonObject remembered()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(kSection, kKey).toUtf8()).object();
}

QRect placement(const QJsonObject& saved)
{
	const QRect rect = rectOf(saved);
	if (rect.width() < 1 || rect.height() < 1) { return {}; }
	const QScreen* screen = toolwindows::screenFor(saved.value(QStringLiteral("screen")).toString(), rect.center());
	const QRect area = screen != nullptr ? screen->availableGeometry().adjusted(0, kTitleRoom, 0, 0) : QRect();
	return toolwindows::fitted(rect, area);
}

bool showRestored(QWidget* window)
{
	const QJsonObject saved = remembered();
	const QRect place = placement(saved);
	if (place.isEmpty())
	{
		window->showMaximized();
		return false;
	}
	// The normal place first, then the state: un-maximising later returns to it.
	window->showNormal();
	window->setGeometry(place);
	if (saved.value(QStringLiteral("maximized")).toBool()) { window->showMaximized(); }
	return true;
}

} // namespace lmms::gui::mainwindowgeometry
