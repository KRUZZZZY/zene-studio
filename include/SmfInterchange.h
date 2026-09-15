/*
 * SmfInterchange.h - the Standard MIDI File conductor track: the writer and the
 *                    reader behind the `interchange.*` command group.
 *
 * The engine half of feature row 33 (tempo-map export / SMF cross-DAW
 * interchange). A Standard MIDI File is the one interchange format every DAW
 * reads, and the tempo map is the part of a session that has no other portable
 * form: another DAW can read this file and see the SAME tempo and metre steps
 * this session obeys, at the same ticks.
 *
 * WHAT THIS FILE IS NOT. It is not the note/clip export: that is
 * plugins/MidiExport (File > Export MIDI) and it is untouched here. This file
 * carries the CONDUCTOR TRACK - the tempo map and nothing else - which is why
 * it is useful to a DAW that only wants the grid.
 *
 * THE CONVENTION, RECORDED (the release contract asks for it explicitly, and
 * `interchange.smf_convention` hands the same values to a client):
 *
 *  - DIVISION. The writer emits SMF division 480 = ticks per QUARTER note
 *    (`SmfTicksPerQuarterNote`). LMMS' own grid is 48 ticks per quarter note
 *    (`DefaultTicksPerBar` 192 / 4) - one beat, and the engine's tempo is
 *    defined on exactly that beat (`Engine::updateFramesPerTick`: sampleRate *
 *    60 * 4 / DefaultTicksPerBar / bpm, i.e. 48 ticks = one quarter note =
 *    one minute/bpm). So `SmfTicksPerLmmsTick` is 10 and a LMMS tick maps to
 *    the file EXACTLY, with no rounding anywhere on the write side.
 *  - TEMPO. One event per tempo change, carried by the tempo meta event
 *    FF 51 03 tttttt, tttttt = microseconds per QUARTER note = round(60e6 /
 *    bpm) (Standard MIDI File 1.0, "set tempo"). The tempo is per quarter note
 *    REGARDLESS of the time signature in force - which is exactly what this
 *    engine's own tempo means, because a time-signature event in LMMS changes
 *    bar/beat arithmetic and NOT the tick-to-frame rate (docs/TEMPO-MAP.md).
 *    The bpm -> microseconds -> bpm round trip is EXACT for every integer bpm
 *    the engine accepts (10..999): the quotient spacing of 60e6/bpm is wider
 *    than one microsecond over that whole range, so rounding cannot collide.
 *  - TIME SIGNATURE. One event per metre change, carried by FF 58 04 nn dd cc
 *    bb with dd = log2(denominator), cc = 24 (MIDI clocks per metronome click)
 *    and bb = 8 (32nd notes per quarter note). The reader turns dd back into
 *    2^dd, so a metre the map can hold (a power of two up to 32) round-trips
 *    exactly.
 *  - THE SHAPE. Format 1, ONE track: the conductor track at index 0. Format 1
 *    is what a DAW reads as a tempo map (a format-0 file merges the conductor
 *    into the note track); the reader accepts format 0 and format 1 alike and
 *    reads the tempo/metre events of EVERY track, because a foreign file may
 *    put them anywhere.
 *  - THE TICK-0 SEED, and why it is not optional. LMMS' tempo map has a
 *    GLOBAL tempo and metre that are in force BEFORE the map's first event; an
 *    SMF has no such concept - before its first tempo event a player assumes
 *    120 bpm. Export therefore writes, at tick 0, the tempo and the metre the
 *    timeline actually obeys AT tick 0 (map.tempoAtTick(0, global) and
 *    map.timeSignatureAtTick(0, global)) unless the map already carries that
 *    half at tick 0. Without the seed, a session whose map starts at bar 5
 *    would export as a file whose bars 1-4 play at 120 bpm - a different
 *    session. `seed_events` in the write report counts the halves written this
 *    way, so the caller can see exactly what the file does not have in the map.
 *
 * WHAT IS DELIBERATELY OUT OF SCOPE (docs/SMF-INTERCHANGE.md, and the one line
 * in docs/KNOWN-LIMITATIONS.md): events are STEPS, so no tempo curve is
 * exported (a linear ramp is not representable in this file's vocabulary and
 * the map itself holds steps only); only the conductor track is written, so
 * notes, clips and automation are not in the file; and only the tempo and
 * metre meta events are read back, so the rest of a foreign file is ignored
 * rather than refused.
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
 */

#ifndef LMMS_SMF_INTERCHANGE_H
#define LMMS_SMF_INTERCHANGE_H

#include <QString>

#include <vector>

#include "TempoMap.h"
#include "TimePos.h"
#include "lmms_export.h"

namespace lmms
{
namespace interchange
{

//! The division every file this module WRITES declares: ticks per quarter note.
constexpr int SmfTicksPerQuarterNote = 480;
//! LMMS' grid: DefaultTicksPerBar (192) / DefaultBeatsPerBar (4) = one quarter
//! note = 48 ticks, which is the beat `Engine::updateFramesPerTick` divides by.
constexpr int LmmsTicksPerQuarterNote = 48;
//! 480 / 48: an exact integer, which is why the write side never rounds a tick.
constexpr int SmfTicksPerLmmsTick =
	SmfTicksPerQuarterNote / LmmsTicksPerQuarterNote;
//! One minute in microseconds - the scale the tempo meta event is expressed in.
constexpr int SmfMicrosecondsPerMinute = 60000000;
//! The chunk id and the two meta types this module understands.
constexpr unsigned char SmfMetaTempo = 0x51;
constexpr unsigned char SmfMetaTimeSignature = 0x58;
constexpr unsigned char SmfMetaEndOfTrack = 0x2F;
constexpr unsigned char SmfMetaTrackName = 0x03;
//! Format 1 with one conductor track (see the header comment).
constexpr int SmfWrittenFormat = 1;

/*! One conductor event, in LMMS' own tick domain (48 ticks per quarter note).
 *  Either half may be absent, exactly as in TempoMapEvent: the two halves are
 *  independent step functions over one tick list. */
struct SmfInterchangeEvent
{
	tick_t tick = 0;
	bool hasTempo = false;
	int tempo = TempoMapDefaultTempo;
	bool hasTimeSignature = false;
	int numerator = 4;
	int denominator = 4;

