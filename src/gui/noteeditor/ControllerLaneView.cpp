/*
 * ControllerLaneView.cpp - the note editor's controller lane: a clip's CC, pitch-bend or aftertouch curve
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

#include "ControllerLaneView.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>

#include "Accessibility.h"
#include "DesignTokens.h"
#include "MidiClip.h"
#include "NoteEditor.h"

namespace lmms::gui
{

namespace
{

constexpr int kLaneHeight = 96;

} // namespace

ControllerLaneView::ControllerLaneView(NoteEditor* editor)
	: QWidget(editor)
	, m_editor(editor)
	, m_state(editor->state())
{
	setObjectName(QStringLiteral("noteControllerLane"));
	a11y::announce(this, tr("Controller lane"), tr("The clip's curve for the chosen controller; drag to draw it, right-drag or Alt-drag to erase."));
	setToolTip(tr("Drag to draw the controller's curve; right-drag or Alt-drag to erase it"));
}

QSize ControllerLaneView::sizeHint() const { return {200, kLaneHeight}; }

const ControllerLane* ControllerLaneView::lane() const
{
	ControllerKind kind = ControllerKind::ControlChange;
	int controller = 0;
	if (m_state.clip == nullptr || !ControllerLane::fromId(m_state.controllerLane, &kind, &controller)) { return nullptr; }
	return m_state.clip->controllers().lane(kind, controller);
}

int ControllerLaneView::maximum() const
{
	return m_state.controllerLane == QStringLiteral("pitchbend") ? 16383 : 127;
}

int ControllerLaneView::valueAtY(int y) const
{
	// yOfValue's inverse: the top row is the maximum, the bottom row 0, so both ends can be drawn.
	const int bottom = std::max(1, height() - 1);
	const double t = 1.0 - std::clamp(y, 0, bottom) / static_cast<double>(bottom);
	return std::clamp(static_cast<int>(std::lround(t * maximum())), 0, maximum());
}

int ControllerLaneView::yOfValue(int value) const
{
	return static_cast<int>((1.0 - static_cast<double>(value) / maximum()) * (height() - 1));
}

int ControllerLaneView::snappedTick(int x) const
{
	return std::max(0, NoteGrid::snapDown(static_cast<int>(m_state.grid.tickAtX(x)), m_state.gridTicks));
}

void ControllerLaneView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.panel);
	p.fillRect(QRect(0, 0, width(), 1), pal.base);
	if (maximum() > 127) { p.fillRect(QRect(0, yOfValue(8192), width(), 1), pal.gridBeat); }  // pitch bend's centre
	if (const ControllerLane* shown = lane()) { paintCurve(p, *shown); }
	if (m_drawing && m_firstTick >= 0) { paintStroke(p); }
	if (m_state.clip == nullptr || m_state.controllerLane.isEmpty()) { return; }
	p.setPen(pal.textMuted);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignRight, m_state.controllerLane);
}

void ControllerLaneView::paintCurve(QPainter& p, const ControllerLane& shown) const
{
	// The curve, sampled every few pixels across the view; the points as dots.
	if (shown.points.empty()) { return; }
	const tokens::Palette& pal = tokens::palette();
	QPolygon curve;
	for (int x = 0; x <= width(); x += 3) { curve << QPoint(x, yOfValue(shown.valueAt(static_cast<tick_t>(m_state.grid.tickAtX(x))))); }
	p.setPen(QPen(pal.accent, 2));
	p.drawPolyline(curve);
	p.setPen(Qt::NoPen);
	p.setBrush(pal.accent);
	for (const ControllerPoint& point : shown.points)
	{
		p.drawEllipse(QPointF(m_state.grid.xOfTick(point.tick), yOfValue(point.value)), 3.0, 3.0);
	}
}

void ControllerLaneView::paintStroke(QPainter& p) const
{
	// The span the stroke replaces (red when erasing) and its points.
	const tokens::Palette& pal = tokens::palette();
	const int a = static_cast<int>(m_state.grid.xOfTick(std::min(m_firstTick, m_lastTick)));
	const int b = static_cast<int>(m_state.grid.xOfTick(std::max(m_firstTick, m_lastTick) + std::max(1, m_state.gridTicks)));
	QColor band = m_erasing ? pal.record : pal.accent;
	band.setAlpha(40);
	p.fillRect(QRect(a, 0, b - a, height()), band);
	p.setPen(Qt::NoPen);
	p.setBrush(pal.text);
	for (const auto& [tick, value] : m_drawn) { p.drawEllipse(QPointF(m_state.grid.xOfTick(tick), yOfValue(value)), 2.5, 2.5); }
}

void ControllerLaneView::mousePressEvent(QMouseEvent* event)
{
	if (m_state.clip == nullptr || m_state.controllerLane.isEmpty()) { return; }
	m_drawing = true;
	m_erasing = event->button() == Qt::RightButton || event->modifiers().testFlag(Qt::AltModifier);
	m_drawn.clear();
	m_firstTick = m_lastTick = snappedTick(event->pos().x());
	drawTo(event->pos());
}

void ControllerLaneView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_drawing) { drawTo(event->pos()); }
}

void ControllerLaneView::mouseReleaseEvent(QMouseEvent*)
{
	if (!m_drawing) { return; }
	m_drawing = false;
	commit();
	update();
}

void ControllerLaneView::drawTo(const QPoint& pos)
{
	// Every grid step between the last position and this one takes a value on the line between
	// them, so a fast stroke leaves no gap.
	const int tick = snappedTick(pos.x());
	const int value = valueAtY(pos.y());
	const int step = std::max(1, m_state.gridTicks);
	const auto previous = m_drawn.find(m_lastTick);
	const int from = previous != m_drawn.end() ? previous->second : value;
	const int span = tick - m_lastTick;
	for (int t = std::min(m_lastTick, tick); t <= std::max(m_lastTick, tick); t += step)
	{
		const double at = span == 0 ? 1.0 : static_cast<double>(t - m_lastTick) / span;
		m_drawn[t] = static_cast<int>(std::lround(from + (value - from) * at));
	}
	m_lastTick = tick;
	update();
}

void ControllerLaneView::commit()
{
	if (m_drawn.empty() || m_state.clip == nullptr) { return; }
	const int low = m_drawn.begin()->first;
	const int high = m_drawn.rbegin()->first;
	// The lane's points outside the stroke stay; inside it the stroke replaces them (erasing: nothing does).
	QJsonArray points;
	if (const ControllerLane* existing = lane())
	{
		for (const ControllerPoint& point : existing->points)
		{
			if (point.tick < low || point.tick > high) { points.append(QJsonObject{{QStringLiteral("tick"), point.tick}, {QStringLiteral("value"), point.value}}); }
		}
	}
	if (!m_erasing)
	{
		for (const auto& [tick, value] : m_drawn) { points.append(QJsonObject{{QStringLiteral("tick"), tick}, {QStringLiteral("value"), value}}); }
	}
	m_drawn.clear();
	m_editor->run(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), m_editor->clipId()},
		{QStringLiteral("lane"), m_state.controllerLane}, {QStringLiteral("points"), points}});
}

} // namespace lmms::gui
