/*
 * LaneScale.cpp - what a note or controller lane's values mean: its scale, units and readouts
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

#include "LaneScale.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainter>

#include "DesignTokens.h"
#include "MidiClipControllers.h"

namespace lmms::gui
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("LaneScale", text); }

QString plain(int value) { return QString::number(value); }
QString percent(int value) { return tr("%1 %").arg(value); }
QString signedNumber(int value) { return value > 0 ? QStringLiteral("+%1").arg(value) : QString::number(value); }
QString cents(int value) { return tr("%1 ct").arg(signedNumber(value)); }
QString repeats(int value) { return value == 1 ? tr("1 repeat") : tr("%1 repeats").arg(value); }

//! Left of centre "L 12", right "R 12", the centre "C": a direction in words, not only in a bar's side.
QString pan(int value)
{
	if (value == 0) { return tr("C"); }
	return value < 0 ? tr("L %1").arg(-value) : tr("R %1").arg(value);
}

std::function<QString(int)> noteText(NoteLane lane)
{
	switch (lane)
	{
	case NoteLane::Velocity:
	case NoteLane::Release:
	case NoteLane::Chance:
	case NoteLane::Spread: return percent;
	case NoteLane::Pan: return pan;
	case NoteLane::Pitch: return cents;
	case NoteLane::Repeats: return repeats;
	case NoteLane::Timbre:
	case NoteLane::Pressure: break;
	}
	return plain;
}

//! The controllers the lane chooser names; any other is "CC <n>".
QString ccTitle(int controller)
{
	const struct { int cc; const char* name; } named[] = {{1, "Mod wheel"}, {2, "Breath"}, {7, "Volume"}, {10, "Pan"},
		{11, "Expression"}, {64, "Sustain"}};
	for (const auto& entry : named)
	{
		if (entry.cc == controller) { return tr("%1 (CC %2)").arg(tr(entry.name)).arg(controller); }
	}
	return tr("CC %1").arg(controller);
}

//! A controller value: the raw 0..127, with what it means where the controller gives it a meaning.
std::function<QString(int)> ccText(int controller)
{
	if (controller == 64) { return [](int v) { return v >= 64 ? tr("On (%1)").arg(v) : tr("Off (%1)").arg(v); }; }
	if (controller == 10) { return [](int v) { return tr("%1 (%2)").arg(v).arg(pan(v - 64)); }; }
	return plain;
}

QString pitchBend(int value) { return value == 8192 ? tr("0 (centre)") : signedNumber(value - 8192); }

//! @a text in @a box on the panel colour, a little see-through, so a curve under it never hides it.
void backedText(QPainter& p, const QRect& box, const QString& text, const QColor& colour, int flags)
{
	QColor back = tokens::palette().panel;
	back.setAlpha(225);
	p.fillRect(box, back);
	p.setPen(colour);
	p.drawText(box.adjusted(tokens::space::xs, 0, -tokens::space::xs, 0), flags, text);
}

//! Paints each mark's label at the right edge; returns the widest label's width.
int paintMarkLabels(QPainter& p, const QRect& area, const LaneScale& scale, const lanescale::YOf& yOf)
{
	const QFontMetrics fm(p.font());
	const int h = fm.height();
	const int tick = tokens::space::xs;
	int widest = 0;
	for (const int mark : scale.marks)
	{
		const QString text = scale.text(mark);
		const int w = fm.horizontalAdvance(text) + 2 * tokens::space::xs;
		widest = std::max(widest, w);
		const int y = std::clamp(yOf(mark) - h / 2, area.top(), std::max(area.top(), area.bottom() + 1 - h));
		p.fillRect(QRect(area.right() + 1 - tick, yOf(mark), tick, 1), tokens::palette().textMuted);
		backedText(p, QRect(area.right() + 1 - tick - w, y, w, h), text, tokens::palette().textMuted,
			Qt::AlignVCenter | Qt::AlignRight);
	}
	return widest + tick;
}

} // namespace

QString LaneScale::rangeText() const
{
	return tr("%1 to %2").arg(text(low), text(high));
}

namespace lanescale
{

LaneScale forNoteLane(NoteLane lane)
{
	const NoteLaneSpec spec = notelanes::spec(lane);
	LaneScale scale;
	scale.title = spec.title;
	scale.low = spec.minimum < 0 ? -spec.drawRange / 2 : spec.minimum;
	scale.high = scale.low + spec.drawRange;
	scale.marks = {scale.low, spec.minimum < 0 ? 0 : (scale.low + scale.high + 1) / 2, scale.high};
	scale.text = noteText(lane);
	return scale;
}

LaneScale forController(const QString& id)
{
	LaneScale scale;
	scale.title = id;
	scale.marks = {0, 64, 127};
	scale.text = plain;
	ControllerKind kind = ControllerKind::ControlChange;
	int controller = 0;
	if (!ControllerLane::fromId(id, &kind, &controller)) { return scale; }
	switch (kind)
	{
	case ControllerKind::PitchBend:
		scale.title = tr("Pitch bend");
		scale.high = 16383;
		scale.marks = {0, 8192, 16383};
		scale.text = pitchBend;
		break;
	case ControllerKind::ChannelPressure: scale.title = tr("Aftertouch"); break;
	case ControllerKind::ProgramChange: scale.title = tr("Program change"); break;
	case ControllerKind::ControlChange:
		scale.title = ccTitle(controller);
		scale.text = ccText(controller);
		break;
	}
	return scale;
}

int laneHeight()
{
	return static_cast<int>(std::lround(4 * tokens::size::target * tokens::targetFactor()));
}

int stemTolerance()
{
	return static_cast<int>(std::lround(tokens::space::xs * tokens::targetFactor()));
}

void paintMarkLines(QPainter& p, const QRect& area, const LaneScale& scale, const YOf& yOf)
{
	for (const int mark : scale.marks) { p.fillRect(QRect(area.left(), yOf(mark), area.width(), 1), tokens::palette().gridStep); }
}

void paintLabels(QPainter& p, const QRect& area, const LaneScale& scale, const YOf& yOf, const QString& status)
{
	p.save();
	p.setFont(tokens::font(tokens::TextRole::Caption));
	const int marks = paintMarkLabels(p, area, scale, yOf);
	QString header = tr("%1, %2").arg(scale.title, scale.rangeText());
	if (!status.isEmpty()) { header = tr("%1 - %2").arg(header, status); }
	const QFontMetrics fm(p.font());
	const int room = area.width() - marks - 2 * tokens::space::xs;
	if (room > 2 * tokens::space::xl)
	{
		const QString shown = fm.elidedText(header, Qt::ElideRight, room - 2 * tokens::space::xs);
		const QRect box(area.left(), area.top() + 1, fm.horizontalAdvance(shown) + 2 * tokens::space::xs, fm.height());
		backedText(p, box, shown, tokens::palette().text, Qt::AlignVCenter | Qt::AlignLeft);
	}
	p.restore();
}

void paintPlayhead(QPainter& p, const QRect& area, int x)
{
	if (x < 0) { return; }
	p.fillRect(QRect(area.left() + x, area.top(), 1, area.height()), tokens::palette().playhead);
}

void paintReadout(QPainter& p, const QRect& area, const QPoint& anchor, const QString& text)
{
	if (text.isEmpty()) { return; }
	const tokens::Palette& pal = tokens::palette();
	p.save();
	p.setFont(tokens::font(tokens::TextRole::Caption));
	const QFontMetrics fm(p.font());
	const QSize size(fm.horizontalAdvance(text) + 2 * tokens::space::sm, fm.height() + 2 * tokens::space::xxs);
	// Above and right of the pointer; to its left near the right edge, below it near the top.
	QRect box(anchor + QPoint(tokens::space::md, -size.height() - tokens::space::xs), size);
	if (box.right() > area.right()) { box.moveRight(anchor.x() - tokens::space::md); }
	if (box.top() < area.top()) { box.moveTop(anchor.y() + tokens::space::md); }
	box.moveLeft(std::max(area.left(), box.left()));
	box.moveBottom(std::min(area.bottom(), box.bottom()));
	p.fillRect(box, pal.raised);
	tokens::strokeCrisp(p, QRectF(box), pal.textMuted);
	p.setPen(pal.text);
	p.drawText(box, Qt::AlignCenter, text);
	p.restore();
}

void paintHead(QPainter& p, const QPointF& centre, const QColor& colour, LaneHead head)
{
	const tokens::Palette& pal = tokens::palette();
	p.save();
	switch (head)
	{
	case LaneHead::Plain:
		p.setPen(QPen(pal.base, 1));
		p.setBrush(colour);
		p.drawEllipse(centre, 3.5, 3.5);
		break;
	case LaneHead::Selected:
		p.setPen(QPen(pal.base, 2));
		p.setBrush(colour);
		p.drawRect(QRectF(centre.x() - 4, centre.y() - 4, 8, 8));
		break;
	case LaneHead::Hidden:
		p.setPen(QPen(colour, 1.5));
		p.setBrush(pal.panel);
		p.drawEllipse(centre, 3.5, 3.5);
		break;
	}
	p.restore();
}

QString selectionText(const LaneScale& scale, int count, int lowest, int highest)
{
	if (count <= 0) { return {}; }
	if (count == 1) { return tr("Selected: %1").arg(scale.text(lowest)); }
	if (lowest == highest) { return tr("%1 selected: %2").arg(count).arg(scale.text(lowest)); }
	return tr("%1 selected: %2 to %3").arg(count).arg(scale.text(lowest), scale.text(highest));
}

} // namespace lanescale

} // namespace lmms::gui
