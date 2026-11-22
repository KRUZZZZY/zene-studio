/*
 * ItemListSurfaceSources.cpp - the XY pad's, the comp strip's and the arranger's editable lists
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
#include <QPointer>
#include <QTimer>

#include "ArrangerClipKeys.h"
#include "Clip.h"
#include "CompSwipeStrip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ItemList.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "XyPad.h"

namespace lmms::gui::itemlists
{

namespace
{

constexpr int ClipPollMs = 400;  //!< how often the clip list looks for a change (the arranger has no one signal for it)

const int PositionMax = static_cast<int>(MaxSongLength);

//! The XY pad's two axes: the macro each drives and its value, typed as a percentage.
class XySource : public ItemListSource
{
public:
	XySource(XyPadDialog* pad, QObject* parent)
		: ItemListSource(parent)
		, m_pad(pad)
	{
		connect(pad, &XyPadDialog::changed, this, &ItemListSource::changed);
	}

	QString title() const override { return tr("XY pad axes"); }

	QList<ItemColumn> columns() const override
	{
		return {{QStringLiteral("axis"), tr("Axis"), ItemColumn::Kind::Text, 0, 0, false},
			{QStringLiteral("macro"), tr("Macro"), ItemColumn::Kind::Text, 0, 0, false},
			{QStringLiteral("value"), tr("Value"), ItemColumn::Kind::Percent, 0, 100, true}};
	}

	int rowCount() const override { return m_pad != nullptr && !m_pad->macroId(false).isEmpty() ? 2 : 0; }
	QString rowId(int row) const override { return row == 0 ? QStringLiteral("x") : QStringLiteral("y"); }

	int value(int row, int) const override
	{
		const QPointF point = m_pad->area()->point();
		return static_cast<int>(std::lround((row == 0 ? point.x() : point.y()) * 100.0));
	}

	QString text(int row, int column) const override
	{
		if (column == 0) { return row == 0 ? tr("X (left-right)") : tr("Y (up-down)"); }
		return column == 1 ? m_pad->macroName(row == 1) : ItemListSource::text(row, column);
	}

	bool setValue(int row, int, int value) override
	{
		if (m_pad == nullptr) { return false; }
		const bool ran = run(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), m_pad->channel()},
			{QStringLiteral("macro"), m_pad->macroId(row == 1)}, {QStringLiteral("value"), std::clamp(value, 0, 100) / 100.0}});
		m_pad->refresh();  // the dot goes where the macros now stand
		return ran;
	}

	int step(int) const override { return 1; }

private:
	QPointer<XyPadDialog> m_pad;
};

//! The comp's segments: where each starts and ends and the lane it plays. A boundary moved gives the span it leaves to
//! the neighbouring segment's lane; Insert splits a segment, its second half on the next lane; Delete gives a segment
//! to its neighbour's lane - each one comp.select.
class CompSource : public ItemListSource
{
public:
	CompSource(CompSwipeStrip* strip, QObject* parent)
		: ItemListSource(parent)
		, m_strip(strip)
	{
		connect(strip, &CompSwipeStrip::stateChanged, this, &ItemListSource::changed);
	}

	QString title() const override { return tr("Comp segments"); }

	QList<ItemColumn> columns() const override
	{
		const std::vector<int>& lanes = m_strip->lanes();
		const int top = lanes.empty() ? 0 : *std::max_element(lanes.begin(), lanes.end());
		return {{QStringLiteral("begin"), tr("From"), ItemColumn::Kind::Position, 0, PositionMax, true},
			{QStringLiteral("end"), tr("To"), ItemColumn::Kind::Position, 0, PositionMax, true},
			{QStringLiteral("lane"), tr("Lane"), ItemColumn::Kind::Number, 0, top, true}};
	}

	int rowCount() const override { return m_strip != nullptr ? static_cast<int>(m_strip->segments().size()) : 0; }
	QString rowId(int row) const override { return QString::number(segment(row).begin); }

	int value(int row, int column) const override
	{
		const CompSwipeStrip::Segment& s = segment(row);
		return column == 0 ? s.begin : column == 1 ? s.end : s.lane;
	}

	bool setValue(int row, int column, int value) override
	{
		if (!hasRow(row)) { return false; }
		const CompSwipeStrip::Segment s = segment(row);
		if (column == 2) { return comp(s.begin, s.end, value); }
		const bool start = column == 0;
		const int old = start ? s.begin : s.end;
		if (value == old) { return true; }
		const bool grows = start ? value < old : value > old;
		if (grows) { return comp(std::min(value, old), std::max(value, old), s.lane); }
		const int neighbour = row + (start ? -1 : 1);
		if (neighbour < 0 || neighbour >= rowCount()) { refuse(tr("the comp's own start and end move only outwards")); return false; }
		return comp(std::min(value, old), std::max(value, old), segment(neighbour).lane);
	}

	bool canInsert() const override { return true; }
	bool canRemove() const override { return true; }

	bool insertAfter(int row) override
	{
		if (row < 0 || row >= rowCount()) { refuse(tr("choose the segment to split")); return false; }
		const CompSwipeStrip::Segment s = segment(row);
		if (s.end - s.begin < 2) { refuse(tr("the segment has no interior tick boundary to split at")); return false; }
		const std::vector<int>& lanes = m_strip->lanes();
		const auto at = std::find(lanes.begin(), lanes.end(), s.lane);
		const int next = at == lanes.end() || at + 1 == lanes.end() ? lanes.front() : *(at + 1);
		return comp(s.begin + (s.end - s.begin) / 2, s.end, next);
	}

	bool remove(const QList<int>& rows) override
	{
		if (rowCount() < 2) { refuse(tr("the comp's only segment cannot go")); return false; }
		struct Edit
		{
			int begin;
			int end;
			int lane;
		};
		const std::vector<CompSwipeStrip::Segment>& segments = m_strip->segments();
		std::vector<Edit> edits;
		edits.reserve(static_cast<std::size_t>(rows.size()));
		for (const int row : rows)
		{
			if (row < 0 || row >= static_cast<int>(segments.size())) { refuse(tr("choose a comp segment")); return false; }
			const CompSwipeStrip::Segment& s = segments.at(static_cast<std::size_t>(row));
			const int neighbour = row > 0 ? row - 1 : row + 1;
			edits.push_back({s.begin, s.end, segments.at(static_cast<std::size_t>(neighbour)).lane});
		}
		for (const Edit& edit : edits)
		{
			if (!comp(edit.begin, edit.end, edit.lane)) { return false; }
		}
		return true;
	}

private:
	bool hasRow(int row) const { return row >= 0 && row < rowCount(); }
	const CompSwipeStrip::Segment& segment(int row) const { return m_strip->segments().at(static_cast<std::size_t>(row)); }

	bool comp(int begin, int end, int lane)
	{
		if (end <= begin) { refuse(tr("the range is empty")); return false; }
		const bool ran = run(QStringLiteral("comp.select"), {{QStringLiteral("track"), m_strip->trackId()}, {QStringLiteral("begin"), begin},
			{QStringLiteral("end"), end}, {QStringLiteral("lane"), lane}});
		emit m_strip->ran(ran ? tr("Comp edited from the list") : lastError());  // the take panel re-reads comp.get_state
		return ran;
	}

	QPointer<CompSwipeStrip> m_strip;
};

//! The arrangement's clips, track by track and by start: name, start and length. Selecting a row selects the clip
//! (window.select, and in the arranger); start and length edits are clip.move and clip.resize; Insert adds a clip after
//! the current one on its track (clip.add); Delete removes selected clips with one clip.delete and one undo step each.
class ClipsSource : public ItemListSource
{
public:
	explicit ClipsSource(QObject* parent)
		: ItemListSource(parent)
	{
		auto* poll = new QTimer(this);
		poll->setInterval(ClipPollMs);
		connect(poll, &QTimer::timeout, this, [this] { look(); });
		poll->start();
		connect(ShellSelection::instance(), &ShellSelection::changed, this, &ItemListSource::changed);
		look();
	}

	QString title() const override { return tr("Arrangement clips"); }

	QList<ItemColumn> columns() const override
	{
		return {{QStringLiteral("track"), tr("Track"), ItemColumn::Kind::Text, 0, 0, false},
			{QStringLiteral("name"), tr("Clip"), ItemColumn::Kind::Text, 0, 0, false},
			{QStringLiteral("position"), tr("Start"), ItemColumn::Kind::Position, 0, PositionMax, true},
			{QStringLiteral("length"), tr("Length"), ItemColumn::Kind::Length, 1, PositionMax, true}};
	}

	int rowCount() const override { return static_cast<int>(m_clips.size()); }
	QString rowId(int row) const override { return m_clips.at(row) != nullptr ? control::clipIdOf(m_clips.at(row)) : QString(); }

	int value(int row, int column) const override
	{
		const Clip* clip = m_clips.at(row);
		if (clip == nullptr) { return 0; }
		return static_cast<int>(column == 2 ? clip->startPosition().getTicks() : clip->length().getTicks());
	}

	QString text(int row, int column) const override
	{
		const Clip* clip = m_clips.at(row);
		if (clip == nullptr) { return QString(); }
		if (column == 0) { return clip->getTrack() != nullptr ? clip->getTrack()->name() : QString(); }
		return column == 1 ? clip->name() : ItemListSource::text(row, column);
	}

	bool setValue(int row, int column, int value) override
	{
		if (row >= rowCount() || m_clips.at(row) == nullptr) { return false; }
		if (column == 2) { return runAndLook(QStringLiteral("clip.move"), {{QStringLiteral("clip"), rowId(row)}, {QStringLiteral("position"), std::max(0, value)}}); }
		return runAndLook(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), rowId(row)}, {QStringLiteral("length"), std::max(1, value)}});
	}

	bool canInsert() const override { return true; }
	bool canRemove() const override { return true; }

	bool insertAfter(int row) override
	{
		const Clip* after = row >= 0 && row < rowCount() ? m_clips.at(row).data() : nullptr;
		const Track* track = after != nullptr ? after->getTrack() : ShellSelection::instance()->track();
		if (track == nullptr) { refuse(tr("choose a clip or a track first")); return false; }
		const int position = after != nullptr ? static_cast<int>(after->endPosition().getTicks()) : 0;
		return runAndLook(QStringLiteral("clip.add"), {{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("position"), position}});
	}

	bool remove(const QList<int>& rows) override
	{
		if (rows.isEmpty()) { return false; }
		QStringList ids;
		for (const int row : rows)
		{
			if (row < 0 || row >= rowCount() || m_clips.at(row) == nullptr) { return false; }
			ids.append(rowId(row));
		}
		for (const QString& id : ids)
		{
			if (!run(QStringLiteral("clip.delete"), {{QStringLiteral("clip"), id}})) { look(); return false; }
		}
		look();
		return true;
	}

	bool tracksSelection() const override { return true; }

	QList<int> selectedRows() const override
	{
		const Clip* chosen = ShellSelection::instance()->clip();
		for (int row = 0; row < rowCount(); ++row)
		{
			if (chosen != nullptr && m_clips.at(row) == chosen) { return {row}; }
		}
		return {};
	}

	void select(const QList<int>& rows) override
	{
		if (rows.isEmpty() || m_clips.at(rows.first()) == nullptr) { return; }
		GuiApplication* gui = getGUI();
		SongEditor* editor = gui != nullptr && gui->songEditor() != nullptr ? gui->songEditor()->m_editor : nullptr;
		arrangerkeys::selectClip(editor, m_clips.at(rows.first()));
	}

private:
	//! An edit, and the list read again at once rather than at the next look.
	bool runAndLook(const QString& command, const QJsonObject& args)
	{
		const bool ran = run(command, args);
		look();
		return ran;
	}

	//! The clips now, and whether anything shown changed since the last look.
	void look()
	{
		QList<QPointer<Clip>> now;
		QStringList shown;
		const Song* song = Engine::getSong();
		for (const Track* track : song != nullptr ? song->tracks() : Song::TrackList{})
		{
			std::vector<Clip*> clips(track->getClips().begin(), track->getClips().end());
			std::sort(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->startPosition() < b->startPosition(); });
			for (Clip* clip : clips)
			{
				now.append(clip);
				shown.append(QStringLiteral("%1|%2|%3|%4|%5").arg(track->name(), clip->name()).arg(clip->startPosition().getTicks())
					.arg(clip->length().getTicks()).arg(reinterpret_cast<quintptr>(clip)));
			}
		}
		if (shown == m_shown) { return; }
		m_shown = shown;
		m_clips = now;
		emit changed();
	}

	QList<QPointer<Clip>> m_clips;
	QStringList m_shown;
};

} // namespace

ItemListSource* xyAxes(XyPadDialog* pad, QObject* parent)
{
	return new XySource(pad, parent);
}

ItemListSource* compSegments(CompSwipeStrip* strip, QObject* parent)
{
	return new CompSource(strip, parent);
}

ItemListSource* clips(QObject* parent)
{
	return new ClipsSource(parent);
}

} // namespace lmms::gui::itemlists
