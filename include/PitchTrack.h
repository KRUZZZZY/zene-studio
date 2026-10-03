/*
 * PitchTrack.h - a monophonic line's notes, found in audio (YIN)
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
#ifndef LMMS_PITCH_TRACK_H
#define LMMS_PITCH_TRACK_H

#include <cstddef>
#include <vector>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::pitchtrack
{

//! One detected note: frames [start, end) of the analysed audio, its MIDI key and its loudness (0..1).
struct PitchNote
{
	f_cnt_t start = 0;
	f_cnt_t end = 0;
	int key = 60;
	float level = 0.f;
};

/*! 0.5.0 L05 (ED-160): the notes of a monophonic line - a voice, a bass, a lead - in @a count mono
 *  frames at @a rate: YIN's pitch per hop, held notes where the key stays put and the audio is louder
 *  than @a gate, at least @a minFrames long. Control thread; one pass, bounded memory. Octave errors
 *  are YIN's own (a strong second harmonic can read an octave up). */
LMMS_EXPORT std::vector<PitchNote> detectNotes(const float* mono, std::size_t count, int rate, float gate = 0.02f,
	f_cnt_t minFrames = 2048);

//! YIN's estimate of @a size frames at @a frame, in Hz; 0 for no clear pitch. Exposed for tests.
LMMS_EXPORT float pitchAt(const float* mono, std::size_t count, std::size_t frame, int size, int rate);

} // namespace lmms::pitchtrack

#endif // LMMS_PITCH_TRACK_H
