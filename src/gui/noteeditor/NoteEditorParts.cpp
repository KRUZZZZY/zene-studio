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
#include <vector>

#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Accessibility.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"

namespace lmms::gui
{

namespace
{

constexpr int kKeyboardWidth = 84;
constexpr int kRulerHeight = 24;
constexpr int kLaneHeight = 96;

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
		if (key % 12 == 0 || rowH >= 16)
		{
			p.setPen(key % 12 == 0 ? pal.text : pal.textMuted);
			p.drawText(r.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, NoteGrid::keyName(key));
		}
	}
	p.fillRect(QRect(width() - 1, 0, 1, height()), pal.base);
}

void NoteKeyboardView::mousePressEvent(QMouseEvent* event)
{
	m_pressed = m_state.grid.keyAtY(event->pos().y());
	if (m_pressed >= 0) { m_editor->auditionPress(m_pressed, m_state.lastVelocity); }
	update();
}

void NoteKeyboardView::mouseReleaseEvent(QMouseEvent*)
{
	if (m_pressed >= 0) { m_editor->auditionRelease(m_pressed); }
	m_pressed = -1;
	update();
}

void NoteKeyboardView::wheelEvent(QWheelEvent* event)
{
	m_state.grid.setScrollY(m_state.grid.scrollY() - event->angleDelta().y() / 2);
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
}

QSize NoteRuler::sizeHint() const { return {200, kRulerHeight}; }

int NoteRuler::loopEndX() const
{
	if (m_state.clip == nullptr) { return -1; }
	return static_cast<int>(m_state.grid.xOfTick(m_state.clip->length().getTicks()));
}

int NoteRuler::draggedLength(int x) const
{
	const int bar = DefaultTicksPerBar;
	const int step = std::max(m_state.gridTicks, bar / 4);
	return std::max(step, NoteGrid::snap(static_cast<int>(m_state.grid.tickAtX(x)), step));
}

void NoteRuler::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
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
	const int bar = DefaultTicksPerBar;
	const int barsPerLabel = std::max(1, static_cast<int>(std::ceil(48.0 / (bar * g.pixelsPerTick()))));
	const int firstBar = static_cast<int>(g.firstVisibleTick()) / bar;
	for (int b = firstBar; b * bar <= g.lastVisibleTick(); ++b)
	{
		const int x = static_cast<int>(g.xOfTick(b * bar));
		p.fillRect(QRect(x, height() / 2, 1, height() / 2), pal.gridBar);
		if (b % barsPerLabel != 0) { continue; }
		p.setPen(pal.textMuted);
		p.drawText(QRect(x + 3, 0, 60, height() - 6), Qt::AlignVCenter | Qt::AlignLeft, QString::number(b + 1));
	}
	p.fillRect(QRect(0, height() - 1, width(), 1), pal.base);
}

void NoteRuler::mousePressEvent(QMouseEvent* event)
{
	if (m_state.clip == nullptr) { return; }
	if (std::abs(event->pos().x() - loopEndX()) <= 6)
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
	const bool overEnd = m_state.clip != nullptr && std::abs(event->pos().x() - loopEndX()) <= 6;
	setCursor(overEnd || m_dragging ? Qt::SizeHorCursor : Qt::ArrowCursor);
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
	a11y::announce(this, tr("Expression lane"),
		tr("One value per note - velocity, pan, chance, timbre, pressure or micro-pitch; drag across notes to draw."));
}

QSize NoteLaneView::sizeHint() const { return {200, kLaneHeight}; }

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

void NoteLaneView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.panel);
	p.fillRect(QRect(0, 0, width(), 1), pal.base);
	const NoteLaneSpec spec = notelanes::spec(m_state.lane);
	const int baseY = centred(spec) ? yOfValue(0) : height() - 1;
	p.fillRect(QRect(0, baseY, width(), 1), pal.gridBeat);
	if (m_state.clip == nullptr) { return; }
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = m_editor->canvas()->visibleRange();
	// A dot on each stem only when the stems are far enough apart to tell them apart; zoomed
	// far out thousands of antialiased dots cost more than the whole note area.
	const bool heads = (last - first) * 6 < std::max(1, width());
	p.setRenderHint(QPainter::Antialiasing, heads);
	for (int i = first; i < last; ++i)
	{
		const Note& note = *notes[static_cast<std::size_t>(i)];
		const int x = static_cast<int>(m_state.grid.xOfTick(note.pos().getTicks()));
		const int y = yOfValue(shownValue(note));
		const QColor colour = note.selected() ? pal.text : tokens::velocityColor(static_cast<int>(note.getVolume()));
		p.fillRect(QRect(x, std::min(y, baseY), 2, std::abs(baseY - y) + 1), colour);
		if (!heads) { continue; }
		p.setPen(Qt::NoPen);
		p.setBrush(colour);
		p.drawEllipse(QPointF(x + 1, y), 3.5, 3.5);
	}
}

void NoteLaneView::mousePressEvent(QMouseEvent* event)
{
	if (m_state.clip == nullptr || event->button() != Qt::LeftButton) { return; }
	m_drawing = true;
	m_pending.clear();
	drawAt(event->pos());
}

void NoteLaneView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_drawing) { drawAt(event->pos()); }
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
	// the selection non-empty only selected notes do, so a ramp can be drawn over a chord.
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = m_editor->canvas()->visibleRange();
	bool anySelected = false;
	for (int i = first; i < last && !anySelected; ++i) { anySelected = notes[static_cast<std::size_t>(i)]->selected(); }
	const int value = valueAtY(pos.y());
	for (int i = first; i < last; ++i)
	{
		const Note& note = *notes[static_cast<std::size_t>(i)];
		const int x = static_cast<int>(m_state.grid.xOfTick(note.pos().getTicks()));
		if (std::abs(x - pos.x()) <= 4 && (!anySelected || note.selected())) { m_pending[note.id()] = value; }
	}
	update();
}

void NoteLaneView::commit()
{
	if (m_pending.empty()) { return; }
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
	if (m_state.lane == NoteLane::Velocity) { m_state.lastVelocity = pending.rbegin()->second; }
	update();
}

} // namespace lmms::gui
