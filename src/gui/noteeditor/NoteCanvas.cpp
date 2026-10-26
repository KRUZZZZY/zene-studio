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
#include <cmath>

#include <QPainter>
#include <QPaintEvent>

#include "Accessibility.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "Song.h"
#include "TempoMap.h"
#include "TouchInput.h"
#include "Track.h"
#include "WcagContrast.h"

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

//! A note label's colour: whichever of the theme's two text colours reads better on the note's
//! fill as painted (at its alpha over a row), since pitch, track and velocity fills can be light
//! or dark whatever the theme.
QColor labelColour(const QColor& fill, const tokens::Palette& pal)
{
	const qreal a = fill.alphaF();
	const QColor& row = pal.keyWhiteRow;
	const QColor shown = QColor::fromRgbF(fill.redF() * a + row.redF() * (1 - a),
		fill.greenF() * a + row.greenF() * (1 - a), fill.blueF() * a + row.blueF() * (1 - a));
	return a11y::contrastRatio(pal.textOnAccent, shown) >= a11y::contrastRatio(pal.text, shown) ? pal.textOnAccent : pal.text;
}

//! 0.5.0 theme 11: the narrowest a note's click target gets - the design system's; in accessible mode (Q050-141)
//! the touch minimum, 44 px.
int minimumTarget()
{
	if (tokens::accessibleMode()) { return tokens::targetSize(); }
	return static_cast<int>(std::lround(tokens::space::md * tokens::targetFactor()));
}

//! A note's edge, opaque in the note's own colour so a faint (unlikely, muted) note still shows against its
//! row: dashed when the note is muted, dotted when it may not play - its state in a shape, not only a shade.
void paintEdge(QPainter& p, const Note& note, const QRect& r, const QColor& solid)
{
	const Qt::PenStyle style = note.muted() ? Qt::DashLine : note.probability() < 1.f ? Qt::DotLine : Qt::SolidLine;
	p.setPen(QPen(note.muted() ? tokens::palette().text : solid, 1, style));
	p.setBrush(Qt::NoBrush);
	p.drawRect(r.adjusted(0, 0, -1, -1));
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
	// 0.5.0 Q050-141: a finger's tap selects the note under it, as a left click does. In accessible mode a
	// row is a 44 px note target plus the 8 px gap to the next row, and vertical zoom cannot go below that.
	touch::installSingleTouch(this);
	if (tokens::accessibleMode())
	{
		m_state.grid.setMinRowHeight(tokens::targetSize() + tokens::targetSpacing(0));
		m_state.grid.setRowHeight(m_state.grid.rowHeight());
	}
	a11y::announce(this, tr("Notes"),
		tr("The clip's notes, edited as in FL Studio's piano roll. P draw, B paint, D delete, T mute, C slice, "
			"E select, Y play, Z zoom to the selection (1 to 7 pick the tools too). A click draws, a drag on a note "
			"moves it or, on its right edge, resizes it; the right button deletes the notes it crosses, and from an "
			"empty row slices. Ctrl selects, Ctrl+Shift adds; Shift locks the pitch and Ctrl the timing while "
			"dragging; Alt turns the snap off. Double-click a note for its properties. Ctrl+C, Ctrl+X, Ctrl+V and "
			"Ctrl+B copy, cut, paste and duplicate; Delete removes the selection; the arrow keys move it. Alt+wheel "
			"over a note changes its velocity (Ctrl+Alt finely, Shift+Alt nudges it). The wheel scrolls; with Ctrl it "
			"zooms, with Shift it scrolls sideways; the middle button drags the view. Without a mouse: Alt+Left and "
			"Alt+Right go to the previous and next note (Shift adds it), Alt+Up and Alt+Down to the chord's note above "
			"and below, Home and End to the first and last; Ctrl+Left and Ctrl+Right shorten and lengthen the selection, "
			"Ctrl+Up and Ctrl+Down change its velocity; Insert adds a note after the cursor, Enter opens its properties, "
			"Ctrl+Shift+L the editable note list."));
	installKeyboardAccess();  // theme 10 (NoteCanvasAccess.cpp)
	repaintOnMeterChange(this);
}

