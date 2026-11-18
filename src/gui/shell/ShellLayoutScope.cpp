/*
 * ShellLayoutScope.cpp - the personal layout put aside while a project's own layout shows
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

/*! Design theme 22: project and personal layouts stay distinct. The shell persists whatever is on screen as the
 *  user's layout (ui/shell.layout), so before this a project's own layout, once applied, became everybody's: the
 *  next project without one, or a new one, opened in it. Now the personal layout is put aside (ui/shell.personal)
 *  when a project's layout replaces it, and comes back when the project is left. Interface state, as the layout
 *  itself is: no undo step, nothing in the project. */

#include "Shell.h"

#include <QJsonDocument>

#include "ConfigManager.h"

namespace lmms::gui
{

namespace
{

const QString kSection = QStringLiteral("ui");
const QString kAside = QStringLiteral("shell.personal");

} // namespace

void Shell::setPersonalAside()
{
	if (personalAside()) { return; }  // the first is the user's; a later one is another project's
	ConfigManager::inst()->setValue(kSection, kAside, QString::fromUtf8(QJsonDocument(snapshot()).toJson(QJsonDocument::Compact)));
}

bool Shell::restorePersonal()
{
	const QString text = ConfigManager::inst()->value(kSection, kAside);
	const QJsonObject doc = QJsonDocument::fromJson(text.toUtf8()).object();
	if (doc.isEmpty()) { return false; }
	dropPersonalAside();
	restoreSnapshot(doc);
	return true;
}

void Shell::dropPersonalAside()
{
	ConfigManager::inst()->setValue(kSection, kAside, QString());
}

bool Shell::personalAside()
{
	return !ConfigManager::inst()->value(kSection, kAside).isEmpty();
}

} // namespace lmms::gui
