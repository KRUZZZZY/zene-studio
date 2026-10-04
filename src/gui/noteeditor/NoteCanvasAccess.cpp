/*
 * NoteCanvasAccess.cpp - the note area without a mouse: moving between notes, editing them from the keyboard,
 *                        and what a screen reader reads
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

// 0.5.0 UX decision, theme 10: every edit the mouse makes on the note area has a key, each one registered command
// and one undo step. The arrow keys move the selection (NoteCanvasKeys.cpp, unchanged); here:
// - Alt+Left / Alt+Right: the previous / next note (Shift adds it to the selection); Alt+Up / Alt+Down: the note
//   above / below in the chord; Home / End: the first / last note - note.select;
// - Ctrl+Left / Ctrl+Right: the selection a grid step shorter / longer; Ctrl+Up / Ctrl+Down: its velocity 5 up /
//   down (Ctrl+Shift: 1, as Ctrl+Alt+wheel) - note.edit;
// - Insert: a note after the cursor's (same key, the last length and velocity), or at the view's left edge on the
//   middle row - note.add;
// - Enter: the cursor note's properties box (every value, typed);
// - Ctrl+Shift+L: the editable note list (window.item_list).
// The cursor - the note these keys act on - is marked with the focus ring's outline while the area has focus, and
// is what a screen reader reads as the area's value with the clip's note and selection counts.

#include <algorithm>

#include <QJsonArray>
#include <QKeyEvent>
#include <QSet>

#include "AccessibleCanvas.h"
#include "AccessibleWidgets.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteKeyNav.h"
#include "NoteProperties.h"
#include "volume.h"

namespace lmms::gui
{

namespace
{

constexpr int kKeyVelocityStep = 5;  //!< Ctrl+Up/Down: velocity per press on the 0..200 scale (Ctrl+Shift: 1)

//! The modifiers that pick a key's meaning: the keypad flag (arrows carry it on macOS) does not.
Qt::KeyboardModifiers modsOf(const QKeyEvent* event)
{
	return event->modifiers() & ~Qt::KeypadModifier;
}

bool isArrow(int key)
{
	return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
}

//! Exactly one of Alt and Ctrl, Shift allowed: Ctrl+Alt+arrow stays the plain arrows' nudge.
bool only(Qt::KeyboardModifiers mods, Qt::KeyboardModifier wanted)
{
	const Qt::KeyboardModifier other = wanted == Qt::AltModifier ? Qt::ControlModifier : Qt::AltModifier;
	return mods.testFlag(wanted) && !mods.testFlag(other) && !mods.testFlag(Qt::MetaModifier);
}

notekeys::Step stepFor(int key)
{
	switch (key)
	{
		case Qt::Key_Left: return notekeys::Step::Previous;
		case Qt::Key_Up: return notekeys::Step::Above;
		case Qt::Key_Down: return notekeys::Step::Below;
		default: return notekeys::Step::Next;
	}
}

} // namespace

void NoteCanvas::installKeyboardAccess()
{
	// The area paints its own focus ring (paintOverlay), so AccessibleMode's is not laid over it.
	a11y::describeCanvas(this, QAccessible::Canvas, [this] { return accessibleValue(); }, false);
	a11y::CanvasKeyCursor::of(this)->setMark([this] {
		const Note* cursor = cursorNote();
		return cursor != nullptr ? noteRect(*cursor).adjusted(-2, -2, 2, 2) : QRect();
	});
}

bool NoteCanvas::isEditKey(const QKeyEvent* event)
{
	static const QSet<int> plainKeys{Qt::Key_Home, Qt::Key_End, Qt::Key_Insert, Qt::Key_Return, Qt::Key_Enter};
	const Qt::KeyboardModifiers mods = modsOf(event);
	const int key = event->key();
	if (isArrow(key)) { return only(mods, Qt::AltModifier) || only(mods, Qt::ControlModifier); }
	if (mods == Qt::NoModifier) { return plainKeys.contains(key); }
	return key == Qt::Key_L && mods == (Qt::ControlModifier | Qt::ShiftModifier);
}

bool NoteCanvas::editKey(const QKeyEvent* event)
{
	if (!isEditKey(event)) { return false; }
	if (moveKey(event) || changeKey(event)) { return true; }
	const int key = event->key();
	if (key == Qt::Key_Insert) { insertAfterCursor(); }
	else if (key == Qt::Key_L)
	{
		m_editor->run(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("notes")}});
	}
	else if (const Note* cursor = cursorNote()) { showNoteProperties(m_editor, *cursor); }  // Enter
	return true;
}

bool NoteCanvas::moveKey(const QKeyEvent* event)
{
	const Qt::KeyboardModifiers mods = modsOf(event);
	const int key = event->key();
	if (isArrow(key) && only(mods, Qt::AltModifier))
	{
		moveCursor(static_cast<int>(stepFor(key)), mods.testFlag(Qt::ShiftModifier));
		return true;
	}
	if (key != Qt::Key_Home && key != Qt::Key_End) { return false; }
	moveCursor(static_cast<int>(key == Qt::Key_Home ? notekeys::Step::First : notekeys::Step::Last), false);
	return true;
}

bool NoteCanvas::changeKey(const QKeyEvent* event)
{
	const Qt::KeyboardModifiers mods = modsOf(event);
	const int key = event->key();
	if (!isArrow(key) || !only(mods, Qt::ControlModifier)) { return false; }
	const int grid = std::max(1, m_state.gridTicks);
	const int fine = mods.testFlag(Qt::ShiftModifier) ? 1 : kKeyVelocityStep;
	switch (key)
	{
		case Qt::Key_Left: resizeSelection(-grid); break;
		case Qt::Key_Right: resizeSelection(grid); break;
		case Qt::Key_Up: changeVelocity(fine); break;
		default: changeVelocity(-fine); break;
	}
	return true;
}

void NoteCanvas::moveCursor(int step, bool extend)
{
	const Note* to = notekeys::neighbour(m_state.clip->notes(), cursorNote(), static_cast<notekeys::Step>(step));
	if (to == nullptr)
	{
		a11y::announceStatus(this, tr("No note there"));
		return;
	}
	m_keyCursorId = to->id();
	selectOnly({noteById(to->id())}, extend);
	if (const Note* now = noteById(m_keyCursorId)) { showCursorNote(*now); }  // the select may have re-read the clip
	update();
}

void NoteCanvas::resizeSelection(int ticks)
{
	QJsonArray edits;
	int length = 0;
	for (const Note* note : selectedNotes())
	{
		length = std::max(1, static_cast<int>(note->length().getTicks()) + ticks);
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("length"), length}});
	}
	if (edits.isEmpty()) { return; }
	if (!m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}}).isEmpty())
	{
		m_state.lastLength = length;  // FL: a resized note's length is the next one's
	}
}

void NoteCanvas::changeVelocity(int amount)
{
	QJsonArray edits;
	for (const Note* note : selectedNotes())
	{
		const int velocity = std::clamp(static_cast<int>(note->getVolume()) + amount, int{MinVolume}, int{MaxVolume});
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("velocity"), velocity}});
	}
	if (edits.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::insertAfterCursor()
{
	const Note* cursor = cursorNote();
	const int middle = m_state.grid.keyAtY(height() / 2);
	const int key = cursor != nullptr ? cursor->key() : scaleKey(middle >= 0 ? middle : DefaultKey);
	const int start = cursor != nullptr ? static_cast<int>(cursor->pos().getTicks() + cursor->length().getTicks())
		: std::max(0, NoteGrid::snap(static_cast<int>(m_state.grid.firstVisibleTick()), m_state.gridTicks));
	const QJsonObject added = m_editor->run(QStringLiteral("note.add"), {{QStringLiteral("clip"), m_editor->clipId()},
		{QStringLiteral("key"), key}, {QStringLiteral("position"), start}, {QStringLiteral("length"), m_state.lastLength},
		{QStringLiteral("velocity"), m_state.lastVelocity}});
	const QString id = added.value(QStringLiteral("note")).toString();
	if (id.isEmpty() || m_state.clip == nullptr) { return; }
	m_editor->run(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("notes"), QJsonArray{id}}});
	for (const Note* note : m_state.clip->notes())
	{
		if (control::noteIdOf(note) == id) { m_keyCursorId = note->id(); }
	}
	update();
}

const Note* NoteCanvas::cursorNote() const
{
	if (m_state.clip == nullptr) { return nullptr; }
	const Note* cursor = noteById(m_keyCursorId);
	if (cursor != nullptr && cursor->selected()) { return cursor; }
	return notekeys::firstSelected(m_state.clip->notes());
}

void NoteCanvas::showCursorNote(const Note& note)
{
	NoteGrid& g = m_state.grid;
	const double start = note.pos().getTicks();
	if (start < g.firstVisibleTick() || start >= g.lastVisibleTick())
	{
		g.setScrollTicks(std::max(0.0, start - (g.lastVisibleTick() - g.firstVisibleTick()) / 4.0));
	}
	const int y = g.yOfKey(note.key());
	if (y < 0 || y + g.rowHeight() > height()) { g.centreOnKey(note.key()); }
	emit m_editor->stateChanged();
}

QString NoteCanvas::accessibleValue() const
{
	if (m_state.clip == nullptr) { return tr("No clip open"); }
	const QString counts = notekeys::summary(m_state.clip->notes());
	const Note* cursor = cursorNote();
	return cursor != nullptr ? notekeys::describe(*cursor) + QStringLiteral(". ") + counts : counts;
}

} // namespace lmms::gui
