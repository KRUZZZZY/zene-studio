/*
 * MidiSmfWrite.cpp - notes as a Standard MIDI File
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

#include "MidiSmfWrite.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "LmmsTypes.h"
#include "TimePos.h"

namespace lmms::midismf
{

namespace
{

constexpr int TicksPerQuarter = DefaultTicksPerBar / 4;

void putBigEndian(QByteArray* out, quint32 value, int bytes)
{
	for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) { out->append(static_cast<char>((value >> shift) & 0xFF)); }
}

//! The largest delta time a Standard MIDI File can hold: a variable-length quantity of at most four bytes.
constexpr quint32 MaxVarLen = 0x0FFFFFFF;

//! Appends @a value as a variable-length quantity; false, with nothing appended, when it needs more than four bytes.
bool putVarLen(QByteArray* out, quint32 value)
{
	if (value > MaxVarLen) { return false; }
	quint8 groups[4];
	int count = 0;
	do
	{
		groups[count++] = value & 0x7F;
		value >>= 7;
	} while (value != 0);
	while (count > 0)
	{
		--count;
		out->append(static_cast<char>(groups[count] | (count > 0 ? 0x80 : 0)));
	}
	return true;
}

struct SmfEvent
{
	tick_t tick = 0;
	int order = 0;  //!< note-offs before note-ons at one tick
	quint8 status = 0;
	quint8 key = 0;
	quint8 velocity = 0;
};

} // namespace

Result write(const std::vector<Note>& notes, int bpm)
{
	std::vector<SmfEvent> events;
	events.reserve(notes.size() * 2);
	for (const Note& note : notes)
	{
		const auto key = static_cast<quint8>(std::clamp(note.key, 0, 127));
		const auto velocity = static_cast<quint8>(std::clamp(note.velocity, 1, 127));
		const tick_t start = std::max<tick_t>(0, note.start);
		const qint64 end = static_cast<qint64>(start) + std::max<tick_t>(1, note.length);
		if (end > static_cast<qint64>(std::numeric_limits<tick_t>::max()))
		{
			return {false, {}, QStringLiteral("a note ends past the largest tick the tick type holds; nothing was written")};
		}
		events.push_back(SmfEvent{start, 1, 0x90, key, velocity});
		events.push_back(SmfEvent{static_cast<tick_t>(end), 0, 0x80, key, 0});
	}
	std::stable_sort(events.begin(), events.end(), [](const SmfEvent& a, const SmfEvent& b) {
		return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
	});
	QByteArray track;
	putVarLen(&track, 0);
	track.append("\xFF\x51\x03", 3);
	putBigEndian(&track, static_cast<quint32>(60000000 / std::max(1, bpm)), 3);
	tick_t at = 0;
	for (const SmfEvent& event : events)
	{
		if (!putVarLen(&track, static_cast<quint32>(event.tick - at)))
		{
			return {false, {},
				QStringLiteral("a delta time exceeds four bytes: the event at tick %1 is more than %2 ticks after the event "
					"before it; nothing was written").arg(event.tick).arg(MaxVarLen)};
		}
		at = event.tick;
		track.append(static_cast<char>(event.status));
		track.append(static_cast<char>(event.key));
		track.append(static_cast<char>(event.velocity));
	}
	putVarLen(&track, 0);
	track.append("\xFF\x2F\x00", 3);
	QByteArray file("MThd", 4);
	putBigEndian(&file, 6, 4);
	putBigEndian(&file, 0, 2);
	putBigEndian(&file, 1, 2);
	putBigEndian(&file, TicksPerQuarter, 2);
	file.append("MTrk", 4);
	putBigEndian(&file, static_cast<quint32>(track.size()), 4);
	return {true, file + track, {}};
}

} // namespace lmms::midismf
