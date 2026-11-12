/*
 * MixerModeBarLayout.h - the mixer mode bar's rows, which wrap when the panel is narrow
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
#ifndef LMMS_GUI_MIXER_MODE_BAR_LAYOUT_H
#define LMMS_GUI_MIXER_MODE_BAR_LAYOUT_H

#include <QLayout>
#include <QList>

namespace lmms::gui
{

/*! gui-1030: the bar's controls in rows that wrap. One row holds them all in a wide panel, its last
 *  group (pushRight()) at the right end as before; a narrow panel gets as many rows as it needs, each
 *  control at its full size, so none is clipped or crushed. A unit (a caption and its choices) wraps
 *  as a whole. The bar asks for the height its rows need through heightForWidth. */
class MixerModeBarLayout : public QLayout
{
public:
	explicit MixerModeBarLayout(QWidget* parent) : QLayout(parent) {}
	~MixerModeBarLayout() override;
	MixerModeBarLayout(const MixerModeBarLayout&) = delete;
	MixerModeBarLayout& operator=(const MixerModeBarLayout&) = delete;

	//! @a widgets stay together on one row.
	void addUnit(const QList<QWidget*>& widgets);
	//! The units added from now on sit at the right end while every unit fits on one row.
	void pushRight() { m_pushFrom = static_cast<int>(m_units.size()); }

	void addItem(QLayoutItem* item) override { m_items.append(item); m_units.append(1); invalidate(); }
	int count() const override { return static_cast<int>(m_items.size()); }
	QLayoutItem* itemAt(int index) const override { return m_items.value(index); }
	QLayoutItem* takeAt(int index) override;
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
	QSize sizeHint() const override;
	QSize minimumSize() const override;
	void setGeometry(const QRect& rect) override
	{
		QLayout::setGeometry(rect);
		arrange(rect, true);
	}

private:
	struct Row { int firstUnit = 0; int width = 0; int height = 0; };
	static QSize itemSize(const QLayoutItem* item) { return item->sizeHint().expandedTo(item->minimumSize()); }
	int firstItemOf(int unit) const;
	QSize unitSize(int unit) const;  //!< empty for a unit whose widgets are all hidden
	QList<Row> rows(int width) const;
	int arrange(const QRect& rect, bool apply) const;
	int placeUnit(int unit, int x, int y, int rowHeight) const;

	QList<QLayoutItem*> m_items;
	QList<int> m_units;  //!< the number of items in each unit, in order
	int m_pushFrom = -1;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_MODE_BAR_LAYOUT_H
