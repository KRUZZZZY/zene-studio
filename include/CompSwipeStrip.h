/*
 * CompSwipeStrip.h - comp by swiping: the take lanes as rows, a drag along one comps that range
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
#ifndef LMMS_GUI_COMP_SWIPE_STRIP_H
#define LMMS_GUI_COMP_SWIPE_STRIP_H

#include <vector>

#include <QJsonObject>
#include <QWidget>

#include "lmms_export.h"

namespace lmms::gui
{

/*! 0.5.0 L03 (ED-165): comp by swiping. The track's take lanes are drawn as rows over the comp's
 *  span, each comp segment lit on the lane it plays; a drag along a lane runs comp.select for the
 *  range dragged on that lane, and a double-click runs comp.cycle at that tick (ED-168) - the
 *  registry's commands, so each swipe is one undo step. setState() takes comp.get_state's answer. */
class LMMS_EXPORT CompSwipeStrip : public QWidget
{
	Q_OBJECT
public:
	CompSwipeStrip(const QString& trackId, QWidget* parent);

	void setState(const QJsonObject& state);
	//! The tick at @a x and the lane at @a y (or -1), for the gesture and the test.
	int tickAt(int x) const;
	int laneAt(int y) const;
	QSize sizeHint() const override;

signals:
	//! A swipe or a cycle ran; the panel refreshes and shows @a status.
	void ran(const QString& status);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
	struct Segment
	{
		int begin = 0;
		int end = 0;
		int lane = 0;
	};
	static constexpr int RowHeight = 22;

	QString m_trackId;
	std::vector<int> m_lanes;
	std::vector<Segment> m_segments;
	int m_begin = 0;
	int m_end = 0;
	int m_dragLane = -1;
	int m_dragFrom = 0;
	int m_dragTo = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_COMP_SWIPE_STRIP_H
