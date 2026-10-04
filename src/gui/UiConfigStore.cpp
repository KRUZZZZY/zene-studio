/*
 * UiConfigStore.cpp - interface state kept as JSON in the config file (or with the project), written as one undo step
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

#include "UiConfigStore.h"

#include <QJsonDocument>

#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::gui::uistore
{

namespace
{

const QString Section = QStringLiteral("ui");

void store(const QString& key, const QJsonObject& value)
{
	ConfigManager::inst()->setValue(Section, key, QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
	ConfigManager::inst()->saveConfigFile();
}

using Put = std::function<void(const QJsonObject&)>;

//! @a put @a value now and record ONE undo step that puts @a previous back (and a redo that puts @a value
//! again), running @a apply after each; @a result with the transaction attached.
ControlResult recordWrite(const Put& put, const QJsonObject& previous, const QJsonObject& value, QJsonObject result,
	const Inverse& inverse, const std::function<void()>& apply, const QString& mechanism)
{
	control::addUndoStep(
		[put, previous, apply] { put(previous); if (apply) { apply(); } },
		[put, value, apply] { put(value); if (apply) { apply(); } });
	put(value);
	if (apply) { apply(); }
	result.insert(QStringLiteral("__transaction"), control::transactionPayload(previous, inverse.op, inverse.args, true, mechanism));
	return ControlResult::success(result);
}

} // namespace

QJsonObject read(const QString& key)
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(Section, key).toUtf8()).object();
}

ControlResult write(const QString& key, const QJsonObject& value, QJsonObject result,
	const Inverse& inverse, const std::function<void()>& apply)
{
	return recordWrite([key](const QJsonObject& doc) { store(key, doc); }, read(key), value, std::move(result), inverse, apply,
		QStringLiteral("action checkpoint: the recorded undo step writes the previous ui/%1 object back, saves "
			"the config file and re-applies it to the interface, exactly as this command does").arg(key));
}

ControlResult writeProject(ProjectWorkspace::Part part, const QJsonObject& value, QJsonObject result,
	const Inverse& inverse, const std::function<void()>& apply)
{
	const Put put = [part](const QJsonObject& doc) {
		ProjectWorkspace::instance().setPart(part, doc);
		if (Song* song = Engine::getSong()) { song->setModified(); }
	};
	const QString what = part == ProjectWorkspace::Part::Workflows ? QStringLiteral("workflows") : QStringLiteral("layout");
	return recordWrite(put, ProjectWorkspace::instance().part(part), value, std::move(result), inverse, apply,
		QStringLiteral("action checkpoint: the recorded undo step puts the project's previous %1 back (<workspace>, "
			"saved with the project) and re-applies it to the interface, exactly as this command does").arg(what));
}

} // namespace lmms::gui::uistore
