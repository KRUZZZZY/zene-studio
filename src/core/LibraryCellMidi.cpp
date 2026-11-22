/*
 * LibraryCellMidi.cpp - a launcher cell's pattern notes, saved into the library
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

#include "LibraryCellMidi.h"

#include <algorithm>
#include <vector>

#include <QSaveFile>

#include "Midi.h"
#include "MidiClip.h"
#include "MidiSmfWrite.h"
#include "Note.h"
#include "PatternStore.h"
#include "Track.h"

namespace lmms::librarymidi
{

auto CellMidi::save(PatternStore* store, int pattern, int bpm, const QString& target) -> Result
{
	Result result;
	if (store == nullptr || pattern < 0 || pattern >= store->numOfPatterns())
	{
		result.error = QStringLiteral("the pattern %1 is not in this project").arg(pattern);
		return result;
	}
	std::vector<midismf::Note> notes;
	for (Track* track : store->tracks())
	{
		if (pattern >= track->numOfClips()) { continue; }
		auto* clip = dynamic_cast<MidiClip*>(track->getClip(static_cast<std::size_t>(pattern)));
		if (clip == nullptr) { continue; }
		for (const Note* note : clip->notes())
		{
			notes.push_back(midismf::Note{std::max<tick_t>(0, note->pos().getTicks()),
				std::max<tick_t>(1, note->length().getTicks()), note->key(), note->midiVelocity(MidiDefaultVelocity)});
		}
	}
	if (notes.empty())
	{
		result.error = QStringLiteral("the pattern has no notes");
		return result;
	}
	const midismf::Result written = midismf::write(notes, bpm);
	if (!written.ok)
	{
		result.error = written.error;
		return result;
	}
	QSaveFile file(target);
	if (!file.open(QIODevice::WriteOnly) || file.write(written.bytes) != written.bytes.size() || !file.commit())
	{
		result.error = QStringLiteral("cannot write %1").arg(target);
		return result;
	}
	result.ok = true;
	result.notes = static_cast<int>(notes.size());
	return result;
}

} // namespace lmms::librarymidi