void NoteCanvas::followSongMeter(NoteEditorState& state)
{
	Song* song = Engine::getSong();
	if (song == nullptr || state.clip == nullptr) { return; }
	static const TempoMap noChanges;
	const TempoMapTimeSignature global{song->getTimeSigModel().getNumerator(), song->getTimeSigModel().getDenominator()};
	// A pattern's clip has its own ticks, not the arrangement's: the song's metre, with no map changes.
	const Track* track = state.clip->getTrack();
	const bool arranged = track != nullptr && track->trackContainer() == song;
	state.grid.setMeter(global, arranged ? song->tempoMap().map() : noChanges,
		arranged ? static_cast<int>(state.clip->startPosition().getTicks()) : 0);
}

void NoteCanvas::repaintOnMeterChange(QWidget* widget)
{
	if (Engine::getSong() == nullptr) { return; }
	connect(Engine::getSong(), &Song::timeSignatureChanged, widget, [widget] { widget->update(); });
}

void NoteCanvas::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();  // the focus ring (paintOverlay)
}

void NoteCanvas::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
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

void NoteCanvas::cancelGesture()
{
	m_drag = Drag::None;
	m_anchorId = -1;
	m_auditionKey = -1;
	m_strokeIds.clear();
	m_muteTo = -1;
	m_painted.clear();
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
	const int rowTop = m_state.grid.yOfKey(key);
	if (tokens::accessibleMode())
	{
		// A target-high note centred in its row: the row's remaining height is the gap to the next row's note.
		const int h = std::min(tokens::targetSize(), m_state.grid.rowHeight() - 1);
		return QRect(static_cast<int>(x0), rowTop + (m_state.grid.rowHeight() - h) / 2, w, h);
	}
	return QRect(static_cast<int>(x0), rowTop + 1, w, m_state.grid.rowHeight() - 1);
}

QRect NoteCanvas::touchRect(const Note& note) const
{
	const QRect r = noteRect(note);
	const int grow = minimumTarget() - r.width();
	return grow > 0 && !r.isEmpty() ? r.adjusted(-grow / 2, 0, grow - grow / 2, 0) : r;
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
	// 0.5.0 theme 11 / Q050-141: a note narrower than the minimum target is hit anywhere in its touch target -
	// only where no note is hit exactly, so a short note never takes a tap from its neighbour; where several
	// targets hold the tap, the note whose centre is nearest wins.
	Note* nearest = nullptr;
	int nearestDistance = 0;
	for (int i = last - 1; i >= first; --i)
	{
		Note* note = notes[static_cast<std::size_t>(i)];
		const QRect r = touchRect(*note);
		if (r.isEmpty() || !r.contains(pos)) { continue; }
		const int distance = std::abs(noteRect(*note).center().x() - pos.x());
		if (nearest == nullptr || distance < nearestDistance)
		{
			nearest = note;
			nearestDistance = distance;
		}
	}
	return nearest;
}

bool NoteCanvas::onResizeHandle(const Note& note, const QPoint& pos) const
{
	const QRect r = noteRect(note);
	return r.width() > 2 * kResizeHandle && r.contains(pos) && pos.x() >= r.right() - kResizeHandle;
}

