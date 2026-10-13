/*
 * NoteCanvasKeys.cpp - the note editor's note area: keys, the clipboard, the wheel and the middle button
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

#include <QJsonArray>
#include <QKeyEvent>
#include <QWheelEvent>

#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "volume.h"

namespace lmms::gui
{

namespace
{

constexpr int kWheelVelocityStep = 5;  //!< Alt+wheel: velocity per notch on the 0..200 scale (Ctrl+Alt: 1)

//! Ctrl+B: FL's duplicate (Command+B on macOS, as Qt maps Ctrl there).
bool isDuplicate(const QKeyEvent* event)
{
	return event->key() == Qt::Key_B && event->modifiers() == Qt::ControlModifier;
}

//! The keys the note area acts on itself (the editor's tool keys are NoteEditor::handlesKey).
bool canvasKey(const QKeyEvent* event)
{
	const bool plain = event->modifiers() == Qt::NoModifier;
	return (plain && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace))
		|| event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut) || event->matches(QKeySequence::Paste)
		|| event->matches(QKeySequence::SelectAll) || isDuplicate(event);
}

} // namespace

bool NoteCanvas::event(QEvent* event)
{
	// While the note area has the focus, its keys and the tool letters are the editor's, not a window
	// shortcut's: FL's piano roll owns them the same way.
	if (event->type() == QEvent::ShortcutOverride)
	{
		const auto* key = static_cast<QKeyEvent*>(event);
		if (canvasKey(key) || isEditKey(key) || NoteEditor::handlesKey(key))
		{
			event->accept();
			return true;
		}
	}
	return QWidget::event(event);
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
	m_strokeIds.clear();
	for (const Note* note : selectedNotes()) { m_strokeIds.push_back(note->id()); }
	std::sort(m_strokeIds.begin(), m_strokeIds.end());
	commitErase();
}

bool NoteCanvas::nudgeForKey(int key, Qt::KeyboardModifiers modifiers)
{
	modifiers &= ~Qt::KeypadModifier;
	const bool ctrlAlt = modifiers == (Qt::ControlModifier | Qt::AltModifier)
		|| modifiers == (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
	if (modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier && !ctrlAlt) { return false; }
	const int step = std::max(1, m_state.gridTicks);
	const bool shift = modifiers.testFlag(Qt::ShiftModifier);
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

bool NoteCanvas::selectionKey(const QKeyEvent* event)
{
	const int key = event->key();
	if (key == Qt::Key_Delete || key == Qt::Key_Backspace) { deleteSelection(); }
	else if (key == Qt::Key_Escape) { selectOnly({}, false); }
	else if (event->matches(QKeySequence::SelectAll))
	{
		selectOnly(std::vector<Note*>(m_state.clip->notes().begin(), m_state.clip->notes().end()), false);
	}
	else { return false; }
	return true;
}

void NoteCanvas::runClipboard(const QString& command)
{
	QJsonObject args{{QStringLiteral("clip"), m_editor->clipId()}};
	if (command == QLatin1String("note.duplicate")) { args.insert(QStringLiteral("grid"), std::max(1, m_state.gridTicks)); }
	if (command != QLatin1String("note.paste"))
	{
		args.insert(QStringLiteral("scope"), selectedNotes().empty() ? QStringLiteral("clip") : QStringLiteral("selection"));
	}
	m_editor->run(command, args);
}

bool NoteCanvas::clipboardKey(const QKeyEvent* event)
{
	if (event->matches(QKeySequence::Copy)) { runClipboard(QStringLiteral("note.copy")); }
	else if (event->matches(QKeySequence::Cut)) { runClipboard(QStringLiteral("note.cut")); }
	else if (event->matches(QKeySequence::Paste)) { runClipboard(QStringLiteral("note.paste")); }
	else if (isDuplicate(event)) { runClipboard(QStringLiteral("note.duplicate")); }
	else { return false; }
	return true;
}

void NoteCanvas::keyPressEvent(QKeyEvent* event)
{
	// Theme 10's keys (NoteCanvasAccess.cpp) come before the nudge, which takes the arrows with Shift or nothing.
	const bool handled = m_state.clip != nullptr && (clipboardKey(event) || selectionKey(event) || editKey(event)
		|| nudgeForKey(event->key(), event->modifiers()));
	if (!handled)
	{
		event->ignore();  // the tool letters go on to the editor
		return;
	}
	event->accept();
}

void NoteCanvas::wheelEvent(QWheelEvent* event)
{
	const QPoint delta = event->angleDelta();
	const int amount = delta.y() != 0 ? delta.y() : delta.x();
	const Note* note = noteAt(event->position().toPoint());
	if (event->modifiers().testFlag(Qt::AltModifier) && note != nullptr && amount != 0)
	{
		wheelOnNote(note, amount, event->modifiers());
	}
	else { wheelView(event, amount); }
	event->accept();
	emit m_editor->stateChanged();
}

void NoteCanvas::wheelOnNote(const Note* note, int amount, Qt::KeyboardModifiers mods)
{
	// FL: Alt+wheel over a note changes its velocity (Ctrl+Alt finely), Shift+Alt nudges it in time - the
	// selection with it when the note is part of one. One note.edit a notch.
	const int sign = amount > 0 ? 1 : -1;
	const bool nudge = mods.testFlag(Qt::ShiftModifier);
	const int step = mods.testFlag(Qt::ControlModifier) ? 1 : kWheelVelocityStep;
	const std::vector<Note*> targets = note->selected() ? selectedNotes() : std::vector<Note*>{noteById(note->id())};
	QJsonArray edits;
	for (const Note* target : targets)
	{
		if (target == nullptr) { continue; }
		QJsonObject edit{{QStringLiteral("note"), control::noteIdOf(target)}};
		if (nudge) { edit.insert(QStringLiteral("position"), std::max(0, static_cast<int>(target->pos().getTicks()) + sign * std::max(1, m_state.gridTicks))); }
		else { edit.insert(QStringLiteral("velocity"), std::clamp(static_cast<int>(target->getVolume()) + sign * step, int{MinVolume}, int{MaxVolume})); }
		edits.append(edit);
	}
	if (edits.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::wheelView(const QWheelEvent* event, int amount)
{
	const Qt::KeyboardModifiers mods = event->modifiers();
	NoteGrid& g = m_state.grid;
	if (mods.testFlag(Qt::ControlModifier))
	{
		// Smooth zoom: continuous, anchored under the pointer.
		g.zoomAround(std::pow(1.0015, amount), event->position().x());
	}
	else if (mods.testFlag(Qt::AltModifier)) { g.setRowHeight(g.rowHeight() + (amount > 0 ? 1 : -1)); }  // off the notes
	else if (mods.testFlag(Qt::ShiftModifier) || event->angleDelta().x() != 0)
	{
		g.setScrollTicks(g.scrollTicks() - amount / g.pixelsPerTick() / 4.0);
	}
	else { g.setScrollY(g.scrollY() - amount / 2); }
}

void NoteCanvas::pressPan()
{
	m_drag = Drag::Pan;
	m_panTicks = m_state.grid.scrollTicks();
	m_panY = m_state.grid.scrollY();
	setCursor(Qt::ClosedHandCursor);
}

void NoteCanvas::panTo(const QPoint& pos)
{
	// The middle button drags the view both ways, the content following the pointer.
	const QPoint moved = pos - m_pressPos;
	m_state.grid.setScrollTicks(m_panTicks - moved.x() / m_state.grid.pixelsPerTick());
	m_state.grid.setScrollY(m_panY - moved.y());
	emit m_editor->stateChanged();
}

} // namespace lmms::gui
