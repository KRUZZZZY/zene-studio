/*
 * RetroMidiClipWriter.cpp - the note matcher that turns a captured MIDI window
 *                           into clip notes (owner item 14)
 *
 * Copyright (c) 2026 LMMS developers
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

#include "RetroMidiClipWriter.h"

#include <algorithm>
#include <vector>

#include "Midi.h"
#include "MidiClip.h"
#include "Note.h"
#include "TimePos.h"
#include "volume.h"

namespace lmms
{

namespace
{

//! A note-on still waiting for its release.
struct OpenNote
{
	tick_t onTick;
	std::uint8_t channel;
	std::uint8_t key;
	std::uint8_t velocity;
};

bool isNoteEvent(const RetroMidiEvent& event)
{
	return event.type == MidiNoteOn || event.type == MidiNoteOff;
}

//! A note-on with velocity 0 IS a note-off (the running-status form), and the
//! element stores the seven-bit velocity MidiEvent::velocity() already masks.
bool isRelease(const RetroMidiEvent& event)
{
	return event.type == MidiNoteOff
		|| (event.type == MidiNoteOn && event.param2 == 0);
}

//! The newest tick among the window's NOTE events - the instant an unmatched
//! note-on is closed at. Derived from the note events rather than from the last
//! event of any kind, so a trailing control change or SysEx placeholder cannot
//! stretch the last note.
tick_t windowEndOf(const RetroMidiEvent* events, std::size_t count, tick_t fallback)
{
	tick_t end = fallback;
	for (std::size_t i = 0; i < count; ++i)
	{
		if (!isNoteEvent(events[i])) { continue; }
		end = std::max(end, static_cast<tick_t>(events[i].tick));
	}
	return end;
}

//! Index of the OLDEST open note for this release's channel and key, or
//! \c open.size() when the release has no note-on before it.
std::size_t matchOpenNote(const std::vector<OpenNote>& open, const RetroMidiEvent& release)
{
	for (std::size_t j = 0; j < open.size(); ++j)
	{
		if (open[j].channel == release.channel && open[j].key == release.param1) { return j; }
	}
	return open.size();
}

//! Append one matched pair as a Note in the caller's tick space. addNote()
//! clones and inserts it in position order and calls updateLength()/dataChanged()
//! itself (src/tracks/MidiClip.cpp:180), so nothing else is needed here.
void appendNote(MidiClip* clip, tick_t onTick, tick_t offTick, int key, int velocity,
	tick_t windowStartTick)
{
	// One tick minimum: a key struck and released inside a single captured tick
	// is still a note, not a zero-length artefact the piano roll cannot show.
	const tick_t length = std::max<tick_t>(1, offTick - onTick);
	const tick_t position = std::max<tick_t>(0, onTick - windowStartTick);
	const int clampedKey = std::clamp(key, 0, NumKeys - 1);
	const volume_t volume = static_cast<volume_t>(
		std::clamp(velocity, static_cast<int>(MinVolume), static_cast<int>(MaxVolume)));
	Note fresh(TimePos(length), TimePos(position), clampedKey, volume);
	// quant_pos = false: the captured ticks are taken as given, exactly as the
	// note.* commands take a caller's ticks (src/core/ControlCommandsNotes.cpp).
	clip->addNote(fresh, false);
}

//! Close every note-on still open at the window edge - minimum one tick - and
//! report each one: the caller is told the capture was truncated rather than
//! handed a note that runs to nowhere.
void closeOpenNotes(MidiClip* clip, const std::vector<OpenNote>& open, tick_t windowEnd,
	tick_t windowStartTick, RetroMidiClipWrite* result)
{
	for (const OpenNote& note : open)
	{
		appendNote(clip, note.onTick, windowEnd, note.key, note.velocity, windowStartTick);
		++result->notes;
		++result->unmatchedOns;
	}
}

} // namespace


RetroMidiClipWrite writeWindowToClip(MidiClip* clip, const RetroMidiEvent* events,
	std::size_t count, tick_t windowStartTick)
{
	RetroMidiClipWrite result;
	result.windowStart = windowStartTick;
	result.windowEnd = windowStartTick;
	if (clip == nullptr || events == nullptr || count == 0) { return result; }

	result.windowEnd = windowEndOf(events, count, windowStartTick);

	// Open notes, oldest first. Matching is per (channel, key) and FIFO, so two
	// strikes of one key (a repeated note whose first release is late) produce
	// two notes rather than one note of the wrong length.
	std::vector<OpenNote> open;

	for (std::size_t i = 0; i < count; ++i)
	{
		const RetroMidiEvent& event = events[i];
		++result.consumed;
		if (!isNoteEvent(event))
		{
			++result.skipped;
			continue;
		}

		if (!isRelease(event))
		{
			open.push_back(OpenNote{static_cast<tick_t>(event.tick), event.channel,
				event.param1, event.param2});
			continue;
		}

		const std::size_t match = matchOpenNote(open, event);
		if (match == open.size())
		{
			// A release with no note-on in the window: the capture started after
			// the key went down. Counted, not invented into a note.
			++result.unmatchedOffs;
			continue;
		}
		appendNote(clip, open[match].onTick, static_cast<tick_t>(event.tick), open[match].key,
			open[match].velocity, windowStartTick);
		++result.notes;
		open.erase(open.begin() + static_cast<std::ptrdiff_t>(match));
	}

	closeOpenNotes(clip, open, result.windowEnd, windowStartTick, &result);
	return result;
}


} // namespace lmms