	bool operator==(const SmfInterchangeEvent& other) const
	{
		return tick == other.tick && hasTempo == other.hasTempo && tempo == other.tempo
			&& hasTimeSignature == other.hasTimeSignature
			&& numerator == other.numerator && denominator == other.denominator;
	}
	bool operator!=(const SmfInterchangeEvent& other) const { return !(*this == other); }
};

/*! The convention this module writes and reads, as data rather than as prose:
 *  `interchange.smf_convention` returns it verbatim, so a client can read the
 *  PPQ and the metre encoding off the wire instead of out of a comment. */
struct SmfConvention
{
	int ticksPerQuarterNote = SmfTicksPerQuarterNote;
	int lmmsTicksPerQuarterNote = LmmsTicksPerQuarterNote;
	int smfTicksPerLmmsTick = SmfTicksPerLmmsTick;
	//! "microseconds per quarter note (meta FF 51 03), independent of the
	//! metre in force - which is what LMMS' own bpm means".
	QString tempoUnit;
	//! "FF 58 04 nn dd cc bb; dd = log2(denominator), cc = 24 MIDI clocks per
	//! metronome click, bb = 8 32nd notes per quarter note".
	QString timeSignatureEncoding;
	//! "format 1, one conductor track at index 0; the reader accepts format 0
	//! and format 1 and reads every track's tempo and metre events".
	QString trackShape;
	//! What a tick-0 event means on each side (the seed rule).
	QString tickZeroRule;
	//! What the file does NOT carry.
	QString statedLimits;
};

//! The convention above, built once.
LMMS_EXPORT const SmfConvention& smfConvention();

//! What one successful write produced. \c bytes and \c sha256 describe the
//! FILE; the event counts describe what went into it.
struct SmfWriteReport
{
	QString path;
	qint64 bytes = 0;
	QString sha256;
	int format = SmfWrittenFormat;
	int trackCount = 1;
	int ticksPerQuarterNote = SmfTicksPerQuarterNote;
	int eventCount = 0;      //!< ticks carrying at least one half
	int tempoEvents = 0;
	int meterEvents = 0;
	//! Halves synthesized at tick 0 by the seed rule (see the header comment):
	//! 0, 1 or 2. The file's first event is at tick 0 whenever this is not 0.
	int seedEvents = 0;
	int firstTick = 0;
	int lastTick = 0;        //!< the last event's tick, in LMMS ticks
};

//! What one successful read produced.
struct SmfReadReport
{
	bool ok = false;
	QString error;           //!< the refusal, verbatim, when ok is false
	int format = 0;
	int trackCount = 0;
	int ticksPerQuarterNote = 0;  //!< the file's own division
	std::vector<SmfInterchangeEvent> events;  //!< LMMS ticks, increasing
	int tempoEvents = 0;
	int meterEvents = 0;
	//! Events whose tick is not exactly representable on LMMS' 48-per-quarter
	//! grid and had to be rounded. Zero for every file this module wrote, and
	//! for any foreign file whose division is a multiple of 48.
	int roundedEvents = 0;
	//! Meta events of the two types this module understands that a later event
	//! at the same tick superseded (the reader keeps the FIRST one, which is
	//! what a player does).
	int supersededEvents = 0;
	//! Tempo/meter events the file carries beyond the map's capacity: the map
	//! cannot hold them, so `mapFromEvents` refuses rather than truncating.
	int capacityEvents = 0;
};

/*! Write the tempo map to \a path as a Standard MIDI File (format 1, one
 *  conductor track). \a globalTempo and \a globalSignature are the values the
 *  timeline obeys OUTSIDE the map, needed for the tick-0 seed. False - with
 *  \a error set and no file left behind - when the file cannot be written. */
LMMS_EXPORT bool writeConductorTrack(const QString& path, const TempoMap& map,
	int globalTempo, const TempoMapTimeSignature& globalSignature,
	SmfWriteReport* report, QString* error);

/*! Read the conductor events of the Standard MIDI File at \a path: every
 *  track's tempo and time-signature meta events, merged by tick (first wins),
 *  in LMMS' tick domain. A file that is not a Standard MIDI File, or whose
 *  division is an SMPTE rate (a negative division), is refused with \c error. */
LMMS_EXPORT SmfReadReport readConductorTrack(const QString& path);

/*! The map \a events describe, or false when an event is one the engine will
 *  not accept (an unsupported denominator, a tempo out of range, a negative
 *  tick) or when the events need more than TempoMap::MaxEvents ticks. The map
 *  is left untouched on refusal, and the map it builds is ACTIVE: a conductor
 *  track is a tempo map by construction, so importing one that is switched off
 *  would import tempo changes the timeline does not obey. */
LMMS_EXPORT bool mapFromEvents(const std::vector<SmfInterchangeEvent>& events,
	TempoMap* map, QString* error);

//! The events of \a map, in the file's terms (the tick-0 seed applied).
LMMS_EXPORT std::vector<SmfInterchangeEvent> exportEvents(const TempoMap& map,
	int globalTempo, const TempoMapTimeSignature& globalSignature, int* seedCount);

} // namespace interchange
} // namespace lmms

#endif // LMMS_SMF_INTERCHANGE_H
