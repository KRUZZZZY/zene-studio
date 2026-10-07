/*
 * NoteLanePaint.cpp - the expression lane's picture: scale, stems and readouts
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

#include <QPainter>

#include "DesignTokens.h"
#include "LaneScale.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteEditorParts.h"

namespace lmms::gui
{

namespace
{

//! A stem's colour: the velocity ramp only where the lane shows a velocity, one neutral hue elsewhere - the
//! value is the stem's height, and the colour never the only thing that says it (0.5.0 theme 11).
QColor stemColour(LaneHead head, bool byVelocity, int value)
{
	const tokens::Palette& pal = tokens::palette();
	switch (head)
	{
	case LaneHead::Selected: return pal.text;
	case LaneHead::Hidden: return pal.textMuted;
	case LaneHead::Plain: break;
	}
	return byVelocity ? tokens::velocityColor(value) : pal.accent;
}

int stemWidth(LaneHead head)
{
	switch (head)
	{
	case LaneHead::Selected: return 3;
	case LaneHead::Hidden: return 1;
	case LaneHead::Plain: break;
	}
	return 2;
}

} // namespace

void NoteLaneView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.panel);
	p.fillRect(QRect(0, 0, width(), 1), pal.base);
	const LaneScale scale = lanescale::forNoteLane(m_state.lane);
	const lanescale::YOf yOf = [this](int value) { return yOfValue(value); };
	lanescale::paintMarkLines(p, rect(), scale, yOf);
	const int baseY = scale.low < 0 ? yOfValue(0) : height() - 1;
	p.fillRect(QRect(0, baseY, width(), 1), pal.gridBeat);
	if (m_state.clip != nullptr) { paintStems(p, baseY); }
	lanescale::paintPlayhead(p, rect(), m_editor->playheadX());
	const QString status = selectionReadout();
	lanescale::paintLabels(p, rect(), scale, yOf, status);
	if (m_hover.x() >= 0) { lanescale::paintReadout(p, rect(), m_hover, pointerReadout(scale)); }
}

void NoteLaneView::paintStems(QPainter& p, int baseY) const
{
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = m_editor->canvas()->visibleRange();
	// A head on each stem only when the stems are far enough apart to tell them apart; zoomed
	// far out thousands of antialiased heads cost more than the whole note area.
	const bool heads = (last - first) * 6 < std::max(1, width());
	const bool byVelocity = m_state.lane == NoteLane::Velocity || m_state.lane == NoteLane::Release;
	p.setRenderHint(QPainter::Antialiasing, heads);
	for (int i = first; i < last; ++i)
	{
		const Note& note = *notes[static_cast<std::size_t>(i)];
		const int x = static_cast<int>(m_state.grid.xOfTick(note.pos().getTicks()));
		const int value = shownValue(note);
		const int y = yOfValue(value);
		// A note whose key is folded away in the note area keeps its stem, hollow and thin, so it is
		// plain that the lane edits a note the rows above do not show.
		const LaneHead head = note.selected() ? LaneHead::Selected
			: m_state.grid.rowOfKey(note.key()) >= 0 ? LaneHead::Plain : LaneHead::Hidden;
		const QColor colour = stemColour(head, byVelocity, value);
		p.fillRect(QRect(x, std::min(y, baseY), stemWidth(head), std::abs(baseY - y) + 1), colour);
		if (heads) { lanescale::paintHead(p, QPointF(x + 1, y), colour, head); }
	}
}

void NoteLaneView::leaveEvent(QEvent* event)
{
	m_hover = QPoint(-1, -1);
	update();
	QWidget::leaveEvent(event);
}

const Note* NoteLaneView::stemAt(int x) const
{
	if (m_state.clip == nullptr) { return nullptr; }
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = m_editor->canvas()->visibleRange();
	const Note* nearest = nullptr;
	int best = lanescale::stemTolerance() + 1;
	for (int i = first; i < last; ++i)
	{
		const Note* note = notes[static_cast<std::size_t>(i)];
		const int distance = std::abs(static_cast<int>(m_state.grid.xOfTick(note->pos().getTicks())) - x);
		if (distance < best)
		{
			best = distance;
			nearest = note;
		}
	}
	return nearest;
}

QString NoteLaneView::pointerReadout(const LaneScale& scale) const
{
	const QString level = scale.text(valueAtY(m_hover.y()));
	if (m_drawing) { return tr("Drawing: %1").arg(level); }
	const Note* note = stemAt(m_hover.x());
	if (note == nullptr) { return tr("Pointer at %1").arg(level); }
	const QString named = m_editor->keyLabel(note->key());
	return tr("%1: %2 (pointer at %3)").arg(named.isEmpty() ? NoteGrid::keyName(note->key()) : named,
		scale.text(shownValue(*note)), level);
}

QString NoteLaneView::selectionReadout() const
{
	if (m_state.clip == nullptr) { return {}; }
	int count = 0;
	int lowest = 0;
	int highest = 0;
	// The whole clip's selection, not only what is on screen: the header says what a draw would edit.
	for (const Note* note : m_state.clip->notes())
	{
		if (!note->selected()) { continue; }
		const int value = shownValue(*note);
		lowest = count == 0 ? value : std::min(lowest, value);
		highest = count == 0 ? value : std::max(highest, value);
		++count;
	}
	return lanescale::selectionText(lanescale::forNoteLane(m_state.lane), count, lowest, highest);
}

QString NoteLaneView::readout() const
{
	return m_hover.x() >= 0 ? pointerReadout(lanescale::forNoteLane(m_state.lane)) : selectionReadout();
}

} // namespace lmms::gui
