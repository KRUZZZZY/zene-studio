/*
 * NoteRandom.h - seeded, stateless note randomisation for MIDI depth
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

#ifndef LMMS_NOTE_RANDOM_H
#define LMMS_NOTE_RANDOM_H

#include <cstdint>

#include <QDomElement>

namespace lmms
{


/*! Note probability and velocity jitter - the seeded half of MIDI depth.
 *
 *  Every value here is a *pure function* of the project seed and the note's
 *  identity (pitch, position, length). That is deliberate, and it is what makes
 *  the feature usable from the audio thread: no allocation, no locking, no
 *  hidden state, and the same inputs always produce the same output - so a
 *  take is exactly repeatable (same seed) and a different seed re-rolls it.
 *
 *  Because the roll input is static per note, the outcome is also stable
 *  *across passes* within one project: a 50% note either plays in every repeat
 *  of the clip or in none of them. Varying per pass would require persisted
 *  per-note playback state; that is deliberately not implemented (see
 *  docs/MIDI-DEPTH.md, "Not done").
 */
namespace NoteRandom
{

//! The seed stored in the project header. Absent (every project saved before
//! MIDI depth, and every project that never touches it) reads as 0.
uint32_t readProjectSeed( const QDomElement& head );

//! Writes the seed into the project header, but only when it is not the
//! default 0 - so a project that does not use MIDI depth serialises
//! byte-identically to how it did before this feature existed.
void writeProjectSeed( QDomElement& head, uint32_t seed );

/*! Uniform 32-bit hash of a note's identity. `salt` separates the independent
 *  streams of one note (probability vs. velocity jitter). */
uint32_t roll( uint32_t seed, int key, int pos, int length, uint32_t salt = 0 );

//! `roll()`, scaled to [0, 1).
float rollUnit( uint32_t seed, int key, int pos, int length, uint32_t salt = 0 );

/*! Whether a note carrying `probability` is played in this take. 1 or more
 *  always passes (and returns before touching the hash), 0 or less never
 *  passes. */
bool passesProbability( float probability, uint32_t seed, int key, int pos, int length );

/*! Multiplicative velocity factor for a note carrying `jitter`: a value in
 *  [1-j, 1+j]. Jitter 0 returns exactly 1.0 without touching the hash. */
float velocityFactor( float jitter, uint32_t seed, int key, int pos, int length );

} // namespace NoteRandom


} // namespace lmms

#endif // LMMS_NOTE_RANDOM_H
