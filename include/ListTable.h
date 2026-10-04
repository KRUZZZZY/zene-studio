/*
 * ListTable.h - the shared list: find, sort and the keyboard's row actions
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

#ifndef LMMS_GUI_LIST_TABLE_H
#define LMMS_GUI_LIST_TABLE_H

#include <QList>
#include <QStringList>
#include <QTableWidget>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

//! One row as a ListTable shows it: its cells, a tooltip per cell (empty for none) and words a find matches though
//! no cell shows them (a command's id, a shortcut's portable spelling).
struct ListRow
{
	QStringList cells;
	QStringList tips;
	QString hiddenWords;
};

/*! 0.5.0 design theme 18: the one list behind the generic list surfaces (CommandListDialog, the shortcuts
 *  page) - what every list shares, whatever its rows:
 *  - find: setFilter() hides the rows no cell (nor the row's hidden words) contains, as typed;
 *  - sort: a header click sorts by that column - numbers by value ("9 dB" before "10 dB"), text as the
 *    locale orders it - a second click reverses it and a third puts the list's own order back (the
 *    command's order: chronological revisions stay chronological until asked otherwise); addSortMenu() offers
 *    the same from the keyboard;
 *  - row actions from the keyboard: Enter (or a double-click) is rowActivated - the list's primary action -
 *    and the Menu key, Shift+F10 or a right-click is rowMenuRequested, for the row's actions.
 *  Rows are addressed by their DATA index - their place in the list setRows() was given - never by where a
 *  sort or a find has put them, so an action always reaches the row the user chose. */
class LMMS_EXPORT ListTable : public QTableWidget
{
	Q_OBJECT
public:
	explicit ListTable(QWidget* parent = nullptr);

	//! Show @a rows under @a headers, sorted and filtered as they were; the row at data index @a keep stays current.
	void setRows(const QStringList& headers, const QList<ListRow>& rows, int keep = -1);
	//! The data index of the current row, or -1 (none, or hidden by the find).
	int currentDataRow() const;
	//! Make the row at data index @a index current (none for -1).
	void selectDataRow(int index);
	//! Hide the rows that do not contain @a text (any cell or the hidden words, case-insensitively).
	void setFilter(const QString& text);
	int visibleRowCount() const;
	//! Sort by @a column (-1: the list's own order), @a ascending or not.
	void sortRows(int column, bool ascending = true);
	int sortColumn() const { return m_sortColumn; }
	//! A "Sort By" submenu on @a menu: one entry per column, then "List Order".
	void addSortMenu(QMenu* menu);

signals:
	void rowActivated(int dataRow);
	void rowMenuRequested(int dataRow, const QPoint& globalPos);

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	void redraw(int keep);
	void applyFilter();
	void requestMenu(const QPoint& globalPos);
	//! The sort indicator a header click moves on from: ascending, descending, then the list's order.
	void cycleSort(int column);

	QStringList m_headers;
	QList<ListRow> m_rows;
	QList<int> m_order;  //!< shown row -> data index
	QString m_filter;
	int m_sortColumn = -1;
	bool m_ascending = true;
	bool m_keyMenuShown = false;  //!< Shift+F10 opened the menu: a keyboard context event right after is the same request
};

} // namespace lmms::gui

#endif // LMMS_GUI_LIST_TABLE_H
