/*
 * NoteCanvasInput.cpp - the note editor's note area: tools, gestures and keys
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
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

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

QJsonArray idsOf(const std::vector<Note*>& notes)
{
	QJsonArray ids;
	for (const Note* note : notes) { ids.append(control::noteIdOf(note)); }
	return ids;
}

bool snapOff(Qt::KeyboardModifiers mods) { return mods.testFlag(Qt::AltModifier); }

} // namespace

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
	const int delta = static_cast<int>(std::lround(m_state.grid.tickAtX(m_currentPos.x()) - m_pressTick));
	return m_drag == Drag::None ? 0 : NoteGrid::snap(delta, m_state.gridTicks);
}

int NoteCanvas::dragKeys() const
{
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
	if (event->button() == Qt::RightButton)
	{
		// The right button erases whatever tool is chosen, as in the classic piano roll.
		pressErase(noteAt(event->pos()));
		return;
	}
	if (event->button() == Qt::LeftButton) { beginPress(event); }
}

void NoteCanvas::beginPress(QMouseEvent* event)
{
	Note* note = noteAt(event->pos());
	switch (m_state.tool)
	{
	case NoteTool::Audition: pressAudition(note); return;
	case NoteTool::Erase: pressErase(note); return;
	case NoteTool::Paint: pressPaint(); return;
	case NoteTool::Slice:
		if (note != nullptr) { sliceAt(note, event->pos()); }
		return;
	case NoteTool::Draw:
	case NoteTool::Select: pressDrawOrSelect(note, event); return;
	}
}

void NoteCanvas::pressDrawOrSelect(Note* note, QMouseEvent* event)
{
	if (note == nullptr) { pressOnEmpty(event); }
	else if (event->modifiers().testFlag(Qt::AltModifier)) { pressVelocity(note); }
	else { pressOnNote(note, event); }
}

void NoteCanvas::pressAudition(const Note* note)
{
	m_drag = Drag::Audition;
	m_auditionKey = note != nullptr ? note->key() : m_pressKey;
	m_editor->auditionPress(m_auditionKey, note != nullptr ? static_cast<int>(note->getVolume()) : m_state.lastVelocity);
}

void NoteCanvas::pressErase(const Note* note)
{
	m_drag = Drag::Erase;
	m_erasedIds.clear();
	if (note != nullptr) { m_erasedIds.push_back(note->id()); }
	update();
}

void NoteCanvas::pressOnNote(Note* note, QMouseEvent* event)
{
	if (event->modifiers().testFlag(Qt::ShiftModifier))
	{
		std::vector<Note*> chosen = selectedNotes();
		const auto it = std::find(chosen.begin(), chosen.end(), note);
		if (it != chosen.end()) { chosen.erase(it); }
		else { chosen.push_back(note); }
		selectOnly(chosen, false);
		return;
	}
	if (!note->selected()) { selectOnly({note}, false); }
	Note* anchor = noteById(note->id());  // the select command may have re-sorted nothing, but resolve anew
	if (anchor == nullptr) { return; }
	m_anchorId = anchor->id();
	m_drag = onResizeHandle(*anchor, event->pos()) ? Drag::Resize : Drag::Move;
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
	m_drag = Drag::Band;
	if (!event->modifiers().testFlag(Qt::ShiftModifier) && !selectedNotes().empty()) { selectOnly({}, false); }
}

void NoteCanvas::mouseMoveEvent(QMouseEvent* event)
{
	m_currentPos = event->pos();
	if (m_drag == Drag::Erase)
	{
		Note* note = noteAt(event->pos());
		if (note != nullptr && std::find(m_erasedIds.begin(), m_erasedIds.end(), note->id()) == m_erasedIds.end())
		{
			m_erasedIds.push_back(note->id());
		}
	}
	if (m_drag == Drag::Paint) { extendPaint(event->pos()); }
	if (m_drag == Drag::None)
	{
		Note* note = noteAt(event->pos());
		const bool handle = note != nullptr && m_state.tool != NoteTool::Erase && onResizeHandle(*note, event->pos());
		setCursor(handle ? Qt::SizeHorCursor : Qt::ArrowCursor);
		return;
	}
	update();
}

void NoteCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	m_currentPos = event->pos();
	commitDrag(m_drag, event->modifiers().testFlag(Qt::ShiftModifier));
	if (m_auditionKey >= 0) { m_editor->auditionRelease(m_auditionKey); }
	m_auditionKey = -1;
	m_drag = Drag::None;
	m_anchorId = -1;
	update();
}

void NoteCanvas::commitDrag(Drag drag, bool shift)
{
	switch (drag)
	{
	case Drag::Draw: commitDraw(); break;
	case Drag::Move: commitMove(); break;
	case Drag::Resize: commitResize(); break;
	case Drag::Band: commitBand(shift); break;
	case Drag::Erase: commitErase(); break;
	case Drag::Paint: commitPaint(); break;
	case Drag::Velocity: commitVelocity(); break;
	case Drag::Audition:
	case Drag::None: break;
	}
}

void NoteCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
	// Select tool: a double-click on an empty row draws a note there, on a note removes it.
	if (m_state.clip == nullptr || m_state.tool != NoteTool::Select || event->button() != Qt::LeftButton) { return; }
	if (Note* note = noteAt(event->pos()))
	{
		m_editor->run(QStringLiteral("note.remove"), {{QStringLiteral("clip"), m_editor->clipId()},
			{QStringLiteral("note"), control::noteIdOf(note)}});
		return;
	}
	m_pressPos = m_currentPos = event->pos();
	m_pressTick = static_cast<int>(m_state.grid.tickAtX(event->pos().x()));
	m_pressKey = m_state.grid.keyAtY(event->pos().y());
	if (m_pressKey >= 0) { commitDraw(); }
}

std::pair<int, int> NoteCanvas::drawSpan() const
{
	// A click draws a note of the last length; a drag right of half a grid cell sets the length.
	const int start = std::max(0, NoteGrid::snapDown(m_pressTick, m_state.gridTicks));
	const int end = NoteGrid::snap(static_cast<int>(m_state.grid.tickAtX(m_currentPos.x())), m_state.gridTicks);
	const int length = end > start + std::max(1, m_state.gridTicks) / 2 ? end - start : m_state.lastLength;
	return {start, std::max(1, length)};
}

void NoteCanvas::commitDraw()
{
	const auto [start, length] = drawSpan();
	m_state.lastLength = length;
	m_editor->run(QStringLiteral("note.add"), {{QStringLiteral("clip"), m_editor->clipId()},
		{QStringLiteral("key"), scaleKey(m_pressKey)}, {QStringLiteral("position"), start},
		{QStringLiteral("length"), m_state.lastLength}, {QStringLiteral("velocity"), m_state.lastVelocity}});
}

void NoteCanvas::commitMove()
{
	const int dt = dragTicks();
	const int rows = dragKeys();
	if (dt == 0 && rows == 0) { return; }
	const bool copy = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
	QJsonArray edits;
	for (Note* note : selectedNotes())
	{
		const int position = std::max(0, static_cast<int>(note->pos().getTicks()) + dt);
		const int key = keyMovedBy(note->key(), rows);
		if (copy)
		{
			m_editor->run(QStringLiteral("note.add"), {{QStringLiteral("clip"), m_editor->clipId()},
				{QStringLiteral("key"), key}, {QStringLiteral("position"), position},
				{QStringLiteral("length"), static_cast<int>(note->length().getTicks())},
				{QStringLiteral("velocity"), static_cast<int>(note->getVolume())}});
			continue;
		}
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)},
			{QStringLiteral("position"), position}, {QStringLiteral("key"), key}});
	}
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

void NoteCanvas::commitBand(bool additive)
{
	const QRect band = QRect(m_pressPos, m_currentPos).normalized();
	std::vector<Note*> inside;
	for (Note* note : m_state.clip->notes())
	{
		if (noteRect(*note).intersects(band)) { inside.push_back(note); }
	}
	selectOnly(inside, additive);
}

void NoteCanvas::commitErase()
{
	for (int id : m_erasedIds)
	{
		if (Note* note = noteById(id))
		{
			m_editor->run(QStringLiteral("note.remove"), {{QStringLiteral("clip"), m_editor->clipId()},
				{QStringLiteral("note"), control::noteIdOf(note)}});
		}
	}
	m_erasedIds.clear();
}

void NoteCanvas::sliceAt(Note* note, const QPoint& pos)
{
	const int raw = static_cast<int>(m_state.grid.tickAtX(pos.x()));
	const int at = snapOff(QGuiApplication::keyboardModifiers()) ? raw : NoteGrid::snap(raw, m_state.gridTicks);
	const int start = note->pos().getTicks();
	const int end = note->endPos().getTicks();
	if (at <= start || at >= end) { return; }
	const int key = note->key();
	const int velocity = static_cast<int>(note->getVolume());
	QJsonArray edits{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("length"), at - start}}};
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
	m_editor->run(QStringLiteral("note.add"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("key"), key},
		{QStringLiteral("position"), at}, {QStringLiteral("length"), end - at}, {QStringLiteral("velocity"), velocity}});
}

void NoteCanvas::nudgeSelection(int ticks, int keys)
{
	QJsonArray edits;
	for (Note* note : selectedNotes())
	{
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)},
			{QStringLiteral("position"), std::max(0, static_cast<int>(note->pos().getTicks()) + ticks)},
			{QStringLiteral("key"), std::clamp(note->key() + keys, 0, 127)}});
	}
	if (edits.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::deleteSelection()
{
	std::vector<int> ids;
	for (Note* note : selectedNotes()) { ids.push_back(note->id()); }
	m_erasedIds = ids;
	commitErase();
}

bool NoteCanvas::nudgeForKey(int key, bool shift)
{
	const int step = std::max(1, m_state.gridTicks);
	const int octave = shift ? 12 : 1;
	switch (key)
	{
	case Qt::Key_Up: nudgeSelection(0, octave); return true;
	case Qt::Key_Down: nudgeSelection(0, -octave); return true;
	case Qt::Key_Left: nudgeSelection(-step, 0); return true;
	case Qt::Key_Right: nudgeSelection(step, 0); return true;
	default: return false;
	}
}

void NoteCanvas::keyPressEvent(QKeyEvent* event)
{
	const int key = event->key();
	const bool ctrlA = key == Qt::Key_A && event->modifiers().testFlag(Qt::ControlModifier) && m_state.clip != nullptr;
	if (key == Qt::Key_Delete || key == Qt::Key_Backspace) { deleteSelection(); }
	else if (key == Qt::Key_Escape) { selectOnly({}, false); }
	else if (ctrlA) { selectOnly(std::vector<Note*>(m_state.clip->notes().begin(), m_state.clip->notes().end()), false); }
	else if (!nudgeForKey(key, event->modifiers().testFlag(Qt::ShiftModifier)))
	{
		event->ignore();
		return;
	}
	event->accept();
}

void NoteCanvas::wheelEvent(QWheelEvent* event)
{
	const QPoint delta = event->angleDelta();
	const int amount = delta.y() != 0 ? delta.y() : delta.x();
	const Qt::KeyboardModifiers mods = event->modifiers();
	NoteGrid& g = m_state.grid;
	if (mods.testFlag(Qt::ControlModifier))
	{
		// Smooth zoom: continuous, anchored under the pointer.
		g.zoomAround(std::pow(1.0015, amount), event->position().x());
	}
	else if (mods.testFlag(Qt::AltModifier)) { g.setRowHeight(g.rowHeight() + (amount > 0 ? 1 : -1)); }
	else if (mods.testFlag(Qt::ShiftModifier) || delta.x() != 0)
	{
		g.setScrollTicks(g.scrollTicks() - amount / g.pixelsPerTick() / 4.0);
	}
	else { g.setScrollY(g.scrollY() - amount / 2); }
	event->accept();
	emit m_editor->stateChanged();
}

void NoteCanvas::paintDragPreview(QPainter& p) const
{
	switch (m_drag)
	{
	case Drag::Draw: paintDrawPreview(p); break;
	case Drag::Erase: paintErasePreview(p); break;
	case Drag::Paint: paintPaintPreview(p); break;
	case Drag::Velocity: paintVelocityPreview(p); break;
	case Drag::Move:
	case Drag::Resize: paintMovePreview(p); break;
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

void NoteCanvas::paintErasePreview(QPainter& p) const
{
	for (int id : m_erasedIds)
	{
		if (const Note* note = noteById(id)) { p.fillRect(noteRect(*note), tokens::palette().record); }
	}
}

int NoteCanvas::resizeDelta() const
{
	const Note* anchor = noteById(m_anchorId);
	if (m_drag != Drag::Resize || anchor == nullptr) { return 0; }
	const int end = anchor->endPos().getTicks();
	const int raw = end + static_cast<int>(std::lround(m_state.grid.tickAtX(m_currentPos.x()) - m_pressTick));
	return NoteGrid::snap(raw, m_state.gridTicks) - end;
}

void NoteCanvas::paintMovePreview(QPainter& p) const
{
	const int dt = m_drag == Drag::Move ? dragTicks() : 0;
	const int rows = m_drag == Drag::Move ? dragKeys() : 0;
	const int dLen = resizeDelta();
	for (const Note* note : selectedNotes())
	{
		const int start = std::max(0, static_cast<int>(note->pos().getTicks()) + dt);
		const int length = std::max(1, static_cast<int>(note->length().getTicks()) + dLen);
		paintNote(p, *note, rectFor(keyMovedBy(note->key(), rows), start, start + length));
	}
}

} // namespace lmms::gui
