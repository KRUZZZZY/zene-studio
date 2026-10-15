/*
 * NoteCanvasGestures.cpp - the note editor's strokes (paint, mute, slice, play) and note colours
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
#include <QPainter>

#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

//! A painted note's length: New Note Length when one is set, else the last drawn length, as a
//! click draws (drawSpan) - but never past its grid cell, so a stroke's notes do not overlap.
int paintLength(const NoteEditorState& state)
{
	const int wanted = state.newLength > 0 ? state.newLength : state.lastLength;
	return std::max(1, std::min(wanted, std::max(1, state.gridTicks)));
}

//! Marks the cell of @a key at @a start for a note, once: a stroke never paints the same cell twice.
void appendPaintCell(std::vector<std::pair<int, int>>& painted, int key, int start)
{
	if (key < 0) { return; }
	const bool taken = std::any_of(painted.begin(), painted.end(),
		[key, start](const std::pair<int, int>& cell) { return cell.first == key && cell.second == start; });
	if (!taken) { painted.emplace_back(key, start); }
}

} // namespace

int NoteCanvas::scaleKey(int key) const
{
	return m_state.keepInScale && key >= 0 ? m_state.grid.nearestInScale(key) : key;
}

void NoteCanvas::pressPaint()
{
	m_drag = Drag::Paint;
	m_painted.clear();
	extendPaint(m_pressPos);
	m_auditionKey = scaleKey(m_pressKey);
	if (m_auditionKey >= 0) { m_editor->auditionPress(m_auditionKey, m_state.lastVelocity); }
}

void NoteCanvas::extendPaint(const QPoint& pos)
{
	// One note per grid cell the pointer crosses, on the row under it (a cell is painted once).
	const int key = scaleKey(m_state.grid.keyAtY(pos.y()));
	const int start = std::max(0, NoteGrid::snapDown(static_cast<int>(m_state.grid.tickAtX(pos.x())), m_state.gridTicks));
	appendPaintCell(m_painted, key, start);
}

void NoteCanvas::paintAlong(const QPoint& from, const QPoint& to)
{
	// Qt delivers a fast drag sparsely, and a cell narrower than a pixel can lie between two pixel samples:
	// every time column the path crosses is painted, on the row where the path enters it; every key row it
	// crosses is painted too, in the column under it; and the release point closes the last segment.
	const int cellTicks = std::max(1, m_state.gridTicks);
	const double fromTick = m_state.grid.tickAtX(from.x());
	const double toTick = m_state.grid.tickAtX(to.x());
	const int fromCell = std::max(0, NoteGrid::snapDown(static_cast<int>(fromTick), cellTicks));
	const int toCell = std::max(0, NoteGrid::snapDown(static_cast<int>(toTick), cellTicks));
	extendPaint(from);
	const int step = toCell > fromCell ? cellTicks : -cellTicks;
	for (int cell = fromCell + step; cell != toCell + step; cell += step)
	{
		// The path enters a cell going right at its left edge, going left at its right edge.
		const double boundary = step > 0 ? cell : cell + cellTicks;
		const double progress = (boundary - fromTick) / (toTick - fromTick);
		const int y = static_cast<int>(std::lround(from.y() + (to.y() - from.y()) * progress));
		appendPaintCell(m_painted, scaleKey(m_state.grid.keyAtY(y)), cell);
	}
	// Pitch: every pixel row strictly between the two points is sampled, in the column the path is in there, so a
	// stroke that stays in one column (a sparse vertical one) still paints each key row it crosses.
	const int rowSpan = std::abs(to.y() - from.y());
	const int rowDir = to.y() > from.y() ? 1 : -1;
	for (int i = 1; i < rowSpan; ++i)
	{
		const double tick = fromTick + (toTick - fromTick) * i / rowSpan;
		const int cell = std::max(0, NoteGrid::snapDown(static_cast<int>(tick), cellTicks));
		appendPaintCell(m_painted, scaleKey(m_state.grid.keyAtY(from.y() + rowDir * i)), cell);
	}
	extendPaint(to);
}

void NoteCanvas::strokeAlong(const QPoint& from, const QPoint& to)
{
	// Qt coalesces a fast drag into sparse events: every note the stroke's path crosses counts, not
	// only the one under each event (a 2-pixel step, finer than a note can be drawn).
	if (m_state.clip == nullptr) { return; }
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = visibleRange();
	const QPoint path = to - from;
	const int steps = std::max(1, std::max(std::abs(path.x()), std::abs(path.y())) / 2);
	for (int i = 1; i <= steps; ++i)
	{
		const QPoint at = from + path * i / steps;
		bool hitRectangle = false;
		for (int index = last - 1; index >= first; --index)
		{
			const Note* note = notes[static_cast<std::size_t>(index)];
			if (noteRect(*note).contains(at))
			{
				hitRectangle = true;
				markStroke(note);
			}
		}
		// Keep noteAt's wider target for narrow notes where the stroke misses every exact rectangle.
		if (!hitRectangle) { markStroke(noteAt(at)); }
	}
}

void NoteCanvas::commitPaint()
{
	if (m_painted.empty()) { return; }
	const int length = paintLength(m_state);
	QJsonArray notes;
	for (const auto& [key, start] : m_painted)
	{
		notes.append(QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("position"), start},
			{QStringLiteral("length"), length}, {QStringLiteral("velocity"), m_state.lastVelocity}});
	}
	m_painted.clear();
	m_editor->run(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("notes"), notes}});
}

void NoteCanvas::commitMute()
{
	// Every note the stroke crossed takes the opposite of the first one's state; so does the rest of the
	// selection when the stroke started on a selected note (one note.edit, one undo step).
	if (m_state.clip == nullptr || m_muteTo < 0) { return; }
	const Note* anchor = noteById(m_anchorId);
	const bool withSelection = anchor != nullptr && anchor->selected();
	QJsonArray edits;
	for (const Note* note : m_state.clip->notes())
	{
		if (stroked(*note) || (withSelection && note->selected()))
		{
			edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("muted"), m_muteTo}});
		}
	}
	m_strokeIds.clear();
	if (edits.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

int NoteCanvas::sliceTick() const
{
	return std::max(0, snapTicks(static_cast<int>(std::lround(m_state.grid.tickAtX(m_currentPos.x())))));
}

void NoteCanvas::commitSlice()
{
	// The line stands at the pointer's (snapped) tick and spans the rows from the press to the release: every
	// note it crosses is cut there, as one note.slice.
	if (m_state.clip == nullptr) { return; }
	const int at = sliceTick();
	const int top = std::min(m_pressPos.y(), m_currentPos.y());
	const int bottom = std::max(m_pressPos.y(), m_currentPos.y());
	QJsonArray ids;
	const NoteVector& notes = m_state.clip->notes();
	const auto [first, last] = visibleRange();
	for (int i = first; i < last; ++i)
	{
		const Note* note = notes[static_cast<std::size_t>(i)];
		const QRect r = noteRect(*note);
		const bool crossed = !r.isEmpty() && r.bottom() >= top && r.top() <= bottom;
		if (crossed && note->pos().getTicks() < at && note->endPos().getTicks() > at) { ids.append(control::noteIdOf(note)); }
	}
	if (ids.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.slice"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("at"), at},
		{QStringLiteral("notes"), ids}});
}

void NoteCanvas::scrubTo(const QPoint& pos)
{
	// The Play tool: a drag sounds each key it reaches, the previous one released first.
	const Note* note = noteAt(pos);
	const int key = note != nullptr ? note->key() : m_state.grid.keyAtY(pos.y());
	if (key < 0 || key == m_auditionKey) { return; }
	if (m_auditionKey >= 0) { m_editor->auditionRelease(m_auditionKey); }
	m_auditionKey = key;
	m_editor->auditionPress(key, note != nullptr ? static_cast<int>(note->getVolume()) : m_state.lastVelocity);
}

void NoteCanvas::paintPaintPreview(QPainter& p) const
{
	QColor fill = tokens::velocityColor(m_state.lastVelocity);
	fill.setAlpha(170);
	const int length = paintLength(m_state);
	for (const auto& [key, start] : m_painted) { p.fillRect(rectFor(key, start, start + length), fill); }
}

QColor NoteCanvas::noteFill(const Note& note) const
{
	if (note.muted()) { return tokens::palette().textMuted; }  // a muted note is grey whatever the mode
	switch (m_state.colorMode)
	{
	case NoteColor::Pitch: return QColor::fromHsv((note.key() % 12) * 30, 150, 220);
	case NoteColor::Track:
	{
		const MidiClip* clip = m_state.clip.data();
		if (clip != nullptr && clip->color().has_value()) { return *clip->color(); }
		if (clip != nullptr && clip->getTrack() != nullptr && clip->getTrack()->color().has_value())
		{
			return *clip->getTrack()->color();
		}
		return tokens::palette().accent;
	}
	case NoteColor::Velocity: break;
	}
	return tokens::velocityColor(static_cast<int>(note.getVolume()));
}

QString NoteCanvas::noteLabel(const Note& note) const
{
	// 0.5.0 ED-088: a sung note shows its syllable.
	if (const QString lyric = lyricText(note.conditions().lyric); !lyric.isEmpty()) { return lyric; }
	const QString name = m_editor->keyLabel(note.key());
	return name.isEmpty() ? NoteGrid::keyName(note.key()) : name;
}

} // namespace lmms::gui
