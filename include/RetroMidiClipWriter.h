/*
 * RetroMidiClipWriter.h - turn a captured MIDI window into clip notes
 *                        (retrospective MIDI capture, owner item 14)
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

#ifndef LMMS_RETRO_MIDI_CLIP_WRITER_H
#define LMMS_RETRO_MIDI_CLIP_WRITER_H

#include <cstddef>

#include "LmmsTypes.h"      // tick_t
#include "RetroMidiRing.h"  // RetroMidiEvent
#include "lmms_export.h"

namespace lmms
{

class MidiClip;


//! What one writeWindowToClip() call did (docs/MIDI-RETRO-CAPTURE.md 3.7).
/*!
 * The counters exist so the caller is TOLD about truncation instead of being
 * handed a clean-looking clip: a note-on with no note-off inside the window is
 * closed at the window's end and counted in unmatchedOns, and a note-off with
 * no note-on is counted in unmatchedOffs. Both are losses the capture cannot
 * hide, and the honest alternative to hiding them is reporting them.
 */
struct RetroMidiClipWrite
{
	int notes = 0;           //!< notes appended to the clip
	int unmatchedOns = 0;    //!< note-ons with no note-off in the window, closed at its end
	int unmatchedOffs = 0;   //!< note-offs with no matching note-on before them
	int skipped = 0;         //!< events deliberately not turned into notes (CC, bend, SysEx, ...)
	int consumed = 0;        //!< events examined
	tick_t windowStart = 0;  //!< the tick the caller passed
	tick_t windowEnd = 0;    //!< newest tick among the window's note events
};


//! Turn the captured window \a events into notes on \a clip.
/*!
 * \a events is the ring's window in arrival order, oldest first, and
 * \a windowStartTick is the tick its first event is placed at (position 0 in
 * the clip). \a clip must be a MIDI clip of an instrument track - the notes go
 * through MidiClip::addNote(), the same entry the piano roll and the note.*
 * commands use, with quant_pos = false so the captured ticks are taken as given
 * rather than snapped to a quantisation grid (SPEC A11: one implementation).
 *
 * Matching is arrival-order FIFO per (channel, key): a note-off closes the
 * OLDEST open note with the same channel and key, which is what a keyboard
 * means when a key is struck twice before the first release arrives. A note-on
 * with velocity 0 is a note-off - the running-status form every keyboard sends
 * (MidiNoteOn is a status byte in include/Midi.h, not a promise). Lengths are
 * clamped to a minimum of one tick, so a captured double-strike that lands on
 * one tick cannot produce a zero-length note.
 *
 * Non-note events (control change, pitch bend, aftertouch, program change and
 * the SysEx placeholder) are counted in \c skipped and written nowhere: the
 * window is a capture of what was PLAYED, and the smallest honest version does
 * not invent automation tracks for it (docs/MIDI-RETRO-CAPTURE.md 7).
 *
 * Allocation: this runs on the consumer (GUI/control) thread - never on the
 * MIDI thread - so its bookkeeping vector is allowed (section 3.7). It never
 * touches the ring, and a null clip or an empty window writes nothing and
 * returns zeroed counters.
 */
LMMS_EXPORT RetroMidiClipWrite writeWindowToClip(MidiClip* clip, const RetroMidiEvent* events,
	std::size_t count, tick_t windowStartTick);


} // namespace lmms

#endif // LMMS_RETRO_MIDI_CLIP_WRITER_H
