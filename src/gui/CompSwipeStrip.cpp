/*
 * CompSwipeStrip.cpp - comp by swiping: the take lanes as rows, a drag along one comps that range
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
#include "CompSwipeStrip.h"

#include <algorithm>

#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "TimePos.h"

namespace lmms::gui
{

CompSwipeStrip::CompSwipeStrip(const QString& trackId, QWidget* parent) :
	QWidget(parent),
	m_trackId(trackId)
{
	setObjectName(QStringLiteral("compSwipeStrip"));
	setAccessibleName(tr("Comp lanes - drag along a lane to comp that range, double-click to cycle the take"));
	setMouseTracking(false);
	setMinimumHeight(RowHeight);
}

void CompSwipeStrip::setState(const QJsonObject& state)
{
	m_lanes.clear();
	for (const QJsonValue& lane : state.value(QStringLiteral("lanes")).toArray()) { m_lanes.push_back(lane.toObject().value(QStringLiteral("lane")).toInt()); }
	m_segments.clear();
	const QJsonObject composite = state.value(QStringLiteral("composite")).toObject();
	for (const QJsonValue& value : composite.value(QStringLiteral("segments")).toArray())
	{
		const QJsonObject s = value.toObject();
		m_segments.push_back({s.value(QStringLiteral("begin")).toInt(), s.value(QStringLiteral("end")).toInt(), s.value(QStringLiteral("lane")).toInt()});
	}
	// The span drawn: the comp's, else four bars from the start (a fresh lane set has no comp yet).
	m_begin = composite.value(QStringLiteral("begin")).toInt();
	m_end = composite.value(QStringLiteral("end")).toInt();
	if (m_end <= m_begin) { m_begin = 0; m_end = 4 * TimePos::ticksPerBar(); }
	setMinimumHeight(RowHeight * std::max<int>(1, static_cast<int>(m_lanes.size())));
	updateGeometry();
	update();
}

int CompSwipeStrip::tickAt(int x) const
{
	const double fraction = std::clamp(static_cast<double>(x) / std::max(1, width()), 0.0, 1.0);
	return m_begin + static_cast<int>(fraction * (m_end - m_begin));
}

int CompSwipeStrip::laneAt(int y) const
{
	const int row = y / RowHeight;
	return row >= 0 && row < static_cast<int>(m_lanes.size()) ? m_lanes[static_cast<std::size_t>(row)] : -1;
}

QSize CompSwipeStrip::sizeHint() const
{
	return {320, RowHeight * std::max<int>(1, static_cast<int>(m_lanes.size()))};
}

void CompSwipeStrip::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), palette().base());
	const auto xOf = [this](int tick) { return static_cast<int>(static_cast<double>(tick - m_begin) / std::max(1, m_end - m_begin) * width()); };
	for (std::size_t row = 0; row < m_lanes.size(); ++row)
	{
		const int y = static_cast<int>(row) * RowHeight;
		p.setPen(palette().mid().color());
		p.drawLine(0, y + RowHeight - 1, width(), y + RowHeight - 1);
		for (const Segment& segment : m_segments)
		{
			if (segment.lane != m_lanes[row]) { continue; }
			p.fillRect(QRect(xOf(segment.begin), y + 2, xOf(segment.end) - xOf(segment.begin), RowHeight - 4), palette().highlight());
		}
		if (m_dragLane == m_lanes[row])
		{
			QColor drag = palette().highlight().color();
			drag.setAlpha(110);
			p.fillRect(QRect(xOf(std::min(m_dragFrom, m_dragTo)), y, std::abs(xOf(m_dragTo) - xOf(m_dragFrom)), RowHeight), drag);
		}
		p.setPen(palette().text().color());
		p.drawText(QRect(4, y, width() - 8, RowHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Lane %1").arg(m_lanes[row]));
	}
}

void CompSwipeStrip::mousePressEvent(QMouseEvent* event)
{
	m_dragLane = event->button() == Qt::LeftButton ? laneAt(event->pos().y()) : -1;
	m_dragFrom = m_dragTo = tickAt(event->pos().x());
	update();
}

void CompSwipeStrip::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragLane < 0) { return; }
	m_dragTo = tickAt(event->pos().x());
	update();
}

void CompSwipeStrip::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_dragLane < 0) { return; }
	m_dragTo = tickAt(event->pos().x());
	const int begin = std::min(m_dragFrom, m_dragTo);
	const int end = std::max(m_dragFrom, m_dragTo);
	const int lane = m_dragLane;
	m_dragLane = -1;
	update();
	if (end <= begin) { return; }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.select"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("begin"), begin}, {QStringLiteral("end"), end}, {QStringLiteral("lane"), lane}});
	emit ran(result.ok ? tr("Comped %1-%2 on lane %3").arg(begin).arg(end).arg(lane) : result.errorMessage);
}

void CompSwipeStrip::mouseDoubleClickEvent(QMouseEvent* event)
{
	m_dragLane = -1;
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.cycle"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("tick"), tickAt(event->pos().x())}});
	emit ran(result.ok ? tr("Lane %1 there now").arg(result.result.value(QStringLiteral("lane")).toInt()) : result.errorMessage);
}

} // namespace lmms::gui
