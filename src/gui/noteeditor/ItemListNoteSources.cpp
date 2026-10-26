/*
 * ItemListNoteSources.cpp - the note area's and the controller lane's editable lists
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
#include <vector>

#include <QJsonArray>
#include <QPointer>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ItemList.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "NoteKeyNav.h"
#include "NoteLanes.h"
#include "Song.h"

namespace lmms::gui::itemlists
{

namespace
{

//! What both note lists share: the editor, its clip, and following both - another clip opened, the clip edited
//! (by the canvas, the socket, undo), the lane chooser changed.
class EditorSource : public ItemListSource
{
public:
	EditorSource(NoteEditor* editor, QObject* parent)
		: ItemListSource(parent)
		, m_editor(editor)
	{
		if (editor != nullptr)
		{
			connect(editor, &NoteEditor::clipChanged, this, [this] { follow(); });
			connect(editor, &NoteEditor::stateChanged, this, &ItemListSource::changed);
		}
		follow();
	}

protected:
	MidiClip* clip() const { return m_editor != nullptr ? m_editor->clip() : nullptr; }
	QString clipId() const { return m_editor != nullptr ? m_editor->clipId() : QString(); }
	int grid() const { return m_editor != nullptr ? std::max(1, m_editor->state().gridTicks) : 1; }

private:
	void follow()
	{
		if (m_clip != nullptr) { disconnect(m_clip, nullptr, this, nullptr); }
		m_clip = clip();
		if (m_clip != nullptr) { connect(m_clip, &MidiClip::dataChanged, this, &ItemListSource::changed); }
		emit changed();
	}

protected:
	QPointer<NoteEditor> m_editor;

private:
	QPointer<MidiClip> m_clip;
};

//! The clip's notes, one row each in the keyboard's order: start, pitch, length, velocity and - when the expression
//! lane shows another value - that value.
class NotesSource : public EditorSource
{
public:
	NotesSource(NoteEditor* editor, QObject* parent)
		: EditorSource(editor, parent)
	{
		connect(this, &ItemListSource::changed, this, [this] { m_dirty = true; });
	}

	QString title() const override
	{
		return clip() != nullptr ? tr("Notes of %1").arg(clip()->name().isEmpty() ? tr("the clip") : clip()->name()) : tr("No clip open");
	}

	QList<ItemColumn> columns() const override
	{
		QList<ItemColumn> all{{QStringLiteral("position"), tr("Start"), ItemColumn::Kind::Position, 0, static_cast<int>(MaxSongLength), true},
			{QStringLiteral("key"), tr("Pitch"), ItemColumn::Kind::Key, 0, NumKeys - 1, true},
			{QStringLiteral("length"), tr("Length"), ItemColumn::Kind::Length, 1, static_cast<int>(MaxSongLength), true},
			{QStringLiteral("velocity"), tr("Velocity"), ItemColumn::Kind::Number, 0, 200, true}};
		const NoteLane lane = m_editor != nullptr ? m_editor->state().lane : NoteLane::Velocity;
		if (lane != NoteLane::Velocity)
		{
			const NoteLaneSpec spec = notelanes::spec(lane);
			all.append({spec.id, spec.title, ItemColumn::Kind::Number, spec.minimum, spec.maximum, true});
		}
		return all;
	}

	int rowCount() const override { return clip() != nullptr ? static_cast<int>(clip()->notes().size()) : 0; }
	QString rowId(int row) const override { return control::noteIdOf(note(row)); }

	int value(int row, int column) const override
	{
		const Note* n = note(row);
		switch (column)
		{
			case 0: return static_cast<int>(n->pos().getTicks());
			case 1: return n->key();
			case 2: return static_cast<int>(n->length().getTicks());
			case 3: return static_cast<int>(n->getVolume());
			default: return notelanes::valueOf(*n, m_editor->state().lane);
		}
	}

	bool setValue(int row, int column, int value) override
	{
		if (clip() == nullptr || row >= rowCount()) { return false; }
		const QList<ItemColumn> all = columns();
		if (column >= all.size()) { return false; }
		const ItemColumn& c = all.at(column);
		if (column == 4)
		{
			const notelanes::Write write = notelanes::writeFor(m_editor->state().lane, clipId(), rowId(row), value);
			return run(write.command, write.args);
		}
		const QJsonObject edit{{QStringLiteral("note"), rowId(row)}, {c.id, std::clamp(value, c.minimum, c.maximum)}};
		return run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clipId()}, {QStringLiteral("edits"), QJsonArray{edit}}});
	}

	int step(int column) const override { return column == 0 || column == 2 ? grid() : 1; }
	bool canInsert() const override { return true; }
	bool canRemove() const override { return true; }

	bool insertAfter(int row) override
	{
		if (clip() == nullptr) { refuse(tr("no clip is open")); return false; }
		const NoteEditorState& state = m_editor->state();
		const Note* after = row >= 0 && row < rowCount() ? note(row) : nullptr;
		const int start = after != nullptr ? static_cast<int>(after->pos().getTicks() + after->length().getTicks()) : 0;
		const QJsonObject args{{QStringLiteral("clip"), clipId()}, {QStringLiteral("key"), after != nullptr ? after->key() : DefaultKey},
			{QStringLiteral("position"), start}, {QStringLiteral("length"), state.lastLength}, {QStringLiteral("velocity"), state.lastVelocity}};
		const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("note.add"), args);
		if (!added.ok) { refuse(added.errorMessage); return false; }
		return run(QStringLiteral("note.select"), {{QStringLiteral("clip"), clipId()},
			{QStringLiteral("notes"), QJsonArray{added.result.value(QStringLiteral("note"))}}});
	}

	bool remove(const QList<int>& rows) override
	{
		return run(QStringLiteral("note.remove_many"), {{QStringLiteral("clip"), clipId()}, {QStringLiteral("notes"), idsOf(rows)}});
	}

	bool tracksSelection() const override { return true; }

	QList<int> selectedRows() const override
	{
		QList<int> rows;
		for (int row = 0; row < rowCount(); ++row)
		{
			if (note(row)->selected()) { rows.append(row); }
		}
		return rows;
	}

	void select(const QList<int>& rows) override
	{
		if (clip() != nullptr) { run(QStringLiteral("note.select"), {{QStringLiteral("clip"), clipId()}, {QStringLiteral("notes"), idsOf(rows)}}); }
	}

private:
	//! The note on @a row: the rows are the clip's notes in the keyboard's order (NoteKeyNav.h). The order is kept as
	//! indices into the clip's list, sorted again after every change - never as note pointers, which a change made
	//! between two re-reads could free (BUGS 11.27's lesson); a stale index only names another note until then.
	const Note* note(int row) const
	{
		const NoteVector& notes = clip()->notes();
		if (m_dirty || m_order.size() != notes.size())
		{
			m_order.resize(notes.size());
			for (std::size_t i = 0; i < m_order.size(); ++i) { m_order[i] = i; }
			std::sort(m_order.begin(), m_order.end(), [&notes](std::size_t a, std::size_t b) { return notekeys::before(notes[a], notes[b]); });
			m_dirty = false;
		}
		return notes[m_order.at(static_cast<std::size_t>(row))];
	}

	QJsonArray idsOf(const QList<int>& rows) const
	{
		QJsonArray ids;
		for (const int row : rows) { ids.append(rowId(row)); }
		return ids;
	}

	mutable std::vector<std::size_t> m_order;
	mutable bool m_dirty = true;
};

//! The controller lane's points: their place and value; the lane is the one the editor's chooser shows.
class ControllerSource : public EditorSource
{
public:
	using EditorSource::EditorSource;

	QString title() const override
	{
		return laneId().isEmpty() ? tr("No controller lane chosen") : tr("Points of %1").arg(laneId());
	}

	QList<ItemColumn> columns() const override
	{
		return {{QStringLiteral("tick"), tr("Position"), ItemColumn::Kind::Position, 0, static_cast<int>(MaxSongLength), true},
			{QStringLiteral("value"), tr("Value"), ItemColumn::Kind::Number, 0, maximum(), true}};
	}

	int rowCount() const override { return static_cast<int>(points().size()); }
	QString rowId(int row) const override { return QString::number(points().at(static_cast<std::size_t>(row)).tick); }

	int value(int row, int column) const override
	{
		const ControllerPoint& point = points().at(static_cast<std::size_t>(row));
		return column == 0 ? static_cast<int>(point.tick) : point.value;
	}

	bool setValue(int row, int column, int value) override
	{
		std::vector<ControllerPoint> all = points();
		if (row >= static_cast<int>(all.size())) { return false; }
		ControllerPoint& point = all[static_cast<std::size_t>(row)];
		if (column == 0)
		{
			const int tick = std::max(0, value);
			if (std::any_of(all.cbegin(), all.cend(), [&point, tick](const ControllerPoint& other)
				{ return &other != &point && other.tick == tick; }))
			{
				refuse(tr("a controller point already exists at that position"));
				return false;
			}
			point.tick = tick;
		}
		else { point.value = std::clamp(value, 0, maximum()); }
		return write(all);
	}

	int step(int column) const override { return column == 0 ? grid() : 1; }
	bool canInsert() const override { return true; }
	bool canRemove() const override { return true; }

	bool insertAfter(int row) override
	{
		if (laneId().isEmpty()) { refuse(tr("choose a controller lane first")); return false; }
		std::vector<ControllerPoint> all = points();
		const int tick = row >= 0 && row < static_cast<int>(all.size()) ? static_cast<int>(all[static_cast<std::size_t>(row)].tick) + grid() : 0;
		if (std::any_of(all.cbegin(), all.cend(), [tick](const ControllerPoint& point) { return point.tick == tick; }))
		{
			refuse(tr("a controller point already exists at that position"));
			return false;
		}
		const ControllerLane* shown = lane();
		all.push_back({tick, shown != nullptr && !shown->points.empty() ? shown->valueAt(tick) : (maximum() + 1) / 2});
		return write(all);
	}

	bool remove(const QList<int>& rows) override
	{
		std::vector<ControllerPoint> kept;
		const std::vector<ControllerPoint> all = points();
		for (int row = 0; row < static_cast<int>(all.size()); ++row)
		{
			if (!rows.contains(row)) { kept.push_back(all[static_cast<std::size_t>(row)]); }
		}
		return write(kept);
	}

private:
	QString laneId() const { return m_editor != nullptr && clip() != nullptr ? m_editor->state().controllerLane : QString(); }
	int maximum() const { return laneId() == QLatin1String("pitchbend") ? 16383 : 127; }

	const ControllerLane* lane() const
	{
		ControllerKind kind = ControllerKind::ControlChange;
		int controller = 0;
		if (!ControllerLane::fromId(laneId(), &kind, &controller)) { return nullptr; }
		return clip()->controllers().lane(kind, controller);
	}

	std::vector<ControllerPoint> points() const
	{
		const ControllerLane* shown = lane();
		return shown != nullptr ? shown->points : std::vector<ControllerPoint>{};
	}

	//! The whole lane as ONE clip.controllers_set (the command sorts the points and keeps one per tick).
	bool write(const std::vector<ControllerPoint>& all)
	{
		QJsonArray json;
		for (const ControllerPoint& point : all)
		{
			json.append(QJsonObject{{QStringLiteral("tick"), static_cast<int>(point.tick)}, {QStringLiteral("value"), point.value}});
		}
		return run(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clipId()}, {QStringLiteral("lane"), laneId()},
			{QStringLiteral("points"), json}});
	}
};

} // namespace

ItemListSource* notes(NoteEditor* editor, QObject* parent)
{
	return new NotesSource(editor, parent);
}

ItemListSource* controllerPoints(NoteEditor* editor, QObject* parent)
{
	return new ControllerSource(editor, parent);
}

} // namespace lmms::gui::itemlists
