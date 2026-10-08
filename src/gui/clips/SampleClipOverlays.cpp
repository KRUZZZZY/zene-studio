/*
 * SampleClipOverlays.cpp - what a sample clip draws over its waveform: gain envelope, fades and their handles, transients
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

#include "SampleClipOverlays.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPolygon>

namespace lmms::gui
{

namespace
{

constexpr int HandleSize = 7;
constexpr int HandleReach = 6;  //!< how far from a handle a press still takes it
//! A fade of length zero shows its handle this far inside the clip, clear of the edge's resize grip
//! (review L05-L09-L08-1 #6: on the edge it took the corner press the inherited resize and move need).
constexpr int ZeroFadeInset = 10;

int xOfTick(const ClipOverlayFrame& frame, int tick) { return frame.xOrigin + static_cast<int>(tick * frame.pixelsPerTick); }

int fadeInX(const ClipEdits& edits, const ClipOverlayFrame& frame) { return xOfTick(frame, edits.fadeInOffsetTicks + edits.fadeInTicks); }

int fadeOutX(const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame)
{
	return xOfTick(frame, clipTicks - edits.fadeOutOffsetTicks - edits.fadeOutTicks);
}

//! Where a fade's handle is drawn and taken: the end of the fade, or inset from the edge when it is zero.
int handleX(FadeHandle handle, const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame)
{
	if (handle == FadeHandle::In) { return edits.fadeInTicks == 0 ? frame.xOrigin + ZeroFadeInset : fadeInX(edits, frame); }
	return edits.fadeOutTicks == 0 ? xOfTick(frame, clipTicks) - ZeroFadeInset : fadeOutX(edits, clipTicks, frame);
}

//! One fade's curve from @a x0 to @a x1: rising (fade in) or falling (fade out), shaded above.
void paintRamp(QPainter& p, FadeShape shape, int x0, int x1, bool rising, const ClipOverlayFrame& frame, const QColor& colour)
{
	if (x1 <= x0) { return; }
	QPolygon curve;
	const int bottom = frame.top + frame.height;
	for (int x = x0; x <= x1; x += 2)
	{
		const double t = static_cast<double>(x - x0) / (x1 - x0);
		const float gain = fadeShapeGain(shape, rising ? t : 1.0 - t);
		curve << QPoint(x, bottom - static_cast<int>(gain * frame.height));
	}
	QPolygon shade = curve;
	shade << QPoint(x1, frame.top) << QPoint(x0, frame.top);
	QColor fill = colour;
	fill.setAlpha(50);
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawPolygon(shade);
	p.setPen(QPen(colour, 1.5));
	p.setBrush(Qt::NoBrush);
	p.drawPolyline(curve);
}

} // namespace

void paintGainEnvelope(QPainter& p, const ClipEdits& edits, const ClipOverlayFrame& frame, const QColor& colour)
{
	if (!edits.hasEnvelope()) { return; }
	const auto yOf = [&frame](float db) { return frame.top + static_cast<int>((1.f - (db + 60.f) / 84.f) * frame.height); };
	QPolygon line;
	line << QPoint(frame.xOrigin, yOf(edits.envelope[0].gainDb));
	for (int i = 0; i < edits.envelopeCount; ++i) { line << QPoint(xOfTick(frame, edits.envelope[i].tick), yOf(edits.envelope[i].gainDb)); }
	line << QPoint(frame.xOrigin + frame.width, yOf(edits.envelope[edits.envelopeCount - 1].gainDb));
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(colour, 1.5));
	p.drawPolyline(line);
	p.setBrush(colour);
	for (int i = 0; i < edits.envelopeCount; ++i)
	{
		p.drawEllipse(QPoint(xOfTick(frame, edits.envelope[i].tick), yOf(edits.envelope[i].gainDb)), 2, 2);
	}
	p.setBrush(Qt::NoBrush);
}

void paintFades(QPainter& p, const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame, const QColor& colour)
{
	p.setRenderHint(QPainter::Antialiasing, true);
	paintRamp(p, edits.fadeInShape, xOfTick(frame, edits.fadeInOffsetTicks), fadeInX(edits, frame), true, frame, colour);
	paintRamp(p, edits.fadeOutShape, fadeOutX(edits, clipTicks, frame), xOfTick(frame, clipTicks - edits.fadeOutOffsetTicks), false, frame, colour);
	// The handles: a small square at the top edge where each fade ends.
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	for (const FadeHandle handle : {FadeHandle::In, FadeHandle::Out})
	{
		p.drawRect(handleX(handle, edits, clipTicks, frame) - HandleSize / 2, frame.top, HandleSize, HandleSize);
	}
	p.setBrush(Qt::NoBrush);
}

FadeHandle fadeHandleAt(const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame, const QPoint& pos)
{
	if (pos.y() > frame.top + HandleSize + HandleReach) { return FadeHandle::None; }
	const int in = std::abs(pos.x() - handleX(FadeHandle::In, edits, clipTicks, frame));
	const int out = std::abs(pos.x() - handleX(FadeHandle::Out, edits, clipTicks, frame));
	const FadeHandle nearer = in <= out ? FadeHandle::In : FadeHandle::Out;
	// A zero-length fade's handle takes only a press on its own square, not the margin around it.
	const bool zero = (nearer == FadeHandle::In ? edits.fadeInTicks : edits.fadeOutTicks) == 0;
	const int reach = zero ? HandleSize / 2 : HandleReach;
	if (std::min(in, out) > reach || (zero && pos.y() > frame.top + HandleSize)) { return FadeHandle::None; }
	return nearer;
}

int fadeTicksAt(FadeHandle handle, int x, int clipTicks, const ClipOverlayFrame& frame)
{
	const int tick = static_cast<int>(std::lround((x - frame.xOrigin) / std::max(0.001f, frame.pixelsPerTick)));
	const int ticks = handle == FadeHandle::In ? tick : clipTicks - tick;
	return std::clamp(ticks, 0, clipTicks);
}

void paintTransients(QPainter& p, const std::vector<int>& ticks, const ClipOverlayFrame& frame, const QColor& colour)
{
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setPen(QPen(colour, 1));
	for (const int tick : ticks)
	{
		const int x = xOfTick(frame, tick);
		p.drawLine(x, frame.top, x, frame.top + frame.height / 4);
	}
}

void paintSnapPoint(QPainter& p, const ClipEdits& edits, const ClipOverlayFrame& frame, const QColor& colour)
{
	if (edits.snapTicks <= 0) { return; }
	const int x = xOfTick(frame, edits.snapTicks);
	const int bottom = frame.top + frame.height;
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setPen(QPen(colour, 1, Qt::DotLine));
	p.drawLine(x, frame.top, x, bottom);
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	p.drawPolygon(QPolygon({QPoint(x, bottom - 7), QPoint(x + 6, bottom - 4), QPoint(x, bottom - 1)}));
}

} // namespace lmms::gui
