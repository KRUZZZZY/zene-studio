/*
 * NoteCanvasInput.cpp - the note editor's note area: the mouse, as FL Studio's piano roll has it
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

#include <QGuiApplication>
#include <QJsonArray>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleHints>

#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteProperties.h"

namespace lmms::gui
{

namespace
{

QJsonArray idsOf(const std::vector<Note*>& notes)
{
	QJsonArray ids;
	for (const Note* note : notes) { ids.append(control::noteIdOf(note)); }
	return ids;
}

} // namespace

bool NoteCanvas::altHeld() const
{
	return (m_mods | QGuiApplication::keyboardModifiers()).testFlag(Qt::AltModifier);
}

int NoteCanvas::snapTicks(int ticks) const { return altHeld() ? ticks : NoteGrid::snap(ticks, m_state.gridTicks); }

Note* NoteCanvas::noteById(int id) const
{
	if (m_state.clip == nullptr || id < 0) { return nullptr; }
	for (Note* note : m_state.clip->notes())
	{
		if (note->id() == id) { return note; }
	}
	return nullptr;
}

std::vector<Note*> NoteCanvas::selectedNotes() const
{
	std::vector<Note*> out;
	if (m_state.clip == nullptr) { return out; }
	for (Note* note : m_state.clip->notes())
	{
		if (note->selected()) { out.push_back(note); }
	}
	return out;
}

void NoteCanvas::selectOnly(const std::vector<Note*>& notes, bool additive)
{
	std::vector<Note*> chosen = notes;
	if (additive)
	{
		for (Note* note : selectedNotes())
		{
			if (std::find(chosen.begin(), chosen.end(), note) == chosen.end()) { chosen.push_back(note); }
		}
	}
	m_editor->run(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()},
		{QStringLiteral("notes"), idsOf(chosen)}});
}

int NoteCanvas::dragTicks() const
{
	// Ctrl while dragging locks the timing (FL's horizontal lock).
	if (m_drag == Drag::None || (m_drag == Drag::Move && m_mods.testFlag(Qt::ControlModifier))) { return 0; }
	return snapTicks(static_cast<int>(std::lround(m_state.grid.tickAtX(m_currentPos.x()) - m_pressTick)));
}

int NoteCanvas::dragKeys() const
{
	// Shift while dragging locks the pitch (FL's vertical lock).
	if (m_drag == Drag::Move && m_mods.testFlag(Qt::ShiftModifier)) { return 0; }
	const int rowH = m_state.grid.rowHeight();
	return static_cast<int>(std::floor((m_currentPos.y() + m_state.grid.scrollY()) / double(rowH)))
		- static_cast<int>(std::floor((m_pressPos.y() + m_state.grid.scrollY()) / double(rowH)));
}

int NoteCanvas::keyMovedBy(int key, int rows) const
{
	const NoteGrid& g = m_state.grid;
	const int row = g.rowOfKey(key);
	if (row < 0) { return scaleKey(std::clamp(key - rows, 0, 127)); }
	const int target = std::clamp(row + rows, 0, g.rowCount() - 1);
	return scaleKey(g.rows()[static_cast<std::size_t>(target)]);
}

void NoteCanvas::mousePressEvent(QMouseEvent* event)
{
	if (m_state.clip == nullptr) { return; }
	setFocus(Qt::MouseFocusReason);
	m_pressPos = m_currentPos = event->pos();
	m_pressTick = static_cast<int>(m_state.grid.tickAtX(event->pos().x()));
	m_pressKey = m_state.grid.keyAtY(event->pos().y());
	m_mods = m_pressMods = event->modifiers();
	switch (event->button())
	{
	case Qt::RightButton: pressRight(noteAt(event->pos())); break;
	case Qt::MiddleButton: pressPan(); break;
	case Qt::LeftButton: beginPress(event); break;
	default: break;
	}
}

void NoteCanvas::pressRight(const Note* note)
{
	// FL, whatever the tool: the right button deletes every note it crosses; held on an empty row it is
	// the slice tool for the stroke.
	if (note != nullptr) { pressStroke(Drag::Erase, note); }
	else { m_drag = Drag::Slice; }
}

void NoteCanvas::beginPress(QMouseEvent* event)
{
	Note* note = noteAt(event->pos());
	switch (m_state.tool)
	{
	case NoteTool::Audition: pressAudition(note); return;
	case NoteTool::Erase: pressStroke(Drag::Erase, note); return;
	case NoteTool::Mute: pressStroke(Drag::Mute, note); return;
	case NoteTool::Paint: pressPaint(); return;
	case NoteTool::Slice: m_drag = Drag::Slice; return;
	case NoteTool::Draw:
	case NoteTool::Select: pressDrawOrSelect(note, event); return;
	}
}

void NoteCanvas::pressDrawOrSelect(Note* note, QMouseEvent* event)
{
	// Ctrl selects (a click one note, a drag a rectangle; Ctrl+Shift adds), whatever is under the pointer.
	if (m_pressMods.testFlag(Qt::ControlModifier)) { pressBand(event); }
	else if (note == nullptr) { pressOnEmpty(event); }
	else { pressOnNote(note, event); }
}

void NoteCanvas::pressAudition(const Note* note)
{
	m_drag = Drag::Audition;
	m_auditionKey = note != nullptr ? note->key() : m_pressKey;
	m_editor->auditionPress(m_auditionKey, note != nullptr ? static_cast<int>(note->getVolume()) : m_state.lastVelocity);
}

void NoteCanvas::pressStroke(Drag drag, const Note* note)
{
	m_drag = drag;
	m_strokeIds.clear();
	m_muteTo = -1;
	m_anchorId = note != nullptr ? note->id() : -1;
	markStroke(note);
	update();
}

void NoteCanvas::markStroke(const Note* note)
{
	// Kept sorted, so the preview and the release look a note up in log time, not by a scan.
	if (note == nullptr) { return; }
	if (m_drag == Drag::Mute && m_muteTo < 0) { m_muteTo = note->muted() ? 0 : 1; }  // the first note crossed decides
	const auto it = std::lower_bound(m_strokeIds.begin(), m_strokeIds.end(), note->id());
	if (it == m_strokeIds.end() || *it != note->id()) { m_strokeIds.insert(it, note->id()); }
}

void NoteCanvas::pressOnNote(Note* note, QMouseEvent* event)
{
	m_state.lastLength = static_cast<int>(note->length().getTicks());  // a new note takes the last touched one's length
	if (!note->selected()) { selectOnly({note}, false); }
	Note* anchor = noteById(note->id());  // the select command may have re-sorted nothing, but resolve anew
	if (anchor == nullptr) { return; }
	m_anchorId = anchor->id();
	const bool fromStart = m_pressMods.testFlag(Qt::ShiftModifier) && onStartHandle(*anchor, event->pos());
	m_drag = onResizeHandle(*anchor, event->pos()) ? Drag::Resize : fromStart ? Drag::ResizeStart : Drag::Move;
	m_auditionKey = anchor->key();
	m_editor->auditionPress(anchor->key(), static_cast<int>(anchor->getVolume()));
}

void NoteCanvas::pressOnEmpty(QMouseEvent* event)
{
	if (m_state.tool == NoteTool::Draw && m_pressKey >= 0)
	{
		m_drag = Drag::Draw;
		m_auditionKey = m_pressKey;
		m_editor->auditionPress(m_pressKey, m_state.lastVelocity);
		return;
	}
	pressBand(event);
}

void NoteCanvas::pressBand(QMouseEvent* event)
{
	m_drag = Drag::Band;
	const bool additive = m_pressMods.testFlag(Qt::ShiftModifier);
	if (!additive && noteAt(event->pos()) == nullptr && !selectedNotes().empty()) { selectOnly({}, false); }
}

void NoteCanvas::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint from = m_currentPos;
	m_currentPos = event->pos();
	m_mods = event->modifiers();
	switch (m_drag)
	{
	case Drag::None: updateCursor(event->pos()); return;
	case Drag::Pan: panTo(event->pos()); return;
	case Drag::Erase:
	case Drag::Mute: strokeAlong(from, event->pos()); break;
	case Drag::Paint: paintAlong(from, event->pos()); break;
	case Drag::Audition: scrubTo(event->pos()); break;
	default: break;
	}
	update();
}

void NoteCanvas::updateCursor(const QPoint& pos)
{
	const Note* note = noteAt(pos);
	const bool editing = note != nullptr && (m_state.tool == NoteTool::Draw || m_state.tool == NoteTool::Select);
	const bool handle = editing && (onResizeHandle(*note, pos)
		|| (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier) && onStartHandle(*note, pos)));
	setCursor(handle ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void NoteCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	const QPoint from = m_currentPos;
	m_currentPos = event->pos();
	m_mods = event->modifiers();
	if (m_drag == Drag::Paint) { paintAlong(from, m_currentPos); }
	if (m_drag == Drag::Draw) { m_drawnAt = event->timestamp(); }
	commitDrag(m_drag);
	if (m_auditionKey >= 0) { m_editor->auditionRelease(m_auditionKey); }
	m_auditionKey = -1;
	m_drag = Drag::None;
	m_anchorId = -1;
	update();
}

void NoteCanvas::commitDrag(Drag drag)
{
	switch (drag)
	{
	case Drag::Draw: commitDraw(); break;
	case Drag::Move: commitMove(); break;
	case Drag::Resize: commitResize(); break;
	case Drag::ResizeStart: commitResizeStart(); break;
	case Drag::Band: commitBand(); break;
	default: commitStroke(drag); break;
	}
}

void NoteCanvas::commitStroke(Drag drag)
{
	switch (drag)
	{
	case Drag::Erase: commitErase(); break;
	case Drag::Slice: commitSlice(); break;
	case Drag::Mute: commitMute(); break;
	case Drag::Paint: commitPaint(); break;
	default: break;
	}
}

void NoteCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
	// FL: a double-click on a note opens its properties - not when its first click drew that note.
	const bool editing = m_state.tool == NoteTool::Draw || m_state.tool == NoteTool::Select;
	if (m_state.clip == nullptr || event->button() != Qt::LeftButton || !editing) { return; }
	const Note* note = noteAt(event->pos());
	const auto interval = static_cast<ulong>(QGuiApplication::styleHints()->mouseDoubleClickInterval());
	const bool drawnByIt = note != nullptr && control::noteIdOf(note) == m_justDrawn && event->timestamp() - m_drawnAt <= interval;
	if (note != nullptr && !drawnByIt) { showNoteProperties(m_editor, *note); }
}

std::pair<int, int> NoteCanvas::drawSpan() const
{
	// A click draws a note of the last note's length (or New Note Length); a drag to the right sets the length
	// to where it reaches. A click is a click wherever in the cell it lands: only a real drag sets a length.
	const int start = std::max(0, altHeld() ? m_pressTick : NoteGrid::snapDown(m_pressTick, m_state.gridTicks));
	const int end = snapTicks(static_cast<int>(m_state.grid.tickAtX(m_currentPos.x())));
	const int clicked = m_state.newLength > 0 ? m_state.newLength : m_state.lastLength;
	const bool dragged = std::abs(m_currentPos.x() - m_pressPos.x()) > 3 && end > start;
	return {start, std::max(1, dragged ? end - start : clicked)};
}

void NoteCanvas::commitDraw()
{
	const auto [start, length] = drawSpan();
	m_state.lastLength = length;
	const QJsonObject added = m_editor->run(QStringLiteral("note.add"), {{QStringLiteral("clip"), m_editor->clipId()},
		{QStringLiteral("key"), scaleKey(m_pressKey)}, {QStringLiteral("position"), start},
		{QStringLiteral("length"), m_state.lastLength}, {QStringLiteral("velocity"), m_state.lastVelocity}});
	m_justDrawn = added.value(QStringLiteral("note")).toString();
}

void NoteCanvas::commitMove()
{
	const int dt = dragTicks();
	const int rows = dragKeys();
	if (dt == 0 && rows == 0) { return; }
	QJsonArray edits;
	for (Note* note : selectedNotes())
	{
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)},
			{QStringLiteral("position"), std::max(0, static_cast<int>(note->pos().getTicks()) + dt)},
			{QStringLiteral("key"), keyMovedBy(note->key(), rows)}});
	}
	// The whole selection as one note.edit: one undo step.
	if (!edits.isEmpty())
	{
		m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
	}
}

void NoteCanvas::commitResize()
{
	Note* anchor = noteById(m_anchorId);
	const int delta = resizeDelta();
	if (anchor == nullptr || delta == 0) { return; }
	QJsonArray edits;
	for (Note* note : selectedNotes())
	{
		const int length = std::max(1, static_cast<int>(note->length().getTicks()) + delta);
		if (note == anchor) { m_state.lastLength = length; }
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("length"), length}});
	}
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::commitResizeStart()
{
	// Shift-drag on the left edge: every selected note's start moves by the same snapped amount; its end stays.
	Note* anchor = noteById(m_anchorId);
	const int delta = startDelta();
	if (anchor == nullptr || delta == 0) { return; }
	QJsonArray edits;
	for (Note* note : selectedNotes())
	{
		const int end = static_cast<int>(note->endPos().getTicks());
		const int start = std::clamp(static_cast<int>(note->pos().getTicks()) + delta, 0, end - 1);
		if (note == anchor) { m_state.lastLength = end - start; }
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("position"), start},
			{QStringLiteral("length"), end - start}});
	}
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::commitBand()
{
	// A click (no real rectangle) picks the note under it; a drag every note the rectangle touches.
	const QRect band = QRect(m_pressPos, m_currentPos).normalized();
	std::vector<Note*> inside;
	if (band.width() < 3 && band.height() < 3)
	{
		if (Note* note = noteAt(m_pressPos)) { inside.push_back(note); }
	}
	else
	{
		for (Note* note : m_state.clip->notes())
		{
			if (noteRect(*note).intersects(band)) { inside.push_back(note); }
		}
	}
	selectOnly(inside, m_pressMods.testFlag(Qt::ShiftModifier));
}

void NoteCanvas::commitErase()
{
	// One pass over the clip collects the ids, and one note.remove_many deletes them: the stroke is one undo step.
	QJsonArray ids;
	if (m_state.clip != nullptr && !m_strokeIds.empty())
	{
		for (const Note* note : m_state.clip->notes())
		{
			if (stroked(*note)) { ids.append(control::noteIdOf(note)); }
		}
	}
	m_strokeIds.clear();
	if (ids.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.remove_many"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("notes"), ids}});
}

void NoteCanvas::paintDragPreview(QPainter& p) const
{
	switch (m_drag)
	{
	case Drag::Draw: paintDrawPreview(p); break;
	case Drag::Erase:
	case Drag::Mute: paintStrokePreview(p); break;
	case Drag::Paint: paintPaintPreview(p); break;
	case Drag::Slice: paintSlicePreview(p); break;
	case Drag::Move:
	case Drag::Resize:
	case Drag::ResizeStart: paintMovePreview(p); break;
	default: break;
	}
}

void NoteCanvas::paintDrawPreview(QPainter& p) const
{
	if (m_pressKey < 0) { return; }
	const auto [start, length] = drawSpan();
	QColor fill = tokens::velocityColor(m_state.lastVelocity);
	fill.setAlpha(170);
	p.fillRect(rectFor(scaleKey(m_pressKey), start, start + length), fill);
}

int NoteCanvas::resizeDelta() const
{
	const Note* anchor = noteById(m_anchorId);
	if (m_drag != Drag::Resize || anchor == nullptr) { return 0; }
	const int end = anchor->endPos().getTicks();
	const int raw = end + static_cast<int>(std::lround(m_state.grid.tickAtX(m_currentPos.x()) - m_pressTick));
	return snapTicks(raw) - end;
}

int NoteCanvas::startDelta() const
{
	const Note* anchor = noteById(m_anchorId);
	if (m_drag != Drag::ResizeStart || anchor == nullptr) { return 0; }
	const int start = anchor->pos().getTicks();
	const int raw = start + static_cast<int>(std::lround(m_state.grid.tickAtX(m_currentPos.x()) - m_pressTick));
	return snapTicks(raw) - start;
}

void NoteCanvas::paintMovePreview(QPainter& p) const
{
	const int dt = m_drag == Drag::Move ? dragTicks() : 0;
	const int rows = m_drag == Drag::Move ? dragKeys() : 0;
	const int dLen = resizeDelta();
	const int dStart = startDelta();
	for (const Note* note : selectedNotes())
	{
		// A resize from the start keeps the end (commitResizeStart's rule); a move or a resize keeps the start.
		const int end = static_cast<int>(note->endPos().getTicks());
		const int start = dStart != 0 ? std::clamp(static_cast<int>(note->pos().getTicks()) + dStart, 0, end - 1)
			: std::max(0, static_cast<int>(note->pos().getTicks()) + dt);
		const int length = dStart != 0 ? end - start : std::max(1, static_cast<int>(note->length().getTicks()) + dLen);
		paintNote(p, *note, rectFor(keyMovedBy(note->key(), rows), start, start + length));
	}
}

} // namespace lmms::gui
