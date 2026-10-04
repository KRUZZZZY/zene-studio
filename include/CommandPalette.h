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
class QMenuBar;
class QPushButton;
class QTreeWidget;

namespace lmms::gui
{

/*! One thing the palette can run. A MENU entry triggers the real QAction, so its dialog,
 *  its question and its shortcut behave exactly as the menu's; a REGISTRY entry (a command
 *  no menu carries) is dispatched through the registry or opened in its form.
 *  Both are the same implementations the menus and the socket use (SPEC A11) - the palette
 *  adds a way to reach them, never a second path.
 *  0.5.0 theme 4: an entry reads by its name (a menu's label, or commandName for a registry
 *  command) with the command id secondary, and carries whether it can be undone.
 */
struct PaletteEntry
{
	QString label;     //!< what the user reads ("Save As...", "Set Tempo")
	QString path;      //!< where it lives ("File", "View ▸ Modules"), or a registry command's group ("Transport")
	QString shortcut;  //!< the action's own shortcut, rendered, or empty
	QString command;   //!< the registered id it declares or is, or empty
	QString detail;    //!< a registry entry's whole description
	QPointer<QAction> action;  //!< null for a registry entry
	//! A registry entry whose command takes arguments: running it CONFIGURES - opens the
	//! command's form (CommandForm), pre-filled from the shell selection - rather than
	//! dispatching it bare. "Run with defaults" (paletteRunWithDefaults) is the secondary way.
	bool form = false;
	//! The command leaves no automatic undo step (commandIsIrreversible): flagged in the list,
	//! and a bare run from the palette asks first.
	bool irreversible = false;
};
/*! Every enabled, visible, non-separator action under @a bar, recursively, with its menu
 *  path. Menus that rebuild on show (View) are asked to rebuild first. */
LMMS_EXPORT QList<PaletteEntry> paletteMenuEntries(QMenuBar* bar);

/*! The registry's commands that take no required argument, declare no `requires`, and are
 *  not already one of @a menuEntries' declared commands. One with optional arguments is a form
 *  entry (`form`): Configure is its primary action. */
LMMS_EXPORT QList<PaletteEntry> paletteRegistryEntries(const QList<PaletteEntry>& menuEntries);

/*! The registry's commands that DO take required arguments (and declare no `requires` and no
 *  menu carries): each runs through its form, so every command is reachable from the palette. */
LMMS_EXPORT QList<PaletteEntry> paletteFormEntries(const QList<PaletteEntry>& menuEntries);

/*! @a entries that match @a query, best first. A match is every query character in order
 *  (case-insensitive) in the label, then in "path label"; a prefix beats a word start beats
 *  a substring beats a scattered match, and the label beats the path. An empty query
 *  keeps every entry in its given order. */
LMMS_EXPORT QList<PaletteEntry> paletteMatches(const QList<PaletteEntry>& entries, const QString& query);

/*! Runs @a entry's PRIMARY action: triggers its action, opens a form entry's form (Configure),
 *  parented to @a formParent, with the selection's ids filled in, or dispatches an argument-free
 *  command - asking first when it cannot be undone. Returns the one-line outcome the palette
 *  shows (empty for an action, which shows its own result). */
LMMS_EXPORT QString paletteActivate(const PaletteEntry& entry, QWidget* formParent = nullptr);

//! Whether @a entry offers "Run with defaults": a form entry whose required arguments (if any)
//! the shell selection fills.
LMMS_EXPORT bool paletteCanRunWithDefaults(const PaletteEntry& entry);

/*! The SECONDARY action of a form entry: dispatch it with no argument but the required ones the
 *  selection fills, asking first when it cannot be undone. Refused, with what is missing, when the
 *  selection does not fill a required argument. Any other entry runs as paletteActivate does. */
LMMS_EXPORT QString paletteRunWithDefaults(const PaletteEntry& entry, QWidget* parent = nullptr);

/*! The palette window: a filter line over the list (name, where it lives, shortcut, and the
 *  command id dimmed), a detail line for the highlighted entry, and its actions. Enter runs the
 *  primary action (Configure... for a command with arguments), Shift+Enter "Run with defaults";
 *  Escape closes. */
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
	//! Runs the primary action of the shown entry at @a row; false when there is none.
	bool activateRow(int row);
	//! Runs the shown entry at @a row with its defaults (paletteRunWithDefaults); false when there is none.
	bool runRowWithDefaults(int row);
	//! The detail line for the highlighted entry (the test's view of it).
	QString detailText() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void refill();
	bool runRow(int row, bool withDefaults);
	int currentRow() const;
	void showDetail();

	QList<PaletteEntry> m_entries;
	QList<PaletteEntry> m_shown;
	QLineEdit* m_filter = nullptr;
	QTreeWidget* m_list = nullptr;
	QLabel* m_detail = nullptr;
	QPushButton* m_primary = nullptr;
	QPushButton* m_secondary = nullptr;
	QLabel* m_status = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_PALETTE_H
