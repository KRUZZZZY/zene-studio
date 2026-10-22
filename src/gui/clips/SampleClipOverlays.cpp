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
//! Q050-125: the dB range the envelope and the gain line share, and the smallest/largest dB a point or
//! handle can be set to - the same -60..+24 `clip.gain_envelope_set` and `clip.set_gain` accept.
constexpr float GainScaleBottomDb = -60.f;
constexpr float GainScaleSpanDb = 84.f;
//! Q050-125: the clip-gain handle is held this far inside the clip's left edge, clear of the resize grip.
constexpr int GainHandleInset = 10;
//! Q050-125: a click this close to an envelope point takes it, and this close to the gain handle takes that.
constexpr int PointReach = 5;
constexpr int GainHandleReach = 6;

int xOfTick(const ClipOverlayFrame& frame, int tick) { return frame.xOrigin + static_cast<int>(tick * frame.pixelsPerTick); }

//! Q050-125: the one dB-to-pixel mapping the envelope line and the gain handle share.
int yOfDb(const ClipOverlayFrame& frame, float db)
{
	return frame.top + static_cast<int>((1.f - (db - GainScaleBottomDb) / GainScaleSpanDb) * frame.height);
}

int fadeInX(const ClipEdits& edits, const ClipOverlayFrame& frame) { return xOfTick(frame, edits.fadeInOffsetTicks + edits.fadeInTicks); }

int fadeOutX(const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame)
{
	return xOfTick(frame, clipTicks - edits.fadeOutOffsetTicks - edits.fadeOutTicks);
}

