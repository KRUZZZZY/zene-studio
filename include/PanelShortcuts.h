/*
 * PanelShortcuts.h - keys bound to one panel's own actions, beside the global menu map
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

#ifndef LMMS_GUI_PANEL_SHORTCUTS_H
#define LMMS_GUI_PANEL_SHORTCUTS_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "ShortcutMap.h"
#include "lmms_export.h"

class QKeyEvent;

namespace lmms::gui::panelshortcuts
{

/*! WF-110/111, stage 1 (0.5.0 L13): the four editing panels keep their own small set of bindable
 *  actions. A panel binding is a key and a registered command, and it exists in ONE scope - the
 *  arranger, the note editor, the automation editor or the perform view. The same key may therefore
 *  mean four different things, and only the panel that holds the keyboard focus acts on it; a
 *  conflict is two actions of the same scope on one key. The choices are overrides kept beside the
 *  menu overrides in ui/shortcuts (ShortcutMap owns that store), so a write is the same one undo
 *  step a menu rebind is and a restart reads the same file.
 *
 *  Every action runs a registered command through ControlRegistry, exactly as its menu item, button
 *  or agent call does (SPEC A11): the key is a third way into the same implementation. No action has
 *  a key of its own, so a panel's existing keys keep working until the user binds a scoped action -
 *  and a bound key takes precedence over the panel's built-in meaning while that panel has the
 *  focus (the router claims it through ShortcutOverride, as XyPad does for its arrows). */
struct Action
{
	QString scope;     //!< arranger, note_editor, automation_editor or perform
	QString id;        //!< the stable id shortcuts.list/set use ("Arranger ▸ Zoom In")
	QString label;     //!< what the shortcuts list shows in its "command" column
	QString command;   //!< the registered command the key runs
	QJsonObject args;  //!< the arguments it runs it with
};

//! Every bindable panel action, in scope order.
LMMS_EXPORT const QList<Action>& actions();

//! The scopes, in the order the shortcuts list groups them (arranger first).
LMMS_EXPORT QStringList scopes();
//! The scope's human title ("Arranger", "Note Editor"...); empty for an unknown scope.
LMMS_EXPORT QString scopeTitle(const QString& scope);
LMMS_EXPORT bool knownScope(const QString& scope);

//! The action @a id of @a scope, or nullptr.
LMMS_EXPORT const Action* action(const QString& scope, const QString& id);
//! The action whose id is @a id (ids are unique across scopes), or nullptr.
LMMS_EXPORT const Action* actionById(const QString& id);

//! The stored panel overrides, nested scope -> action id -> portable key text ("" = deliberately
//! unbound). Nested rather than flattened under a separator character: the store is a config XML
//! attribute, and a control character in it makes the next start's config parse fail (found by the
//! restart transcript), while an action id is arbitrary text that must be kept verbatim.
LMMS_EXPORT QJsonObject overrides();
//! The stored value of @a id in @a scope within @a panels: a string key sequence, "" for a
//! deliberate unbind, or Undefined when the scope has no override for it.
LMMS_EXPORT QJsonValue stored(const QJsonObject& panels, const QString& scope, const QString& id);
//! @a panels with @a id in @a scope set to @a keys; an Undefined @a keys removes the entry, and a
//! scope left empty is removed too (so the store says only what the user chose).
LMMS_EXPORT QJsonObject withStored(const QJsonObject& panels, const QString& scope, const QString& id,
	const QJsonValue& keys);

//! Every panel action with its live keys: the override when it has one, else empty.
LMMS_EXPORT QList<ShortcutMap::Binding> bindings();

//! Whether @a event is the key of any action in @a scope (the panel claims it over a window shortcut).
LMMS_EXPORT bool claims(const QString& scope, const QKeyEvent* event);
/*! Act on @a event when it is a key bound in @a scope: run that action's command. True when the key
 *  was bound (the event is consumed even if the command refused, so the key never falls through to a
 *  different panel's meaning). A text-entry widget with the focus keeps its keys: false. */
LMMS_EXPORT bool handle(const QString& scope, QKeyEvent* event);

//! @a event's portable key text ("Ctrl+Alt+J"); empty for a modifier key or an unparsable one.
LMMS_EXPORT QString portableKey(const QKeyEvent* event);

//! Whether the focus is in a widget that types text (a line edit, a text area, a spin box).
LMMS_EXPORT bool textEntryHasFocus();

//! The scope the keyboard focus is in (the panel whose contents hold it), or empty for a dialog,
//! a detached window, or a widget no panel frames.
LMMS_EXPORT QString focusedScope();

//! Install the application-wide router (idempotent; the shell calls it once its menus exist).
LMMS_EXPORT void install();
//! Remove the router (shutdown, and the tests' restart).
LMMS_EXPORT void uninstall();

} // namespace lmms::gui::panelshortcuts

#endif // LMMS_GUI_PANEL_SHORTCUTS_H
