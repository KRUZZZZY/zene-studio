/*
 * AutomationEditorFades.cpp - a sample clip's fades drawn in the automation editor (ED-180)
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

/*! ED-180: clip.fade_curve could read a sample clip's fades, but nothing drew them. When a sample clip is shown behind
 *  the automation (an audio clip's "Set as ghost in automation editor": the ghost sample), its gain curve - fades, clip gain
 *  and gain envelope, clipGainCurveAt, the same one definition the command reports - is drawn over the waveform:
 *  a line from silence at the band's bottom to unity (or the curve's peak, when the clip is boosted) at its top, and
 *  what the fades take away shaded between that line and unity. Paint only; nothing is written. */

#include <algorithm>
#include <vector>

#include <QPainter>
#include <QPolygonF>

#include "AutomationEditor.h"
#include "ClipEdits.h"
#include "SampleClip.h"

namespace lmms::gui
{

namespace
{

constexpr int PixelsPerPoint = 2;  //!< the curve's resolution on screen
constexpr int MaxPoints = 1024;    //!< clip.fade_curve's own bound

} // namespace

void AutomationEditor::drawGhostFades(QPainter& p, int top, int height)
{
	if (m_ghostSample == nullptr || !m_renderSample || height <= 0) { return; }
	const ClipEdits& edits = m_ghostSample->clipEdits();
	const tick_t length = std::max<tick_t>(1, m_ghostSample->length().getTicks());
	const int left = xCoordOfTick(0);
	const int right = xCoordOfTick(length);
	if (right <= left) { return; }
	const int points = std::clamp((right - left) / PixelsPerPoint, 2, MaxPoints);
	std::vector<float> gains(static_cast<std::size_t>(points));
	float peak = 1.f;
	for (int i = 0; i < points; ++i)
	{
		gains[static_cast<std::size_t>(i)] = clipGainCurveAt(edits, length, static_cast<double>(length) * i / (points - 1));
		peak = std::max(peak, gains[static_cast<std::size_t>(i)]);
	}
	const auto yOf = [top, height, peak](float gain) { return top + height - static_cast<double>(gain / peak) * height; };
	QPolygonF curve;
	for (int i = 0; i < points; ++i)
	{
		curve << QPointF(left + static_cast<double>(right - left) * i / (points - 1), yOf(gains[static_cast<std::size_t>(i)]));
	}
	QPolygonF takenAway = curve;
	takenAway << QPointF(right, yOf(1.f)) << QPointF(left, yOf(1.f));
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	p.setBrush(m_outOfBoundsShade);
	p.drawPolygon(takenAway);
	QColor line = m_ghostSampleColor;
	line.setAlpha(255);
	p.setPen(QPen(line, 2));
	p.setBrush(Qt::NoBrush);
	p.drawPolyline(curve);
	p.restore();
}

} // namespace lmms::gui
