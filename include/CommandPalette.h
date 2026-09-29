/*
 * CommandPalette.h - M3.4: every menu action and every argument-free command, searchable
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

#ifndef LMMS_GUI_COMMAND_PALETTE_H
#define LMMS_GUI_COMMAND_PALETTE_H

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>

#include "lmms_export.h"

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenuBar;

namespace lmms::gui
{

/*! One thing the palette can run. A MENU entry triggers the real QAction, so its dialog,
 *  its question and its shortcut behave exactly as the menu's; a REGISTRY entry (a command
 *  that takes no required argument and no menu carries) is dispatched through the registry.
 *  Both are the same implementations the menus and the socket use (SPEC A11) - the palette
 *  adds a way to reach them, never a second path.
 */
struct PaletteEntry
{
	QString label;     //!< what the user reads ("Save As...", "transport.play")
	QString path;      //!< where it lives ("File", "View ▸ Modules"), or "Command" for registry-only
	QString shortcut;  //!< the action's own shortcut, rendered, or empty
	QString command;   //!< the registered id it declares or is, or empty
	QString detail;    //!< a registry entry's first sentence of description
	QPointer<QAction> action;  //!< null for a registry entry
};

/*! Every enabled, visible, non-separator action under @a bar, recursively, with its menu
 *  path. Menus that rebuild on show (View) are asked to rebuild first. */
LMMS_EXPORT QList<PaletteEntry> paletteMenuEntries(QMenuBar* bar);

/*! The registry's commands that take no required argument, declare no `requires`, and are
 *  not already one of @a menuEntries' declared commands. */
LMMS_EXPORT QList<PaletteEntry> paletteRegistryEntries(const QList<PaletteEntry>& menuEntries);

/*! @a entries that match @a query, best first. A match is every query character in order
 *  (case-insensitive) in the label, then in "path label"; a prefix beats a word start beats
 *  a substring beats a scattered match, and the label beats the path. An empty query
 *  keeps every entry in its given order. */
LMMS_EXPORT QList<PaletteEntry> paletteMatches(const QList<PaletteEntry>& entries, const QString& query);

/*! Runs @a entry: triggers its action, or dispatches its command with no arguments.
 *  Returns the one-line outcome the palette shows (empty for an action, which shows its
 *  own result). */
LMMS_EXPORT QString paletteActivate(const PaletteEntry& entry);

//! The palette window: a filter line over the list, Enter runs, Escape closes.
class LMMS_EXPORT CommandPalette : public QDialog
{
	Q_OBJECT
public:
	CommandPalette(QMenuBar* bar, QWidget* parent = nullptr);

	//! The entries it was built with (the test's view of it).
	const QList<PaletteEntry>& entries() const { return m_entries; }
	//! What the list shows now, in order.
	const QList<PaletteEntry>& shown() const { return m_shown; }

	void setQuery(const QString& query);
	//! Runs the shown entry at @a row; false when there is none.
	bool activateRow(int row);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void refill();

	QList<PaletteEntry> m_entries;
	QList<PaletteEntry> m_shown;
	QLineEdit* m_filter = nullptr;
	QListWidget* m_list = nullptr;
	QLabel* m_status = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_PALETTE_H
