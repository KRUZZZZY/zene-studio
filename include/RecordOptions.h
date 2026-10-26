/*
 * RecordOptions.h - count-in, pre-roll and the record mode
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
#ifndef LMMS_RECORD_OPTIONS_H
#define LMMS_RECORD_OPTIONS_H

#include <memory>
#include <vector>

#include <QJsonObject>
#include <QStringList>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class MidiClip;
class Note;
class SampleBuffer;
class Song;

/*! 0.5.0 L03: how a recording pass starts and what it does to what is there, kept in the config file
 *  (recording/countin, recording/preroll, recording/mode) - the user's preference, not the project:
 *  - WF-080 count-in and pre-roll: the transport starts 'count_in' + 'pre_roll' bars before the play
 *    position, the metronome clicks through the count-in bars whether or not it is on, and audio is
 *    captured only from the play position (the punch gate opened there, unless a punch region is
 *    armed - then the region decides). Stopping puts the punch region back as the timeline has it.
 *  - WF-082 record mode (Q050-094: ONE mode for MIDI and audio): overdub keeps the notes a MIDI pass plays
 *    over and mixes an audio pass into the clip's audio; replace removes the notes already in the clip
 *    that a recorded note overlaps and replaces the clip's audio; keep_history keeps an audio clip's
 *    earlier audio and every new pass as take lanes (the newest pass active; comp.take_select chooses
 *    another) and, for MIDI, keeps notes as overdub does (MIDI take lanes are not kept yet). A loop
 *    recording's passes are take lanes in every mode (the loop policy is a separate unit).
 *    Theme 7 (owner decision 2026-10-03): Replace erases NOTES only - every note from before the pass that
 *    a recorded note overlaps in time, at any pitch - and never the clip's controller lanes (CC, pitch
 *    bend, aftertouch, program) or any automation. modeScope() is that sentence, for the record controls.
 *  beginPass / endPass run on the UI thread around Song::playAndRecord and Song::stop;
 *  countingIn() is read by the metronome on the render thread (one relaxed atomic). */
namespace recordoptions
{

enum class Mode
{
	Overdub = 0,  //!< the stored values 0 and 1 are the config file's: never renumbered
	Replace = 1,
	KeepHistory = 2
};

struct Options
{
	int countInBars = 0;  //!< 0..4
	int preRollBars = 0;  //!< 0..8
	Mode mode = Mode::Overdub;
};

LMMS_EXPORT const QStringList& modeNames();
LMMS_EXPORT Options options();
LMMS_EXPORT void setOptions(const Options& next);
LMMS_EXPORT QJsonObject toJson(const Options& options);
//! "Overdub" or "Replace notes": the mode as a record control names it.
LMMS_EXPORT QString modeLabel(Mode mode);
//! What @a mode does to what is there, MIDI and audio - the scope a record control states.
LMMS_EXPORT QString modeScope(Mode mode);
//! Just the MIDI half of modeScope(), and just the audio half.
LMMS_EXPORT QString midiScope(Mode mode);
LMMS_EXPORT QString audioScope(Mode mode);
//! A record control's choice for @a mode, its scope in brief: "Replace (MIDI erases overlapped notes; ...)".
LMMS_EXPORT QString modeChoice(Mode mode);
//! Overdub for audio (UI thread): @a take mixed into @a existing from the first frame, sample for sample, at
//! @a existing's rate (a take of another rate is linearly resampled to it). The result is as long as the
//! longer of the two. Neither input is changed.
LMMS_EXPORT std::shared_ptr<const SampleBuffer> overdubMix(const SampleBuffer& existing, const SampleBuffer& take);
//! The Replace pass's erase (UI thread): in Replace mode, removes from @a clip every note of @a beforePass (note
//! ids: the notes the clip held when the pass began) that @a recorded overlaps in time, at any pitch, and answers
//! how many it removed. Notes only - the clip's controller lanes are not touched. In Overdub: nothing, 0.
LMMS_EXPORT int replaceOverlapped(MidiClip* clip, const Note& recorded, const std::vector<int>& beforePass);

//! Before a recording pass starts: seek back and open the capture gate at the play position. Answers
//! that position (where Stop returns to), or -1 when there is no count-in or pre-roll.
LMMS_EXPORT tick_t beginPass(Song* song);
//! After the transport stops: the timeline's own punch region back on the gate, the count-in over.
LMMS_EXPORT void endPass(Song* song);
//! Whether @a tick lies in the count-in of the pass running now (render thread; lock-free).
LMMS_EXPORT bool countingIn(tick_t tick);

} // namespace recordoptions

} // namespace lmms

#endif // LMMS_RECORD_OPTIONS_H
