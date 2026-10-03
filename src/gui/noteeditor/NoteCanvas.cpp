/*
 * NoteCanvas.cpp - the note editor's note area: painting
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

#include "NoteCanvas.h"

#include <algorithm>

#include <QPainter>
#include <QPaintEvent>

#include "Accessibility.h"
#include "DesignTokens.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

constexpr int kResizeHandle = 5;

//! The index of the first note at or after @a tick in a position-sorted list.
int firstAtOrAfter(const NoteVector& notes, int tick)
{
	const auto it = std::lower_bound(notes.begin(), notes.end(), tick,
		[](const Note* note, int t) { return note->pos().getTicks() < t; });
	return static_cast<int>(it - notes.begin());
}

} // namespace

NoteCanvas::NoteCanvas(NoteEditor* editor)
	: QWidget(editor)
	, m_editor(editor)
	, m_state(editor->state())
{
	setObjectName(QStringLiteral("noteCanvas"));
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent, true);
	a11y::announce(this, tr("Notes"),
		tr("The clip's notes. Keys 1 to 5 pick draw, select, slice, erase and audition; the arrow "
			"keys move the selection, Delete removes it."));
}

void NoteCanvas::clipChanged()
{
	m_longest = 0;
	if (m_state.clip == nullptr) { return; }
	for (const Note* note : m_state.clip->notes())
	{
		m_longest = std::max(m_longest, static_cast<int>(note->length().getTicks()));
	}
}

void NoteCanvas::setPlayhead(int tick)
{
	if (tick == m_playhead) { return; }
	m_playhead = tick;
	update();
}

NoteRange NoteCanvas::visibleRange() const
{
	if (m_state.clip == nullptr) { return {0, 0}; }
	const NoteVector& notes = m_state.clip->notes();
	const int first = static_cast<int>(m_state.grid.firstVisibleTick()) - m_longest;
	const int last = static_cast<int>(m_state.grid.lastVisibleTick()) + 1;
	return {firstAtOrAfter(notes, std::max(0, first)), firstAtOrAfter(notes, last)};
}

QRect NoteCanvas::rectFor(int key, int start, int end) const
{
	if (m_state.grid.rowOfKey(key) < 0) { return {}; }
	const double x0 = m_state.grid.xOfTick(start);
	const double x1 = m_state.grid.xOfTick(end);
	const int w = std::max(3, static_cast<int>(x1 - x0));
	return QRect(static_cast<int>(x0), m_state.grid.yOfKey(key) + 1, w, m_state.grid.rowHeight() - 1);
}

QRect NoteCanvas::noteRect(const Note& note) const
{
	return rectFor(note.key(), note.pos().getTicks(), note.endPos().getTicks());
}

Note* NoteCanvas::noteAt(const QPoint& pos) const
{
	if (m_state.clip == nullptr) { return nullptr; }
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = visibleRange();
	for (int i = last - 1; i >= first; --i)
	{
		if (noteRect(*notes[static_cast<std::size_t>(i)]).contains(pos)) { return notes[static_cast<std::size_t>(i)]; }
	}
	return nullptr;
}

bool NoteCanvas::onResizeHandle(const Note& note, const QPoint& pos) const
{
	const QRect r = noteRect(note);
	return r.width() > 2 * kResizeHandle && r.contains(pos) && pos.x() >= r.right() - kResizeHandle;
}

void NoteCanvas::resizeEvent(QResizeEvent*)
{
	m_state.grid.setViewport(width(), height());
}

void NoteCanvas::paintEvent(QPaintEvent* event)
{
	QPainter p(this);
	const QRect area = event->rect();
	paintRows(p, area);
	paintGrid(p, area);
	if (m_state.clip != nullptr && !m_state.audioBehind.isEmpty())
	{
		m_audioBehind.paint(p, m_state.grid, *m_state.clip, NoteBackgroundAudio::resolve(m_state.audioBehind, *m_state.clip), area);
	}
	paintClipBounds(p);
	if (m_state.ghosts) { paintGhosts(p); }
	paintNotes(p);
	paintDragPreview(p);
	paintOverlay(p);
}

void NoteCanvas::paintRows(QPainter& p, const QRect& area) const
{
	const tokens::Palette& pal = tokens::palette();
	const NoteGrid& g = m_state.grid;
	const int rowH = g.rowHeight();
	const int firstRow = std::max(0, (area.top() + g.scrollY()) / rowH);
	const int lastRow = std::min(g.rowCount() - 1, (area.bottom() + g.scrollY()) / rowH);
	p.fillRect(area, pal.base);
	for (int row = firstRow; row <= lastRow; ++row)
	{
		const int key = g.rows()[static_cast<std::size_t>(row)];
		QColor fill = NoteGrid::isBlackKey(key) ? pal.keyBlackRow : pal.keyWhiteRow;
		if (m_state.highlightScale && g.inScale(key)) { fill = pal.scaleRow; }
		const int y = row * rowH - g.scrollY();
		p.fillRect(QRect(area.left(), y, area.width(), rowH - 1), fill);
		if (key % 12 == 0) { p.fillRect(QRect(area.left(), y + rowH - 1, area.width(), 1), pal.gridBeat); }
	}
}

void NoteCanvas::paintGrid(QPainter& p, const QRect& area) const
{
	const tokens::Palette& pal = tokens::palette();
	const NoteGrid& g = m_state.grid;
	const int step = g.visibleGridTicks(m_state.gridTicks);
	const int beat = DefaultTicksPerBar / 4;
	const int first = (static_cast<int>(g.tickAtX(area.left())) / step) * step;
	const int last = static_cast<int>(g.tickAtX(area.right() + 1));
	for (int tick = first; tick <= last; tick += step)
	{
		const QColor colour = tick % DefaultTicksPerBar == 0 ? pal.gridBar
			: tick % beat == 0 ? pal.gridBeat : pal.gridStep;
		p.fillRect(QRect(static_cast<int>(g.xOfTick(tick)), area.top(), 1, area.height()), colour);
	}
}

void NoteCanvas::paintClipBounds(QPainter& p) const
{
	if (m_state.clip == nullptr) { return; }
	const tokens::Palette& pal = tokens::palette();
	// Beyond the clip's end the notes do not play: dim that area and mark the end in the accent.
	const int end = static_cast<int>(m_state.grid.xOfTick(m_state.clip->length().getTicks()));
	if (end < width())
	{
		QColor shade = pal.base;
		shade.setAlpha(150);
		p.fillRect(QRect(end, 0, width() - end, height()), shade);
	}
	p.fillRect(QRect(end - 1, 0, 2, height()), pal.accent);
}

void NoteCanvas::paintGhosts(QPainter& p) const
{
	if (m_state.clip == nullptr || m_state.clip->getTrack() == nullptr) { return; }
	QColor ghost = tokens::palette().textMuted;
	ghost.setAlpha(70);
	p.setPen(QPen(ghost, 1));
	p.setBrush(Qt::NoBrush);
	const int own = m_state.clip->startPosition().getTicks();
	const double firstTick = m_state.grid.firstVisibleTick();
	const double lastTick = m_state.grid.lastVisibleTick();
	for (const Clip* other : m_state.clip->getTrack()->getClips())
	{
		const auto* midi = dynamic_cast<const MidiClip*>(other);
		if (midi == nullptr || midi == m_state.clip) { continue; }
		const int offset = midi->startPosition().getTicks() - own;
		const bool onScreen = offset <= lastTick && offset + midi->length().getTicks() >= firstTick;
		if (onScreen) { paintGhostClip(p, *midi, offset); }
	}
}

void NoteCanvas::paintGhostClip(QPainter& p, const MidiClip& clip, int offset) const
{
	for (const Note* note : clip.notes())
	{
		const int start = note->pos().getTicks() + offset;
		const QRect r = rectFor(note->key(), start, start + note->length().getTicks());
		if (!r.isEmpty() && r.right() >= 0 && r.left() <= width()) { p.drawRect(r.adjusted(0, 0, -1, -1)); }
	}
}

void NoteCanvas::paintNotes(QPainter& p) const
{
	if (m_state.clip == nullptr) { return; }
	p.setRenderHint(QPainter::Antialiasing, false);
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = visibleRange();
	const bool moving = m_drag == Drag::Move || m_drag == Drag::Resize;
	for (int i = first; i < last; ++i)
	{
		const Note& note = *notes[static_cast<std::size_t>(i)];
		const QRect r = noteRect(note);
		if (r.isEmpty() || r.right() < 0) { continue; }
		// While a selection is being dragged its notes are drawn where they will land
		// (paintDragPreview) and only outlined here, where they were.
		if (moving && note.selected())
		{
			p.setPen(QPen(tokens::palette().textMuted, 1, Qt::DashLine));
			p.setBrush(Qt::NoBrush);
			p.drawRect(r.adjusted(0, 0, -1, -1));
			continue;
		}
		paintNote(p, note, r);
	}
}

void NoteCanvas::paintNote(QPainter& p, const Note& note, const QRect& r) const
{
	const tokens::Palette& pal = tokens::palette();
	QColor fill = noteFill(note);
	// A note that may not play (chance below 100 %) is drawn fainter in proportion.
	fill.setAlphaF(note.muted() ? 0.35f : 0.45f + 0.55f * note.probability());
	p.fillRect(r, fill);
	// Zoomed far out a note is a sliver a few pixels wide: the edge band, the label and the slide
	// mark cannot be seen there, so they are not drawn (the widest-zoom frame's cost, measured in
	// PianoRollBenchmarkTest, is dominated by these thousands of slivers).
	if (r.width() <= 4)
	{
		if (note.selected()) { p.fillRect(r, pal.text); }
		return;
	}
	p.fillRect(QRect(r.left(), r.top(), 2, r.height()), fill.darker(160));
	if (note.selected())
	{
		p.setPen(QPen(pal.text, 2));
		p.setBrush(Qt::NoBrush);
		p.drawRect(r.adjusted(1, 1, -1, -1));
	}
	if (r.width() > 28 && r.height() >= 11)
	{
		p.setFont(tokens::font(tokens::TextRole::Caption));
		p.setPen(pal.textOnAccent);
		p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, noteLabel(note));
	}
	if (note.slide()) { p.fillRect(QRect(r.right() - 3, r.top(), 3, r.height()), pal.text); }
}

void NoteCanvas::paintOverlay(QPainter& p) const
{
	const tokens::Palette& pal = tokens::palette();
	if (m_drag == Drag::Band)
	{
		QColor fill = pal.accent;
		fill.setAlpha(40);
		p.setPen(QPen(pal.accent, 1));
		p.setBrush(fill);
		p.drawRect(QRect(m_pressPos, m_currentPos).normalized());
	}
	if (m_playhead >= 0)
	{
		const int x = static_cast<int>(m_state.grid.xOfTick(m_playhead));
		p.fillRect(QRect(x, 0, 1, height()), pal.playhead);
	}
	if (m_state.clip == nullptr)
	{
		p.setPen(pal.textMuted);
		p.setFont(tokens::font(tokens::TextRole::Body));
		p.drawText(rect(), Qt::AlignCenter, tr("Open a clip: double-click an instrument clip in the arranger."));
	}
}

} // namespace lmms::gui
