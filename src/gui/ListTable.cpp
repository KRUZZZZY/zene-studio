/*
 * ListTable.cpp - the shared list: find, sort and the keyboard's row actions
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

#include "ListTable.h"

#include <algorithm>

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QRegularExpression>

#include "AccessibleWidgets.h"

namespace lmms::gui
{

namespace
{

//! The number @a text starts with ("-3 dB", "120", "1.5 s") in @a value; false when it starts with none.
bool leadingNumber(const QString& text, double* value)
{
	static const QRegularExpression number(QStringLiteral("^\\s*([-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?)"));
	const QRegularExpressionMatch found = number.match(text);
	if (!found.hasMatch()) { return false; }
	*value = found.captured(1).toDouble();
	return true;
}

//! Whether @a a sorts before @a b: numbers by value, anything else as the locale orders text.
bool sortsBefore(const QString& a, const QString& b)
{
	double x = 0;
	double y = 0;
	if (leadingNumber(a, &x) && leadingNumber(b, &y) && x != y) { return x < y; }
	return QString::localeAwareCompare(a, b) < 0;
}

bool rowContains(const ListRow& row, const QString& needle)
{
	if (row.hiddenWords.contains(needle, Qt::CaseInsensitive)) { return true; }
	return std::any_of(row.cells.begin(), row.cells.end(),
		[&needle](const QString& cell) { return cell.contains(needle, Qt::CaseInsensitive); });
}

bool isEnter(const QKeyEvent* event)
{
	const bool plain = (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
	return plain && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter);
}

} // namespace

ListTable::ListTable(QWidget* parent) :
	QTableWidget(parent)
{
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setTabKeyNavigation(false);  // Tab leaves the list for the find field and the buttons, as everywhere else
	horizontalHeader()->setStretchLastSection(true);
	horizontalHeader()->setSectionsClickable(true);
	setAccessibleDescription(tr("Enter runs the row's main action; the Menu key or Shift+F10 opens its actions; "
		"a column title sorts by that column"));
	connect(horizontalHeader(), &QHeaderView::sectionClicked, this, &ListTable::cycleSort);
	connect(this, &QTableWidget::cellDoubleClicked, this, [this] {
		if (currentDataRow() >= 0) { emit rowActivated(currentDataRow()); }
	});
}

void ListTable::setRows(const QStringList& headers, const QList<ListRow>& rows, int keep)
{
	if (headers != m_headers && m_sortColumn >= headers.size()) { m_sortColumn = -1; }
	m_headers = headers;
	m_rows = rows;
	redraw(keep);
}

void ListTable::redraw(int keep)
{
	m_order.clear();
	for (int i = 0; i < m_rows.size(); ++i) { m_order.append(i); }
	if (m_sortColumn >= 0)
	{
		const auto text = [this](int row) { return m_rows.at(row).cells.value(m_sortColumn); };
		std::stable_sort(m_order.begin(), m_order.end(), [&](int a, int b) {
			return m_ascending ? sortsBefore(text(a), text(b)) : sortsBefore(text(b), text(a));
		});
	}
	clear();
	setColumnCount(static_cast<int>(m_headers.size()));
	setHorizontalHeaderLabels(m_headers);
	setRowCount(static_cast<int>(m_order.size()));
	for (int r = 0; r < m_order.size(); ++r)
	{
		const ListRow& row = m_rows.at(m_order.at(r));
		for (int c = 0; c < m_headers.size(); ++c)
		{
			auto* cell = new QTableWidgetItem(row.cells.value(c));
			cell->setToolTip(row.tips.value(c));
			setItem(r, c, cell);
		}
	}
	horizontalHeader()->setSortIndicatorShown(m_sortColumn >= 0);
	if (m_sortColumn >= 0) { horizontalHeader()->setSortIndicator(m_sortColumn, m_ascending ? Qt::AscendingOrder : Qt::DescendingOrder); }
	applyFilter();
	selectDataRow(keep);
}

int ListTable::currentDataRow() const
{
	const int shown = currentRow();
	if (shown < 0 || shown >= m_order.size() || isRowHidden(shown)) { return -1; }
	return m_order.at(shown);
}

void ListTable::selectDataRow(int index)
{
	const int shown = static_cast<int>(m_order.indexOf(index));
	if (index >= 0 && shown >= 0 && !isRowHidden(shown))
	{
		selectRow(shown);
		return;
	}
	setCurrentItem(nullptr);  // gone or hidden: nothing current, so no action is aimed at another row
	clearSelection();
}

void ListTable::setFilter(const QString& text)
{
	m_filter = text.trimmed();
	applyFilter();
}

void ListTable::applyFilter()
{
	for (int r = 0; r < m_order.size(); ++r)
	{
		setRowHidden(r, !m_filter.isEmpty() && !rowContains(m_rows.at(m_order.at(r)), m_filter));
	}
	if (currentRow() >= 0 && isRowHidden(currentRow()))
	{
		setCurrentItem(nullptr);
		clearSelection();
	}
}

int ListTable::visibleRowCount() const
{
	int visible = 0;
	for (int r = 0; r < rowCount(); ++r) { visible += isRowHidden(r) ? 0 : 1; }
	return visible;
}

void ListTable::sortRows(int column, bool ascending)
{
	const int keep = currentDataRow();
	m_sortColumn = column >= 0 && column < m_headers.size() ? column : -1;
	m_ascending = ascending;
	redraw(keep);
	a11y::announceStatus(this, m_sortColumn < 0 ? tr("In the list's own order")
		: tr("Sorted by %1, %2").arg(m_headers.at(m_sortColumn), m_ascending ? tr("ascending") : tr("descending")));
}

void ListTable::cycleSort(int column)
{
	if (column != m_sortColumn) { sortRows(column, true); }
	else if (m_ascending) { sortRows(column, false); }
	else { sortRows(-1); }
}

void ListTable::addSortMenu(QMenu* menu)
{
	QMenu* sort = menu->addMenu(tr("Sort By"));
	for (int c = 0; c < m_headers.size(); ++c)
	{
		QAction* by = sort->addAction(m_headers.at(c));
		by->setCheckable(true);
		by->setChecked(c == m_sortColumn);
		connect(by, &QAction::triggered, this, [this, c] { sortRows(c, c == m_sortColumn ? !m_ascending : true); });
	}
	sort->addSeparator();
	QAction* own = sort->addAction(tr("List Order"));
	own->setCheckable(true);
	own->setChecked(m_sortColumn < 0);
	connect(own, &QAction::triggered, this, [this] { sortRows(-1); });
}

void ListTable::keyPressEvent(QKeyEvent* event)
{
	m_keyMenuShown = false;
	if (isEnter(event) && currentDataRow() >= 0)
	{
		emit rowActivated(currentDataRow());
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_F10 && event->modifiers() == Qt::ShiftModifier)
	{
		m_keyMenuShown = true;
		requestMenu(QPoint());
		event->accept();
		return;
	}
	QTableWidget::keyPressEvent(event);
}

void ListTable::contextMenuEvent(QContextMenuEvent* event)
{
	event->accept();
	if (event->reason() == QContextMenuEvent::Keyboard)
	{
		// The Menu key (Qt turns it into this event); one Shift+F10 already handled is not asked twice.
		const bool handled = m_keyMenuShown;
		m_keyMenuShown = false;
		if (!handled) { requestMenu(QPoint()); }
		return;
	}
	const int shown = rowAt(event->pos().y());
	if (shown >= 0) { selectRow(shown); }
	requestMenu(event->globalPos());
}

void ListTable::requestMenu(const QPoint& globalPos)
{
	QPoint at = globalPos;
	if (at.isNull())
	{
		// From the keyboard: under the current row, else the list's middle.
		const QRect cell = currentItem() != nullptr ? visualItemRect(currentItem()) : viewport()->rect();
		at = viewport()->mapToGlobal(currentItem() != nullptr ? cell.bottomLeft() : cell.center());
	}
	emit rowMenuRequested(currentDataRow(), at);
}

} // namespace lmms::gui
