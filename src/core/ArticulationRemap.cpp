/*
 * ArticulationRemap.cpp - the notes follow their articulation when a track's map is reordered or trimmed
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

#include "ArticulationRemap.h"

#include <cstdint>
#include <unordered_map>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteConditions.h"
#include "Song.h"
#include "Engine.h"

namespace lmms
{

bool validateArticulationOrigins(const std::vector<int>& from, int oldCount, QString* error)
{
	std::vector<bool> seen(static_cast<std::size_t>(oldCount) + 1, false);
	for (const int origin : from)
	{
		if (origin < 0 || origin > oldCount)
		{
			*error = QStringLiteral("'from' names the old position of each articulation: 0 for a new one, else 1..%1").arg(oldCount);
			return false;
		}
		if (origin > 0 && seen[static_cast<std::size_t>(origin)])
		{
			*error = QStringLiteral("'from' names old articulation %1 twice").arg(origin);
			return false;
		}
		seen[static_cast<std::size_t>(origin)] = true;
	}
	return true;
}

std::vector<ArticulationNoteChange> planArticulationRemap(const InstrumentTrack& track, const std::vector<int>& from, int oldCount)
{
	// Old position -> new position; a position nobody came from is a removed articulation (0).
	std::vector<int> moved(static_cast<std::size_t>(oldCount) + 1, 0);
	for (std::size_t i = 0; i < from.size(); ++i)
	{
		if (from[i] > 0) { moved[static_cast<std::size_t>(from[i])] = static_cast<int>(i) + 1; }
	}
	std::vector<ArticulationNoteChange> changes;
	for (const Clip* clip : track.getClips())
	{
		const auto* midi = dynamic_cast<const MidiClip*>(clip);
		if (midi == nullptr) { continue; }
		for (const Note* note : midi->notes())
		{
			const int before = note->conditions().articulation;
			if (before <= 0) { continue; }
			const int after = before <= oldCount ? moved[static_cast<std::size_t>(before)] : 0;
			if (after != before) { changes.push_back({clip->id(), note->id(), before, after}); }
		}
	}
	return changes;
}

void applyArticulationChanges(const QString& trackId, const std::vector<ArticulationNoteChange>& changes, bool forward)
{
	ControlResult ignored;
	auto* track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(trackId, &ignored));
	if (track == nullptr || changes.empty()) { return; }
	std::unordered_map<std::uint64_t, const ArticulationNoteChange*> byNote;
	const auto key = [](int clip, int note) { return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(clip)) << 32) | static_cast<std::uint32_t>(note); };
	for (const ArticulationNoteChange& change : changes) { byNote[key(change.clip, change.note)] = &change; }
	track->lock();
	for (Clip* clip : track->getClips())
	{
		auto* midi = dynamic_cast<MidiClip*>(clip);
		if (midi == nullptr) { continue; }
		bool touched = false;
		for (Note* note : midi->notes())
		{
			const auto hit = byNote.find(key(clip->id(), note->id()));
			if (hit == byNote.end()) { continue; }
			NoteConditions conditions = note->conditions();
			conditions.articulation = forward ? hit->second->after : hit->second->before;
			note->setConditions(conditions);
			touched = true;
		}
		if (touched) { emit midi->dataChanged(); }
	}
	track->unlock();
	Engine::getSong()->setModified();
}

} // namespace lmms
