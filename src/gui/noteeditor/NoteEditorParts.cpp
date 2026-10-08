/*
 * NoteEditorParts.cpp - the note editor's key column, ruler and expression lane
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

#include "NoteEditorParts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Accessibility.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "LaneScale.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteLaneKeys.h"

namespace lmms::gui
{

namespace
{

constexpr int kKeyboardWidth = 84;
constexpr int kRulerHeight = 24;

bool centred(const NoteLaneSpec& spec) { return spec.minimum < 0; }

} // namespace

// ---------------------------------------------------------------------------------------------
// the key column

NoteKeyboardView::NoteKeyboardView(NoteEditor* editor)
	: QWidget(editor)
	, m_editor(editor)
	, m_state(editor->state())
{
	setObjectName(QStringLiteral("noteKeyboard"));
	a11y::announce(this, tr("Keys"), tr("The key of each row; press one to hear it."));
}

QSize NoteKeyboardView::sizeHint() const { return {kKeyboardWidth, 200}; }

void NoteKeyboardView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	const NoteGrid& g = m_state.grid;
	QPainter p(this);
	p.fillRect(rect(), pal.raised);
	const int rowH = g.rowHeight();
	const int firstRow = std::max(0, g.scrollY() / rowH);
	const int lastRow = std::min(g.rowCount() - 1, (height() + g.scrollY()) / rowH);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	for (int row = firstRow; row <= lastRow; ++row)
	{
		const int key = g.rows()[static_cast<std::size_t>(row)];
		const QRect r(0, row * rowH - g.scrollY(), width(), rowH - 1);
		const bool black = NoteGrid::isBlackKey(key);
		QColor face = black ? pal.base : pal.control;
		if (key == m_pressed) { face = pal.accent; }
		p.fillRect(r.adjusted(black ? width() / 3 : 0, 0, 0, 0), face);
		if (m_state.highlightScale && g.inScale(key)) { p.fillRect(QRect(0, r.top(), 3, r.height()), pal.accent); }
		paintLabel(p, r, key);
	}
	p.fillRect(QRect(width() - 1, 0, 1, height()), pal.base);
}

void NoteKeyboardView::paintLabel(QPainter& p, const QRect& row, int key) const
{
	const QString name = m_editor->keyLabel(key);
	const bool c = key % 12 == 0;
	if (name.isEmpty() && !c && m_state.grid.rowHeight() < 16) { return; }
	p.setPen(!name.isEmpty() || c ? tokens::palette().text : tokens::palette().textMuted);
	p.drawText(row.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, name.isEmpty() ? NoteGrid::keyName(key) : name);
}

void NoteKeyboardView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::MiddleButton)
	{
		m_zoomY = event->pos().y();
		m_zoomRows = m_state.grid.rowHeight();
		m_zoomKey = m_state.grid.keyAtY(m_zoomY);
		return;
	}
	// Only the left button auditions: the right one is the editor's context menu, the middle one
	// the row-height drag below. Neither sounds the key.
	m_pressed = event->button() == Qt::LeftButton ? m_state.grid.keyAtY(event->pos().y()) : -1;
	if (m_pressed >= 0) { m_editor->auditionPress(m_pressed, m_state.lastVelocity); }
	update();
}

void NoteKeyboardView::mouseMoveEvent(QMouseEvent* event)
{
	// Up makes the rows taller, down shorter - a pixel of row height per eight of drag - with the key the
	// drag started on staying under the pointer.
	if (m_zoomY < 0) { return; }
	NoteGrid& g = m_state.grid;
	g.setRowHeight(m_zoomRows + (m_zoomY - event->pos().y()) / 8);
	if (const int row = g.rowOfKey(m_zoomKey); row >= 0) { g.setScrollY(row * g.rowHeight() + g.rowHeight() / 2 - m_zoomY); }
	emit m_editor->stateChanged();
}

void NoteKeyboardView::mouseReleaseEvent(QMouseEvent*)
{
	m_zoomY = -1;
	if (m_pressed >= 0) { m_editor->auditionRelease(m_pressed); }
	m_pressed = -1;
	update();
}

void NoteKeyboardView::wheelEvent(QWheelEvent* event)
{
	// A trackpad sends pixel deltas and no angle deltas: those scroll by their pixels.
	const int angle = event->angleDelta().y();
	m_state.grid.setScrollY(m_state.grid.scrollY() - (angle != 0 ? angle / 2 : event->pixelDelta().y()));
	emit m_editor->stateChanged();
}

// ---------------------------------------------------------------------------------------------
// the ruler and the clip's loop region

NoteRuler::NoteRuler(NoteEditor* editor)
	: QWidget(editor)
	, m_editor(editor)
	, m_state(editor->state())
{
	setObjectName(QStringLiteral("noteRuler"));
	setMouseTracking(true);
	a11y::announce(this, tr("Bar ruler"), tr("Bars, and the clip's loop region; drag its end to resize the clip."));
	NoteCanvas::repaintOnMeterChange(this);
}

QSize NoteRuler::sizeHint() const { return {200, kRulerHeight}; }

int NoteRuler::loopEndX() const
{
	if (m_state.clip == nullptr) { return -1; }
	return static_cast<int>(m_state.grid.xOfTick(m_state.clip->length().getTicks()));
}

bool NoteRuler::overLoopEnd(int x) const
{
	// The clip's end is a drag target no narrower than the design system's (tokens::targetSize).
	return m_state.clip != nullptr && std::abs(x - loopEndX()) <= tokens::targetSize() / 2;
}

int NoteRuler::draggedLength(int x) const
{
	const int tick = static_cast<int>(m_state.grid.tickAtX(x));
	const int step = std::max(m_state.gridTicks, m_state.grid.beatTicksAt(tick));  // at least the metre's beat
	return std::max(step, NoteGrid::snap(tick, step));
}

void NoteRuler::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	NoteCanvas::followSongMeter(m_state);
	const NoteGrid& g = m_state.grid;
	QPainter p(this);
	p.fillRect(rect(), pal.raised);
	const int end = m_dragging ? static_cast<int>(g.xOfTick(draggedLength(m_dragX))) : loopEndX();
	if (end >= 0)
	{
		QColor loop = pal.accent;
		loop.setAlpha(90);
		const int start = static_cast<int>(g.xOfTick(0));
		p.fillRect(QRect(start, height() - 6, end - start, 6), loop);
		p.fillRect(QRect(end - 2, 0, 3, height()), pal.accent);
	}
	p.setFont(tokens::font(tokens::TextRole::Caption));
	// The bars in the song's metre (0.5.0 W2), numbered from the clip's start; a label at least every 48 px.
	for (const NoteGrid::Bar& bar : g.barsBetween(g.firstVisibleTick(), g.lastVisibleTick()))
	{
		const int x = static_cast<int>(g.xOfTick(bar.tick));
		p.fillRect(QRect(x, height() / 2, 1, height() / 2), pal.gridBar);
		const int barsPerLabel = std::max(1, static_cast<int>(std::ceil(48.0 / (bar.barTicks * g.pixelsPerTick()))));
		if (bar.index % barsPerLabel != 0) { continue; }
		p.setPen(pal.textMuted);
		p.drawText(QRect(x + 3, 0, 60, height() - 6), Qt::AlignVCenter | Qt::AlignLeft, QString::number(bar.index + 1));
	}
	p.fillRect(QRect(0, height() - 1, width(), 1), pal.base);
}

void NoteRuler::cancelDrag()
{
	m_dragging = false;
	update();
}

void NoteRuler::mousePressEvent(QMouseEvent* event)
{
	// The left button only: a right or middle click must neither seek nor start a resize.
	if (m_state.clip == nullptr || event->button() != Qt::LeftButton) { return; }
	if (overLoopEnd(event->pos().x()))
	{
		m_dragging = true;
		m_dragX = event->pos().x();
		return;
	}
	// Anywhere else on the ruler moves the song position there (transport.seek), snapped to the
	// grid - the arrangement position of that point of the clip.
	const int tick = std::max(0, NoteGrid::snapDown(static_cast<int>(m_state.grid.tickAtX(event->pos().x())), m_state.gridTicks));
	m_editor->run(QStringLiteral("transport.seek"),
		{{QStringLiteral("ticks"), static_cast<int>(m_state.clip->startPosition().getTicks()) + tick}});
	m_editor->updatePlayhead(true);
}

void NoteRuler::mouseMoveEvent(QMouseEvent* event)
{
	setCursor(overLoopEnd(event->pos().x()) || m_dragging ? Qt::SizeHorCursor : Qt::ArrowCursor);
	if (!m_dragging) { return; }
	m_dragX = event->pos().x();
	update();
}

void NoteRuler::mouseReleaseEvent(QMouseEvent* event)
{
	if (!m_dragging) { return; }
	m_dragging = false;
	m_editor->run(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), m_editor->clipId()},
		{QStringLiteral("length"), draggedLength(event->pos().x())}});
	update();
}

// ---------------------------------------------------------------------------------------------
// the expression lane

NoteLaneView::NoteLaneView(NoteEditor* editor)
	: QWidget(editor)
	, m_editor(editor)
	, m_state(editor->state())
{
	setObjectName(QStringLiteral("noteLane"));
	setMouseTracking(true);  // 0.5.0 theme 11: the value under the pointer reads out while it hovers
	a11y::announce(this, tr("Expression lane"),
		tr("One value per note - velocity, pan, chance, timbre, pressure, micro-pitch, release velocity, velocity "
			"spread or repeats; drag across notes to draw. Keys: Left and Right go from note to note, Up and Down change "
			"the value (Page Up and Page Down by 10)."));
	lanekeys::attachExpressionLane(this, editor);  // theme 10: the keyboard (NoteLaneKeys.cpp)
}

QSize NoteLaneView::sizeHint() const { return {200, lanescale::laneHeight()}; }

int NoteLaneView::valueAtY(int y) const
{
	const NoteLaneSpec spec = notelanes::spec(m_state.lane);
	const double low = centred(spec) ? -spec.drawRange / 2.0 : spec.minimum;
	const double t = 1.0 - std::clamp(y, 2, height() - 3) / static_cast<double>(height());
	return notelanes::clamp(m_state.lane, static_cast<int>(std::lround(low + t * spec.drawRange)));
}

int NoteLaneView::yOfValue(int value) const
{
	const NoteLaneSpec spec = notelanes::spec(m_state.lane);
	const double low = centred(spec) ? -spec.drawRange / 2.0 : spec.minimum;
	const double t = std::clamp((value - low) / spec.drawRange, 0.0, 1.0);
	return static_cast<int>((1.0 - t) * (height() - 1));
}

int NoteLaneView::shownValue(const Note& note) const
{
	const auto it = m_pending.find(note.id());
	return it != m_pending.end() ? it->second : notelanes::valueOf(note, m_state.lane);
}

void NoteLaneView::cancelDrag()
{
	m_drawing = false;
	m_pending.clear();
	update();
}

void NoteLaneView::mousePressEvent(QMouseEvent* event)
{
	if (m_state.clip == nullptr || event->button() != Qt::LeftButton) { return; }
	m_drawing = true;
	m_pending.clear();
	m_drawAt = event->pos();
	drawAt(event->pos());
}

void NoteLaneView::mouseMoveEvent(QMouseEvent* event)
{
	m_hover = event->pos();
	if (m_drawing)
	{
		drawAlong(m_drawAt, event->pos());
		m_drawAt = event->pos();
	}
	else { update(); }  // the readout follows the pointer
}

void NoteLaneView::mouseReleaseEvent(QMouseEvent*)
{
	if (!m_drawing) { return; }
	m_drawing = false;
	commit();
}

void NoteLaneView::drawAt(const QPoint& pos)
{
	// Every visible note whose stem is within a few pixels of the pointer takes the value; with
	// the selection non-empty (anywhere in the clip) only selected notes do, so a ramp can be drawn
	// over a chord. The clip can go mid-drag (deleted): nothing to draw on then.
	if (m_state.clip == nullptr) { return; }
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = m_editor->canvas()->visibleRange();
	const bool anySelected = std::any_of(notes.begin(), notes.end(), [](const Note* note) { return note->selected(); });
	const int value = valueAtY(pos.y());
	for (int i = first; i < last; ++i)
	{
		const Note& note = *notes[static_cast<std::size_t>(i)];
		const int x = static_cast<int>(m_state.grid.xOfTick(note.pos().getTicks()));
		if (std::abs(x - pos.x()) <= lanescale::stemTolerance() && (!anySelected || note.selected()))
		{
			m_pending[note.id()] = value;
			m_lastValue = value;
		}
	}
	update();
}

void NoteLaneView::drawAlong(const QPoint& from, const QPoint& to)
{
	// Qt coalesces a fast drag into sparse move events, so the path between two events is walked as the
	// note area's strokes walk theirs: a stem the pointer passed over between them takes the value too.
	const QPoint path = to - from;
	const int steps = std::max(1, std::max(std::abs(path.x()), std::abs(path.y())) / 2);
	for (int i = 1; i <= steps; ++i) { drawAt(from + path * i / steps); }
}

void NoteLaneView::commit()
{
	if (m_pending.empty() || m_state.clip == nullptr) { m_pending.clear(); return; }
	const std::map<int, int> pending = std::move(m_pending);
	m_pending.clear();
	// Collect every write before running any: a command may re-sort or re-load the clip's note
	// list, so the list is not walked while commands run.
	std::vector<notelanes::Write> writes;
	for (const Note* note : m_state.clip->notes())
	{
		const auto it = pending.find(note->id());
		if (it != pending.end())
		{
			writes.push_back(notelanes::writeFor(m_state.lane, m_editor->clipId(), control::noteIdOf(note), it->second));
		}
	}
	QJsonArray batched;
	for (const notelanes::Write& write : writes)
	{
		if (write.command == QStringLiteral("note.edit")) { batched.append(write.args.value(QStringLiteral("edits")).toArray().at(0)); }
		else { m_editor->run(write.command, write.args); }
	}
	if (!batched.isEmpty())
	{
		m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), batched}});
	}
	if (m_state.lane == NoteLane::Velocity) { m_state.lastVelocity = m_lastValue; }
	update();
}

} // namespace lmms::gui
