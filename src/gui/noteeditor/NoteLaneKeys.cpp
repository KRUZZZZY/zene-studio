/*
 * NoteLaneKeys.cpp - the expression and controller lanes from the keyboard
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

#include "NoteLaneKeys.h"

#include <algorithm>
#include <vector>

#include <QCoreApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QPointer>
#include <QSet>

#include "AccessibleCanvas.h"
#include "AccessibleWidgets.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "Note.h"
#include "NoteEditor.h"
#include "NoteKeyNav.h"
#include "NoteLanes.h"

namespace lmms::gui::lanekeys
{

namespace
{

constexpr int kCoarse = 10;  //!< Page Up / Page Down on the expression lane


Qt::KeyboardModifiers modsOf(const QKeyEvent* event) { return event->modifiers() & ~Qt::KeypadModifier; }

bool listKey(const QKeyEvent* event)
{
	return event->key() == Qt::Key_L && modsOf(event) == (Qt::ControlModifier | Qt::ShiftModifier);
}

//! What both lanes share: the lane, its editor, and the filter that hands it the keys it claims - over a window
//! shortcut too (the arranger's Alt and Ctrl arrows are window shortcuts) while the lane has the focus.
class LaneKeys : public QObject
{
public:
	LaneKeys(QWidget* lane, NoteEditor* editor)
		: QObject(lane)
		, m_lane(lane)
		, m_editor(editor)
	{
		lane->installEventFilter(this);
	}

protected:
	virtual bool claims(const QKeyEvent* event) const = 0;
	virtual void handle(const QKeyEvent* event) = 0;

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		const bool keyEvent = event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress;
		if (watched != m_lane || !keyEvent || m_editor == nullptr || m_editor->clip() == nullptr) { return false; }
		const auto* key = static_cast<QKeyEvent*>(event);
		if (!claims(key)) { return false; }
		event->accept();
		if (event->type() == QEvent::KeyPress)
		{
			handle(key);
			m_lane->update();
		}
		return true;
	}

	NoteEditorState& state() const { return m_editor->state(); }
	int xOfTick(double tick) const { return static_cast<int>(state().grid.xOfTick(tick)); }
	void openList(const QString& surface) const
	{
		m_editor->run(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), surface}});
	}

	QWidget* m_lane;
	QPointer<NoteEditor> m_editor;
};

//! The expression lane: a slider over the selected notes, moved note by note.
class ExpressionKeys : public LaneKeys
{
public:
	ExpressionKeys(QWidget* lane, NoteEditor* editor)
		: LaneKeys(lane, editor)
	{
		a11y::describeCanvas(lane, QAccessible::Slider, [this] { return value(); });
		a11y::CanvasKeyCursor::of(lane)->setMark([this] { return mark(); });
	}

protected:
	bool claims(const QKeyEvent* event) const override
	{
		const Qt::KeyboardModifiers mods = modsOf(event) & ~Qt::ShiftModifier;
		const int key = event->key();
		const bool plainKey = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down
			|| key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Home || key == Qt::Key_End;
		return (plainKey && mods == Qt::NoModifier) || listKey(event);
	}

	void handle(const QKeyEvent* event) override
	{
		switch (event->key())
		{
			case Qt::Key_Left: move(notekeys::Step::Previous, event); break;
			case Qt::Key_Right: move(notekeys::Step::Next, event); break;
			case Qt::Key_Home: move(notekeys::Step::First, event); break;
			case Qt::Key_End: move(notekeys::Step::Last, event); break;
			case Qt::Key_Up: change(1); break;
			case Qt::Key_Down: change(-1); break;
			case Qt::Key_PageUp: change(kCoarse); break;
			case Qt::Key_PageDown: change(-kCoarse); break;
			default: openList(QStringLiteral("notes")); break;
		}
	}

private:
	const Note* cursor() const
	{
		const NoteVector& notes = m_editor->clip()->notes();
		for (const Note* note : notes)
		{
			if (note->id() == m_cursorId && note->selected()) { return note; }
		}
		return notekeys::firstSelected(notes);
	}

	void move(notekeys::Step step, const QKeyEvent* event)
	{
		const Note* to = notekeys::neighbour(m_editor->clip()->notes(), cursor(), step);
		if (to == nullptr) { a11y::announceStatus(m_lane, tr("No note there")); return; }
		m_cursorId = to->id();
		QJsonArray ids{control::noteIdOf(to)};
		if (modsOf(event).testFlag(Qt::ShiftModifier))
		{
			for (const Note* note : m_editor->clip()->notes())
			{
				if (note->selected() && note != to) { ids.append(control::noteIdOf(note)); }
			}
		}
		m_editor->run(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("notes"), ids}});
	}

	//! Every selected note's value @a delta up, in ONE note.edit - the MPE axes included, which
	//! note.edit carries as timbre/pressure/pitch. One key press is one undo step, and a selected
	//! note the cursor is not on changes with the rest (Q050-099).
	void change(int delta)
	{
		const NoteLane lane = state().lane;
		const QString field = noteEditField(lane);
		QJsonArray edits;
		for (const Note* note : m_editor->clip()->notes())
		{
			if (!note->selected()) { continue; }
			const int value = notelanes::clamp(lane, notelanes::valueOf(*note, lane) + delta);
			const double stored = lane == NoteLane::Chance || lane == NoteLane::Spread ? value / 100.0 : value;
			edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {field, stored}});
		}
		if (!edits.isEmpty())
		{
			m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
		}
	}

	//! The note.edit field a lane writes: the MPE lanes became batch-editable with note.edit in Q050-099.
	static QString noteEditField(NoteLane lane)
	{
		switch (lane)
		{
			case NoteLane::Velocity: return QStringLiteral("velocity");
			case NoteLane::Pan: return QStringLiteral("pan");
			case NoteLane::Chance: return QStringLiteral("probability");
			case NoteLane::Release: return QStringLiteral("release_velocity");
			case NoteLane::Spread: return QStringLiteral("velocity_spread");
			case NoteLane::Repeats: return QStringLiteral("repeats");
			case NoteLane::Timbre: return QStringLiteral("timbre");
			case NoteLane::Pressure: return QStringLiteral("pressure");
			case NoteLane::Pitch: return QStringLiteral("pitch");
		}
		return QStringLiteral("velocity");
	}

	QString value() const
	{
		if (m_editor == nullptr || m_editor->clip() == nullptr) { return tr("No clip open"); }
		const NoteLaneSpec spec = notelanes::spec(state().lane);
		const Note* note = cursor();
		if (note == nullptr) { return tr("%1: no note selected").arg(spec.title); }
		int selected = 0;
		for (const Note* candidate : m_editor->clip()->notes()) { selected += candidate->selected() ? 1 : 0; }
		return tr("%1 %2%3, %4 selected - %5").arg(spec.title).arg(notelanes::valueOf(*note, state().lane))
			.arg(spec.unit).arg(selected).arg(notekeys::describe(*note));
	}

	QRect mark() const
	{
		const Note* note = m_editor != nullptr && m_editor->clip() != nullptr ? cursor() : nullptr;
		if (note == nullptr) { return {}; }
		return QRect(xOfTick(note->pos().getTicks()) - 5, 1, 12, m_lane->height() - 2);
	}

	int m_cursorId = -1;
};

//! The controller lane: a cursor on the clip's time axis and the curve's points around it.
class ControllerKeys : public LaneKeys
{
public:
	ControllerKeys(QWidget* lane, NoteEditor* editor)
		: LaneKeys(lane, editor)
	{
		a11y::describeCanvas(lane, QAccessible::Slider, [this] { return value(); });
		a11y::CanvasKeyCursor::of(lane)->setMark([this] { return QRect(xOfTick(m_tick) - 4, 1, 9, m_lane->height() - 2); });
	}

protected:
	bool claims(const QKeyEvent* event) const override
	{
		static const QSet<int> plainKeys{Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down, Qt::Key_PageUp, Qt::Key_PageDown,
			Qt::Key_Home, Qt::Key_End, Qt::Key_Insert, Qt::Key_Delete, Qt::Key_Backspace};
		const Qt::KeyboardModifiers mods = modsOf(event);
		const bool arrow = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
		return (mods == Qt::NoModifier && plainKeys.contains(event->key())) || (arrow && mods == Qt::ControlModifier) || listKey(event);
	}

	void handle(const QKeyEvent* event) override
	{
		if (move(event->key(), modsOf(event).testFlag(Qt::ControlModifier))) { return; }
		switch (event->key())
		{
			case Qt::Key_Up: change(1); break;
			case Qt::Key_Down: change(-1); break;
			case Qt::Key_PageUp: change(maximum() / 8 + 1); break;
			case Qt::Key_PageDown: change(-(maximum() / 8 + 1)); break;
			case Qt::Key_Insert: write(m_tick, currentValue(), false); break;
			case Qt::Key_L: openList(QStringLiteral("controller")); break;
			default: write(m_tick, 0, true); break;  // Delete, Backspace
		}
	}

private:
	//! The cursor keys; false for any other.
	bool move(int key, bool ctrl)
	{
		switch (key)
		{
			case Qt::Key_Left: ctrl ? toPoint(false) : step(-1); return true;
			case Qt::Key_Right: ctrl ? toPoint(true) : step(1); return true;
			case Qt::Key_Home: m_tick = 0; return true;
			case Qt::Key_End: m_tick = std::max(0, static_cast<int>(m_editor->clip()->length().getTicks()) - 1); return true;
			default: return false;
		}
	}

	const ControllerLane* lane() const
	{
		ControllerKind kind = ControllerKind::ControlChange;
		int controller = 0;
		if (!ControllerLane::fromId(state().controllerLane, &kind, &controller)) { return nullptr; }
		return m_editor->clip()->controllers().lane(kind, controller);
	}

	int maximum() const { return state().controllerLane == QLatin1String("pitchbend") ? 16383 : 127; }

	int currentValue() const
	{
		const ControllerLane* shown = lane();
		return shown != nullptr && !shown->points.empty() ? shown->valueAt(m_tick) : (maximum() + 1) / 2;
	}

	void step(int direction)
	{
		const int length = static_cast<int>(m_editor->clip()->length().getTicks());
		m_tick = std::clamp(m_tick + direction * std::max(1, state().gridTicks), 0, std::max(0, length - 1));
	}

	void toPoint(bool forward)
	{
		const ControllerLane* shown = lane();
		if (shown == nullptr) { return; }
		const ControllerPoint* best = nullptr;
		for (const ControllerPoint& point : shown->points)
		{
			const bool beyond = forward ? point.tick > m_tick : point.tick < m_tick;
			if (beyond && (best == nullptr || (forward ? point.tick < best->tick : point.tick > best->tick))) { best = &point; }
		}
		if (best == nullptr) { a11y::announceStatus(m_lane, tr("No point there")); return; }
		m_tick = static_cast<int>(best->tick);
	}

	void change(int delta)
	{
		write(m_tick, std::clamp(currentValue() + delta, 0, maximum()), false);
	}

	//! The lane with the point at @a tick set to @a value (or, with @a remove, taken out): ONE clip.controllers_set.
	void write(int tick, int value, bool remove)
	{
		if (state().controllerLane.isEmpty()) { return; }
		QJsonArray points;
		bool placed = false;
		const ControllerLane* shown = lane();
		const std::vector<ControllerPoint> existing = shown != nullptr ? shown->points : std::vector<ControllerPoint>{};
		for (const ControllerPoint& point : existing)
		{
			if (!placed && !remove && point.tick >= tick) { points.append(pointJson(tick, value)); placed = true; }
			if (point.tick != tick) { points.append(pointJson(static_cast<int>(point.tick), point.value)); }
		}
		if (!placed && !remove) { points.append(pointJson(tick, value)); }
		m_editor->run(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), m_editor->clipId()},
			{QStringLiteral("lane"), state().controllerLane}, {QStringLiteral("points"), points}});
	}

	static QJsonObject pointJson(int tick, int value)
	{
		return {{QStringLiteral("tick"), tick}, {QStringLiteral("value"), value}};
	}

	bool onPoint() const
	{
		const ControllerLane* shown = lane();
		return shown != nullptr && std::any_of(shown->points.begin(), shown->points.end(),
			[this](const ControllerPoint& point) { return point.tick == m_tick; });
	}

	QString value() const
	{
		if (m_editor == nullptr || m_editor->clip() == nullptr) { return tr("No clip open"); }
		if (state().controllerLane.isEmpty()) { return tr("No controller lane chosen"); }
		return tr("%1 at %2: %3%4").arg(state().controllerLane, musicaltime::positionText(m_tick)).arg(currentValue())
			.arg(onPoint() ? tr(", a point") : QString());
	}

	int m_tick = 0;
};

} // namespace

void attachExpressionLane(QWidget* lane, NoteEditor* editor)
{
	new ExpressionKeys(lane, editor);
}

void attachControllerLane(QWidget* lane, NoteEditor* editor)
{
	new ControllerKeys(lane, editor);
}

} // namespace lmms::gui::lanekeys
