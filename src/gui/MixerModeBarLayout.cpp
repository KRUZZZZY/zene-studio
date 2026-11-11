/*
 * MixerModeBarLayout.cpp - the mixer mode bar's rows, which wrap when the panel is narrow
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
#include "MixerModeBarLayout.h"

#include <algorithm>
#include <limits>

#include <QWidget>

namespace lmms::gui
{

MixerModeBarLayout::~MixerModeBarLayout()
{
	qDeleteAll(m_items);
}

void MixerModeBarLayout::addUnit(const QList<QWidget*>& widgets)
{
	for (QWidget* widget : widgets)
	{
		// Native buttons and combo boxes may use a fixed vertical size policy. Let
		// them fill their shared row without fixing a pixel height across DPI changes.
		QSizePolicy policy = widget->sizePolicy();
		policy.setVerticalPolicy(QSizePolicy::Minimum);
		widget->setSizePolicy(policy);
		addChildWidget(widget);
		m_items.append(new QWidgetItem(widget));
	}
	m_units.append(static_cast<int>(widgets.size()));
	invalidate();
}

QLayoutItem* MixerModeBarLayout::takeAt(int index)
{
	if (index < 0 || index >= m_items.size()) { return nullptr; }
	int first = 0;
	for (int unit = 0; unit < m_units.size(); first += m_units.at(unit++))
	{
		if (index >= first + m_units.at(unit)) { continue; }
		if (--m_units[unit] == 0)
		{
			m_units.removeAt(unit);
			if (unit < m_pushFrom) { --m_pushFrom; }
		}
		break;
	}
	invalidate();
	return m_items.takeAt(index);
}

int MixerModeBarLayout::firstItemOf(int unit) const
{
	int first = 0;
	for (int i = 0; i < unit; ++i) { first += m_units.at(i); }
	return first;
}

QSize MixerModeBarLayout::unitSize(int unit) const
{
	const int first = firstItemOf(unit);
	QSize size(0, 0);  // not QSize(), whose -1 width would come off every unit
	for (int i = first; i < first + m_units.at(unit); ++i)
	{
		if (m_items.at(i)->isEmpty()) { continue; }
		const QSize item = itemSize(m_items.at(i));
		size = QSize(size.width() + (size.width() > 0 ? spacing() : 0) + item.width(), std::max(size.height(), item.height()));
	}
	return size;
}

QList<MixerModeBarLayout::Row> MixerModeBarLayout::rows(int width) const
{
	QList<Row> rows{Row{}};
	for (int unit = 0; unit < m_units.size(); ++unit)
	{
		const QSize size = unitSize(unit);
		if (size.width() <= 0) { continue; }
		if (rows.last().width > 0 && rows.last().width + spacing() + size.width() > width) { rows.append(Row{unit, 0, 0}); }
		Row& row = rows.last();
		row.width += (row.width > 0 ? spacing() : 0) + size.width();
		row.height = std::max(row.height, size.height());
	}
	return rows;
}

int MixerModeBarLayout::placeUnit(int unit, int x, int y, int rowHeight) const
{
	const int first = firstItemOf(unit);
	for (int i = first; i < first + m_units.at(unit); ++i)
	{
		if (m_items.at(i)->isEmpty()) { continue; }
		const QSize size = itemSize(m_items.at(i));
		// Give every control the full row height: their top edges stay aligned even
		// when the platform gives labels and buttons slightly different size hints.
		m_items.at(i)->setGeometry(QRect(x, y, size.width(), rowHeight));
		x += size.width() + spacing();
	}
	return x;
}

int MixerModeBarLayout::arrange(const QRect& rect, bool apply) const
{
	const QMargins margins = contentsMargins();
	const QRect area = rect.marginsRemoved(margins);
	const QList<Row> laid = rows(area.width());
	int y = area.y();
	for (int r = 0; r < laid.size(); ++r)
	{
		const int end = r + 1 < laid.size() ? laid.at(r + 1).firstUnit : static_cast<int>(m_units.size());
		int x = area.x();
		for (int unit = laid.at(r).firstUnit; apply && unit < end; ++unit)
		{
			// one row: the right group keeps the right end, where the old stretch put it
			if (unit == m_pushFrom && laid.size() == 1) { x = std::max(x, area.right() + 1 - (laid.at(r).width - (x - area.x()))); }
			if (unitSize(unit).width() > 0) { x = placeUnit(unit, x, y, laid.at(r).height); }
		}
		y += laid.at(r).height + (r + 1 < laid.size() ? spacing() : 0);
	}
	return y - area.y() + margins.top() + margins.bottom();
}

QSize MixerModeBarLayout::sizeHint() const
{
	const Row row = rows(std::numeric_limits<int>::max() / 2).constFirst();
	const QMargins m = contentsMargins();
	return {row.width + m.left() + m.right(), row.height + m.top() + m.bottom()};
}

QSize MixerModeBarLayout::minimumSize() const
{
	QSize widest(0, 0);
	for (int unit = 0; unit < m_units.size(); ++unit) { widest = widest.expandedTo(unitSize(unit)); }
	const QMargins m = contentsMargins();
	return {widest.width() + m.left() + m.right(), widest.height() + m.top() + m.bottom()};
}

} // namespace lmms::gui