bool NoteCanvas::onStartHandle(const Note& note, const QPoint& pos) const
{
	const QRect r = noteRect(note);
	return r.width() > 2 * kResizeHandle && r.contains(pos) && pos.x() <= r.left() + kResizeHandle;
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
	followSongMeter(m_state);
	const NoteGrid& g = m_state.grid;
	const int step = g.visibleGridTicks(m_state.gridTicks);
	const auto line = [&](int tick, const QColor& colour) {
		p.fillRect(QRect(static_cast<int>(g.xOfTick(tick)), area.top(), 1, area.height()), colour);
	};
	// Bar by bar in the song's metre (0.5.0 W2): the steps restart at each bar, the beat is the metre's.
	// Zoomed out past a bar, every n-th bar's line is drawn.
	for (const NoteGrid::Bar& bar : g.barsBetween(g.tickAtX(area.left()), g.tickAtX(area.right() + 1)))
	{
		if (bar.index % ((step + bar.barTicks - 1) / bar.barTicks) == 0) { line(bar.tick, pal.gridBar); }
		for (int tick = bar.tick + step; tick < bar.tick + bar.length; tick += step)
		{
			line(tick, (tick - bar.tick) % bar.beatTicks == 0 ? pal.gridBeat : pal.gridStep);
		}
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
	const bool moving = m_drag == Drag::Move || m_drag == Drag::Resize || m_drag == Drag::ResizeStart;
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
	const QColor solid = noteFill(note);
	QColor fill = solid;
	// A note that may not play (chance below 100 %) is drawn fainter in proportion - and outlined (paintEdge).
	fill.setAlphaF(note.muted() ? 0.35f : 0.45f + 0.55f * note.probability());
	// Zoomed far out a note is a sliver a few pixels wide: the edge band, the label and the slide
	// mark cannot be seen there, so they are not drawn (the widest-zoom frame's cost, measured in
	// PianoRollBenchmarkTest, is dominated by these thousands of slivers). Keep the probability alpha
	// in the fill so the chance remains visible at this zoom too.
	if (r.width() <= 4)
	{
		QColor sliver = note.selected() ? pal.text : solid;
		sliver.setAlphaF(fill.alphaF());
		p.fillRect(r, sliver);
		return;
	}
	p.fillRect(r, fill);
	paintEdge(p, note, r, solid);
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
		p.setPen(labelColour(fill, pal));
		// A note that may not play says its chance in words as well as in its fainter fill (0.5.0 theme 11).
		const QString label = note.probability() < 1.f
			? tr("%1 %2%").arg(noteLabel(note)).arg(std::lround(note.probability() * 100.f)) : noteLabel(note);
		p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
	}
	if (note.slide()) { p.fillRect(QRect(r.right() - 3, r.top(), 3, r.height()), pal.text); }
}

bool NoteCanvas::stroked(const Note& note) const
{
	return std::binary_search(m_strokeIds.begin(), m_strokeIds.end(), note.id());
}

void NoteCanvas::paintStrokePreview(QPainter& p) const
{
	// A delete stroke marks what it crossed in the record colour, a mute stroke greys it.
	if (m_state.clip == nullptr) { return; }
	const QColor mark = m_drag == Drag::Erase ? tokens::palette().record : tokens::palette().textMuted;
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = visibleRange();
	for (int i = first; i < last; ++i)
	{
		const Note& note = *notes[static_cast<std::size_t>(i)];
		if (stroked(note)) { p.fillRect(noteRect(note), mark); }
	}
}

void NoteCanvas::paintSlicePreview(QPainter& p) const
{
	// The slice line: faint over the whole height, solid over the rows it spans.
	const int x = static_cast<int>(m_state.grid.xOfTick(sliceTick()));
	QColor faint = tokens::palette().accent;
	faint.setAlpha(80);
	p.fillRect(QRect(x, 0, 1, height()), faint);
	const int top = std::min(m_pressPos.y(), m_currentPos.y());
	const int bottom = std::max(m_pressPos.y(), m_currentPos.y());
	p.fillRect(QRect(x - 1, top, 2, bottom - top + 1), tokens::palette().accent);
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
	if (hasFocus())
	{
		// Where the note keys act: the shell's focus ring, as on its other focusable surfaces.
		p.setPen(QPen(pal.focusRing, tokens::focusRingWidth()));
		p.setBrush(Qt::NoBrush);
		p.drawRect(rect().adjusted(1, 1, -1, -1));
	}
	if (m_state.clip == nullptr)
	{
		p.setPen(pal.textMuted);
		p.setFont(tokens::font(tokens::TextRole::Body));
		p.drawText(rect(), Qt::AlignCenter, tr("Open a clip: double-click a MIDI clip in the arranger."));
	}
}

} // namespace lmms::gui
