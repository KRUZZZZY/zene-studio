/*
 * CommandListBuild.h - the small builders every ListSpec is declared with
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

#ifndef LMMS_GUI_COMMAND_LIST_BUILD_H
#define LMMS_GUI_COMMAND_LIST_BUILD_H

#include <utility>

#include <QObject>

#include "CommandListDialog.h"

namespace lmms::gui::CommandLists
{

/*! The builders CommandLists.cpp declared its surfaces with, shared so a lane's own list file
 *  (CommandListsCustom.cpp, 0.5.0 W3) declares its surfaces the same way instead of copying them. */

inline QString tr(const char* text)
{
	return QObject::tr(text);
}

//! A row action: @a id with the row fields @a fromRow mapped onto its arguments.
inline RowAction act(const char* label, const char* id, QHash<QString, QString> fromRow, QJsonObject fixed = {}, bool ask = false)
{
	RowAction action;
	action.label = tr(label);
	action.id = QLatin1String(id);
	action.fromRow = std::move(fromRow);
	action.fixed = std::move(fixed);
	action.ask = ask;
	return action;
}

inline ListExtra extra(const char* label, const char* id, QJsonObject fixed = {}, bool ask = true)
{
	return ListExtra{tr(label), CommandCall{QLatin1String(id), std::move(fixed), {}, tr(label), ask}};
}

//! An extra that reads something and shows the answer (a floating message).
inline ListExtra readExtra(const char* label, const char* id)
{
	return ListExtra{tr(label), CommandCall{QLatin1String(id), {}, {}, tr(label), false, true}};
}

inline ListSpec spec(const char* title, const char* listId, const char* arrayKey, QStringList columns, QJsonObject args = {})
{
	ListSpec out;
	out.title = tr(title);
	out.listId = QLatin1String(listId);
	out.arrayKey = QLatin1String(arrayKey);
	out.columns = std::move(columns);
	out.listArgs = std::move(args);
	return out;
}

} // namespace lmms::gui::CommandLists

#endif // LMMS_GUI_COMMAND_LIST_BUILD_H