//! Where a fade's handle is drawn and taken: the end of the fade, or inset from the edge when it is zero.
int handleX(FadeHandle handle, const ClipEdits& edits, int clipTicks, const ClipOverlayFrame& frame)
{
	if (handle == FadeHandle::In) { return edits.fadeInTicks == 0 ? xOfTick(frame, edits.fadeInOffsetTicks) + ZeroFadeInset : fadeInX(edits, frame); }
	return edits.fadeOutTicks == 0 ? xOfTick(frame, clipTicks - edits.fadeOutOffsetTicks) - ZeroFadeInset : fadeOutX(edits, clipTicks, frame);
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
	const auto yOf = [&frame](float db) { return yOfDb(frame, db); };
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

int clipGainHandleY(const ClipEdits& edits, const ClipOverlayFrame& frame)
{
	// The handle's centre, held at least half a handle inside the clip so a boosted clip's handle is
	// grabbable even though its gain's own pixel would be above the clip's top edge. It is also held
	// BELOW the fade handles' strip: the zero-length fade-in handle sits at this same x, so a gain
	// handle drawn in that strip could never be grabbed where it is painted (the fade drag takes the
	// press first). On a clip too short for both bands, staying inside the clip wins.
	const int half = HandleSize / 2;
	const int fadeBand = frame.top + HandleSize + HandleReach + 1;
	const int lowest = frame.top + frame.height - half;
	return std::clamp(yOfDb(frame, gainLinearToDb(edits.gain)), std::min(fadeBand, lowest), lowest);
}

void paintClipGain(QPainter& p, const ClipEdits& edits, const ClipOverlayFrame& frame, const QColor& colour)
{
	// The dashed level line across the waveform, at the clip's own gain. The envelope and the fades are
	// drawn apart from it: this line says what the CLIP GAIN alone is, which is the value its handle edits.
	// The level's own pixel, held inside the clip: what the handle means, even when the handle itself
	// is held away from it (a boosted clip, or the fade handles' strip below).
	const int lineY = std::clamp(yOfDb(frame, gainLinearToDb(edits.gain)), frame.top, frame.top + frame.height);
	const int handleY = clipGainHandleY(edits, frame);
	const int x = frame.xOrigin + GainHandleInset;
	QColor line = colour;
	line.setAlpha(160);
	p.setRenderHint(QPainter::Antialiasing, false);
	p.setPen(QPen(line, 1, Qt::DotLine));
	p.drawLine(frame.xOrigin, lineY, frame.xOrigin + frame.width, lineY);
	// The handle: a small square at the clip's left edge, the thing a drag takes. When it is held away
	// from its own line (a boosted clip, or the fade handles' strip) a short tick keeps the two tied
	// together, so the handle still says which level it edits.
	if (handleY != lineY)
	{
		p.setPen(QPen(line, 1));
		p.drawLine(x, std::min(lineY, handleY), x, std::max(lineY, handleY));
	}
	p.setPen(QPen(colour, 1));
	p.setBrush(colour);
	p.drawRect(x - HandleSize / 2, handleY - HandleSize / 2, HandleSize, HandleSize);
	p.setBrush(Qt::NoBrush);
}

bool clipGainHandleAt(const ClipEdits& edits, const ClipOverlayFrame& frame, const QPoint& pos)
{
	const int x = frame.xOrigin + GainHandleInset;
	return std::abs(pos.x() - x) <= GainHandleReach
		&& std::abs(pos.y() - clipGainHandleY(edits, frame)) <= GainHandleReach;
}

float gainDbAtY(int y, const ClipOverlayFrame& frame)
{
	if (frame.height <= 0) { return 0.f; }
	const double t = std::clamp(static_cast<double>(y - frame.top) / frame.height, 0.0, 1.0);
	return std::clamp(static_cast<float>(GainScaleBottomDb + (1.0 - t) * GainScaleSpanDb),
		MinGainPointDb, MaxGainPointDb);
}

int gainTickAtX(int x, const ClipOverlayFrame& frame)
{
	if (frame.pixelsPerTick <= 0.f) { return 0; }
	return static_cast<int>(std::lround((x - frame.xOrigin) / frame.pixelsPerTick));
}

int gainPointAt(const ClipEdits& edits, const ClipOverlayFrame& frame, const QPoint& pos)
{
	for (int i = 0; i < edits.envelopeCount; ++i)
	{
		const QPoint at(xOfTick(frame, edits.envelope[i].tick), yOfDb(frame, edits.envelope[i].gainDb));
		if (std::abs(pos.x() - at.x()) <= PointReach && std::abs(pos.y() - at.y()) <= PointReach) { return i; }
	}
	return -1;
}

float clipGainDraggedDb(float startDb, int dy, const ClipOverlayFrame& frame)
{
	// The whole range over the clip's own height, so a one-pixel nudge is the same fraction of the
	// range however tall the clip is; the envelope's dB-per-pixel is the mapping clipGainDraggedDb uses.
	const double perPixel = frame.height > 0 ? GainScaleSpanDb / frame.height : GainScaleSpanDb;
	return std::clamp(static_cast<float>(startDb - dy * perPixel), MinGainPointDb, MaxGainPointDb);
}

namespace
{

//! Whether @a edits can take a point at @a index/`tick`/`db` at all - the three rules
//! `clip.gain_envelope_set` enforces, so a drag previews only what the command would accept.
bool gainPointAllowed(const ClipEdits& edits, int index, int tick, float db, int clipTicks)
{
	if (index < 0 || index > edits.envelopeCount) { return false; }
	if (index == edits.envelopeCount && edits.envelopeCount >= MaxGainPoints) { return false; }
	return tick >= 0 && tick <= clipTicks && db >= MinGainPointDb && db <= MaxGainPointDb;
}

} // namespace

bool withGainPoint(ClipEdits* edits, int index, int tick, float db, int clipTicks, int* writtenIndex)
{
	if (edits == nullptr || !gainPointAllowed(*edits, index, tick, db, clipTicks)) { return false; }

	// Move the point (or add one), then re-sort: `clip.gain_envelope_set` requires tick order and the
	// envelope's evaluation is a linear walk, so a point dragged past its neighbour must take its place.
	if (index == edits->envelopeCount) { edits->envelope[edits->envelopeCount++] = GainPoint{tick, db}; }
	else { edits->envelope[index] = GainPoint{tick, db}; }
	if (writtenIndex != nullptr)
	{
		/*! The point's index after the STABLE sort, derived from the list as it stands now (before the
		 *  sort): every point with a lower tick sorts before ours, and among equal ticks the stable sort
		 *  keeps the original order, so the ones originally before @a index stay before it. A value match
		 *  alone is ambiguous when two points share a tick, which is why the position is derived. */
		int at = 0;
		for (int i = 0; i < edits->envelopeCount; ++i)
		{
			if (i == index) { continue; }
			if (edits->envelope[i].tick < tick || (edits->envelope[i].tick == tick && i < index)) { ++at; }
		}
		*writtenIndex = at;
	}
	std::stable_sort(edits->envelope.begin(), edits->envelope.begin() + edits->envelopeCount,
		[](const GainPoint& a, const GainPoint& b) { return a.tick < b.tick; });
	return true;
}

int gainPointCount(const ClipEdits& edits) { return edits.envelopeCount; }

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

int fadeTicksAt(FadeHandle handle, int x, int clipTicks, const ClipOverlayFrame& frame, int offsetTicks)
{
	const int tick = static_cast<int>(std::lround((x - frame.xOrigin) / std::max(0.001f, frame.pixelsPerTick)));
	const int ticks = handle == FadeHandle::In ? tick - offsetTicks : clipTicks - offsetTicks - tick;
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
