/*
 * NoteCanvasGestures.cpp - the note editor's paint tool, velocity drag and note colours
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

#include <QJsonArray>
#include <QPainter>

#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "Track.h"
#include "volume.h"

namespace lmms::gui
{

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
	if (key < 0) { return; }
	const bool taken = std::any_of(m_painted.begin(), m_painted.end(),
		[start](const std::pair<int, int>& cell) { return cell.second == start; });
	if (!taken) { m_painted.emplace_back(key, start); }
}

void NoteCanvas::commitPaint()
{
	if (m_painted.empty()) { return; }
	const int length = std::max(1, std::min(m_state.lastLength, std::max(1, m_state.gridTicks)));
	QJsonArray notes;
	for (const auto& [key, start] : m_painted)
	{
		notes.append(QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("position"), start},
			{QStringLiteral("length"), length}, {QStringLiteral("velocity"), m_state.lastVelocity}});
	}
	m_painted.clear();
	m_editor->run(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("notes"), notes}});
}

void NoteCanvas::pressMute(const Note* note)
{
	// The note clicked takes the opposite of its state; so does the rest of the selection when it
	// is part of one (one note.edit, one undo step).
	if (note == nullptr) { return; }
	const bool mute = !note->muted();
	std::vector<Note*> targets = note->selected() ? selectedNotes() : std::vector<Note*>{};
	if (targets.empty()) { targets.push_back(noteById(note->id())); }
	QJsonArray edits;
	for (const Note* target : targets)
	{
		if (target != nullptr) { edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(target)}, {QStringLiteral("muted"), mute ? 1 : 0}}); }
	}
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::pressVelocity(Note* note)
{
	// Alt-drag on a note: up louder, down softer, for every selected note at once.
	if (!note->selected()) { selectOnly({note}, false); }
	m_anchorId = note->id();
	m_drag = Drag::Velocity;
}

int NoteCanvas::velocityDelta() const { return m_pressPos.y() - m_currentPos.y(); }

void NoteCanvas::commitVelocity()
{
	const int delta = velocityDelta();
	if (delta == 0) { return; }
	QJsonArray edits;
	for (Note* note : selectedNotes())
	{
		const int velocity = std::clamp(static_cast<int>(note->getVolume()) + delta, int{MinVolume}, int{MaxVolume});
		if (note->id() == m_anchorId) { m_state.lastVelocity = velocity; }
		edits.append(QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("velocity"), velocity}});
	}
	if (edits.isEmpty()) { return; }
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"), edits}});
}

void NoteCanvas::paintPaintPreview(QPainter& p) const
{
	QColor fill = tokens::velocityColor(m_state.lastVelocity);
	fill.setAlpha(170);
	const int length = std::max(1, std::min(m_state.lastLength, std::max(1, m_state.gridTicks)));
	for (const auto& [key, start] : m_painted) { p.fillRect(rectFor(key, start, start + length), fill); }
}

void NoteCanvas::paintVelocityPreview(QPainter& p) const
{
	const tokens::Palette& pal = tokens::palette();
	p.setFont(tokens::font(tokens::TextRole::Caption));
	for (const Note* note : selectedNotes())
	{
		const int velocity = std::clamp(static_cast<int>(note->getVolume()) + velocityDelta(), int{MinVolume}, int{MaxVolume});
		const QRect r = noteRect(*note);
		p.fillRect(r, tokens::velocityColor(velocity));
		p.setPen(pal.text);
		p.drawText(r.adjusted(2, -14, 40, 0), Qt::AlignTop | Qt::AlignLeft, QString::number(velocity));
	}
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
	const QString name = m_editor->keyLabel(note.key());
	return name.isEmpty() ? NoteGrid::keyName(note.key()) : name;
}

} // namespace lmms::gui
