/*
 * ArrangerRulerTempo.cpp - the ruler lanes' tempo lane: the tempo map's curve, its values and its tooltip
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

#include <algorithm>
#include <cmath>
#include <utility>

#include <QPainter>
#include <QPolygonF>

#include "ArrangerRulerLanes.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MusicalTime.h"
#include "Song.h"
#include "SongEditor.h"
#include "TempoMap.h"
#include "TimeLineWidget.h"

namespace lmms::gui
{

namespace
{

constexpr int ValueGap = tokens::space::xs;  //!< pixels kept between a tempo value and the next one
constexpr int ChangeHit = 6;                 //!< pixels either side of a tempo change its tooltip still names it

//! The lowest and highest tempo of @a map, the song's @a global tempo among them.
std::pair<int, int> tempoRange(const TempoMap& map, int global)
{
	int low = global;
	int high = global;
	for (const TempoMapEvent& event : map.all())
	{
		if (event.hasTempo) { low = std::min(low, event.tempo); high = std::max(high, event.tempo); }
	}
	return {low, high};
}

bool mapShown(const TempoMap& map)
{
	return map.active() && !map.empty();
}

} // namespace

void ArrangerRulerLanes::paintTempo(QPainter& p, const QRect& lane) const
{
	const tokens::Palette& pal = tokens::palette();
	const TempoMap& map = Engine::getSong()->tempoMap().map();
	const int global = Engine::getSong()->getTempo();
	p.setPen(pal.textMuted);
	if (!mapShown(map))
	{
		p.drawText(lane.adjusted(contentLeft() + ValueGap, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("%1 BPM (no tempo map)").arg(global));
		return;
	}
	const auto [low, high] = tempoRange(map, global);
	// ED-050's curve - holds flat, ramps slope - over the lane, with each tempo change's value beside it.
	const double perTick = (xOf(TimePos::ticksPerBar()) - xOf(0)) / static_cast<double>(TimePos::ticksPerBar());
	if (perTick <= 0.0) { return; }
	const int band = lane.height() - 4;
	QPolygonF curve;
	for (int x = contentLeft(); x <= width(); x += 2)
	{
		const double tempo = map.exactTempoAtTick(static_cast<tick_t>(std::max(0.0, (x - xOf(0)) / perTick)), global);
		curve << QPointF(x, lane.bottom() - 2 - (high > low ? (tempo - low) / (high - low) : 0.5) * band);
	}
	p.setPen(QPen(pal.play, 1.25));
	p.drawPolyline(curve);
	paintTempoValues(p, lane, map);
}

void ArrangerRulerLanes::paintTempoValues(QPainter& p, const QRect& lane, const TempoMap& map) const
{
	// Each tempo change's value beside it, unless the last one's label is still in the way (the tooltip names it).
	p.setPen(tokens::palette().text);
	int freeFrom = contentLeft();
	for (const TempoMapEvent& event : map.all())
	{
		const int x = xOf(event.tick);
		if (!event.hasTempo || x < freeFrom || x >= width()) { continue; }
		const QString value = QString::number(event.tempo);
		p.drawText(QPoint(x + 2, lane.top() + p.fontMetrics().ascent()), value);
		freeFrom = x + 2 + p.fontMetrics().horizontalAdvance(value) + ValueGap;
	}
}

QString ArrangerRulerLanes::tempoTip(const QPoint& pos) const
{
	if (m_editor.isNull() || pos.x() < contentLeft() || !laneRect(Lane::Tempo).contains(pos)) { return QString(); }
	const TempoMap& map = Engine::getSong()->tempoMap().map();
	const int global = Engine::getSong()->getTempo();
	if (!mapShown(map)) { return tr("%1 BPM (no tempo map)").arg(global); }
	// The change under the pointer by name - its value may be one a crowded lane left out - else the tempo there.
	for (const TempoMapEvent& event : map.all())
	{
		if (event.hasTempo && std::abs(xOf(event.tick) - pos.x()) <= ChangeHit)
		{
			return tr("Tempo change to %1 BPM at %2").arg(event.tempo).arg(musicaltime::counterText(event.tick));
		}
	}
	const tick_t at = std::max<tick_t>(0, tickAt(pos.x()));
	return tr("%1 BPM at %2").arg(QString::number(map.exactTempoAtTick(at, global), 'f', 1), musicaltime::counterText(at));
}

} // namespace lmms::gui
