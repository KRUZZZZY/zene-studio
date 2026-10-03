/*
 * EffectPickerOrganiser.h - favourites, folders and hidden entries in the effect picker
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
#ifndef LMMS_GUI_EFFECT_PICKER_ORGANISER_H
#define LMMS_GUI_EFFECT_PICKER_ORGANISER_H

#include <functional>

#include <QString>

#include "Effect.h"

class QMenu;
class QModelIndex;
class QStandardItemModel;
class QWidget;

namespace lmms::gui::pickerorganiser
{

/*! 0.5.0 L10 (MX-166): the effect picker's rows carry each plug-in's organiser entry
 *  (PluginOrganiser.h) in item roles on the name column, a filter reads them, and a row's menu
 *  changes them through plugin.organise - so a click and a script take one path. The instrument
 *  browser uses the key and the menu too. */

enum Role
{
	KeyRole = Qt::UserRole + 40,
	FavouriteRole,
	HiddenRole,
	FolderRole
};

//! What the picker shows: everything, or only favourites / one folder; hidden entries or not.
struct Filter
{
	bool favouritesOnly = false;
	bool showHidden = false;
	QString folder;  //!< empty: every folder
};

//! The organiser key of a picker entry (the module name and the sub-plugin key's attributes).
QString keyOf(const EffectKey& key);
//! Put row @a row's entry (for @a key) into the name column's roles; the name shows a star for a favourite.
void markRow(QStandardItemModel& model, int row, const EffectKey& key);
//! Whether a source row passes @a filter.
bool accepts(const QModelIndex& nameIndex, const Filter& filter);
//! Fill @a menu for the row at @a nameIndex; @a changed runs after a change so the picker refreshes.
void fillRowMenu(QMenu* menu, const QModelIndex& nameIndex, QWidget* parent, const std::function<void()>& changed);
//! The same entries for the plug-in @a key names (the instrument browser's rows).
void fillKeyMenu(QMenu* menu, const QString& key, QWidget* parent, const std::function<void()>& changed);

} // namespace lmms::gui::pickerorganiser

#endif // LMMS_GUI_EFFECT_PICKER_ORGANISER_H
