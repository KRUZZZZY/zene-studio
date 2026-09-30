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
class QMenuBar;
class QTableWidget;

namespace lmms::gui
{

/*! The menu entries under @a bar that carry a shortcut, ordered by where they live. Read from
 *  the live menus (the palette's own walk, paletteMenuEntries), so the page cannot drift from
 *  the shortcuts that actually fire. */
LMMS_EXPORT QList<PaletteEntry> shortcutEntries(QMenuBar* bar);

/*! Help > Keyboard Shortcuts (M3 item 4): a searchable table of shortcut, action, menu path
 *  and the registry command the action declares. Read-only - it lists, it does not remap. */
class LMMS_EXPORT ShortcutsPage : public QDialog
{
public:
	explicit ShortcutsPage(QMenuBar* bar, QWidget* parent = nullptr);

	int rowCount() const;
	int visibleRowCount() const;
	void setFilter(const QString& text);

private:
	QLineEdit* m_filter = nullptr;
	QTableWidget* m_table = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHORTCUTS_PAGE_H
