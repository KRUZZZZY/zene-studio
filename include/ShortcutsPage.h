/*
 * ShortcutsPage.h - M3 item 4: every keyboard shortcut, from the menus that own them
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

#ifndef LMMS_GUI_SHORTCUTS_PAGE_H
#define LMMS_GUI_SHORTCUTS_PAGE_H

#include <QDialog>
#include <QList>

#include "CommandPalette.h"
#include "lmms_export.h"

class QLineEdit;
class QMenu;
class QMenuBar;
class QPushButton;

namespace lmms::gui
{

class ListTable;

/*! The menu entries under @a bar that carry a shortcut, ordered by where they live. Read from
 *  the live menus (the palette's own walk, paletteMenuEntries), so the page cannot drift from
 *  the shortcuts that actually fire. */
LMMS_EXPORT QList<PaletteEntry> shortcutEntries(QMenuBar* bar);

/*! Help > Keyboard Shortcuts (M3 item 4): a searchable table of shortcut, action, menu path
 *  and the registry command the action declares. Read-only - it lists, it does not remap.
 *  0.5.0 design themes 18 and 21: grouped by task - the manual's sections (HelpTasks.h), the first
 *  column and the order - with each command by its friendly name (CommandMetadata; its id in the
 *  tooltip and found by the filter); the shared list's sorting and keys (ListTable): Enter runs the
 *  highlighted action, the Menu key or Shift+F10 offers Run and its Command Reference entry. */
class LMMS_EXPORT ShortcutsPage : public QDialog
{
	Q_OBJECT
public:
	explicit ShortcutsPage(QMenuBar* bar, QWidget* parent = nullptr);

	int rowCount() const;
	int visibleRowCount() const;
	void setFilter(const QString& text);
	//! The task column's text for row @a row in the list's own order (the test's view of the grouping).
	QString taskOf(int row) const;
	//! Run the action of row @a row (its data index); false when it has no action left.
	bool runRow(int row);
	//! Q050-202: the shipped keymap presets, as a menu of shortcuts.preset_apply calls.
	QMenu* presetMenu() const { return m_presetMenu; }

private:
	void openRowMenu(int row, const QPoint& at);

	QList<PaletteEntry> m_entries;
	QStringList m_tasks;
	QLineEdit* m_filter = nullptr;
	ListTable* m_table = nullptr;
	QMenu* m_presetMenu = nullptr;
	QPushButton* m_presets = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHORTCUTS_PAGE_H
