/*
 * ShortcutsPage.cpp - M3 item 4: every keyboard shortcut, from the menus that own them
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

#include "ShortcutsPage.h"

#include <algorithm>

#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lmms::gui
{

QList<PaletteEntry> shortcutEntries(QMenuBar* bar)
{
	QList<PaletteEntry> entries;
	for (const PaletteEntry& entry : paletteMenuEntries(bar))
	{
		if (!entry.shortcut.isEmpty()) { entries << entry; }
	}
	std::stable_sort(entries.begin(), entries.end(), [](const PaletteEntry& a, const PaletteEntry& b) {
		return a.path != b.path ? a.path < b.path : a.label < b.label;
	});
	return entries;
}


ShortcutsPage::ShortcutsPage(QMenuBar* bar, QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Keyboard Shortcuts"));
	setObjectName(QStringLiteral("ShortcutsPage"));
	auto* layout = new QVBoxLayout(this);
	m_filter = new QLineEdit(this);
	m_filter->setPlaceholderText(tr("Filter by action, menu or key"));
	m_filter->setAccessibleName(tr("Filter shortcuts"));
	layout->addWidget(m_filter);

	const QList<PaletteEntry> entries = shortcutEntries(bar);
	m_table = new QTableWidget(static_cast<int>(entries.size()), 4, this);
	m_table->setAccessibleName(tr("Keyboard shortcuts"));
	m_table->setHorizontalHeaderLabels({tr("Shortcut"), tr("Action"), tr("Menu"), tr("Command")});
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->verticalHeader()->hide();
	for (int row = 0; row < static_cast<int>(entries.size()); ++row)
	{
		const PaletteEntry& entry = entries[row];
		QString label = entry.label;
		label.remove(QLatin1Char('&'));
		const QStringList cells{entry.shortcut, label, entry.path, entry.command};
		for (int column = 0; column < cells.size(); ++column)
		{
			m_table->setItem(row, column, new QTableWidgetItem(cells[column]));
		}
	}
	m_table->resizeColumnsToContents();
	layout->addWidget(m_table);
	connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsPage::setFilter);
	resize(640, 480);
}


int ShortcutsPage::rowCount() const
{
	return m_table->rowCount();
}


int ShortcutsPage::visibleRowCount() const
{
	int visible = 0;
	for (int row = 0; row < m_table->rowCount(); ++row) { visible += m_table->isRowHidden(row) ? 0 : 1; }
	return visible;
}


void ShortcutsPage::setFilter(const QString& text)
{
	const QString needle = text.trimmed();
	for (int row = 0; row < m_table->rowCount(); ++row)
	{
		bool match = needle.isEmpty();
		for (int column = 0; column < m_table->columnCount() && !match; ++column)
		{
			match = m_table->item(row, column)->text().contains(needle, Qt::CaseInsensitive);
		}
		m_table->setRowHidden(row, !match);
	}
}

} // namespace lmms::gui
