/*
 * UiConfigStore.cpp - interface state kept as JSON in the config file, written as one undo step
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

} // namespace

QJsonObject read(const QString& key)
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(Section, key).toUtf8()).object();
}

ControlResult write(const QString& key, const QJsonObject& value, QJsonObject result,
	const Inverse& inverse, const std::function<void()>& apply)
{
	const QJsonObject previous = read(key);
	control::addUndoStep(
		[key, previous, apply] { store(key, previous); if (apply) { apply(); } },
		[key, value, apply] { store(key, value); if (apply) { apply(); } });
	store(key, value);
	if (apply) { apply(); }
	result.insert(QStringLiteral("__transaction"), control::transactionPayload(previous, inverse.op, inverse.args, true,
		QStringLiteral("action checkpoint: the recorded undo step writes the previous ui/%1 object back, saves "
			"the config file and re-applies it to the interface, exactly as this command does").arg(key)));
	return ControlResult::success(result);
}

} // namespace lmms::gui::uistore
