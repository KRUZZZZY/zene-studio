/*
 * ShortcutMap.h - rebindable keyboard shortcuts over the live menus
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

#ifndef LMMS_GUI_SHORTCUT_MAP_H
#define LMMS_GUI_SHORTCUT_MAP_H

#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QAction;
class QMenuBar;

namespace lmms::gui::ShortcutMap
{

/*! WF-110/111: every menu action can be given any key. A binding's id is where the action
 *  lives and what it says - "File ▸ Save" - read from the live menus (the palette's walk), so
 *  it needs no registry of its own and survives a rebuild of a menu. The user's choices are
 *  OVERRIDES of each action's own default, kept in ui/shortcuts (UiConfigStore); an action
 *  without an override keeps the shortcut the code gives it. */
struct Binding
{
	QString id;        //!< "File ▸ Save", or a panel action's id
	QString scope;     //!< a panel scope (arranger, note_editor, automation_editor, perform), else empty
	QString command;   //!< the registry command the action declares, or empty
	QString keys;      //!< current, portable text ("Ctrl+S"); empty when unbound
	QString defaults;  //!< the code's own shortcut, portable text
	bool overridden = false;
	QPointer<QAction> action;
};

//! The config key the overrides live under (ui/<StoreKey>, an object {"overrides": {id: keys}}).
extern const char* const StoreKey;

//! Every bindable action under @a bar, in menu order.
LMMS_EXPORT QList<Binding> bindings(QMenuBar* bar);

//! The stored overrides (id -> portable key text; "" = deliberately unbound).
LMMS_EXPORT QJsonObject overrides();

//! Give every action under @a bar its override, or its default back when it has none. Each
//! action's default is remembered the first time it is seen, so resetting is exact.
LMMS_EXPORT void apply(QMenuBar* bar);

//! @a keys normalised to portable text; empty with @a ok false when it does not parse.
LMMS_EXPORT QString portable(const QString& keys, bool* ok = nullptr);

//! The ids in @a all bound to @a keys, other than @a except.
LMMS_EXPORT QStringList conflicts(const QList<Binding>& all, const QString& keys, const QString& except);

} // namespace lmms::gui::ShortcutMap

#endif // LMMS_GUI_SHORTCUT_MAP_H
