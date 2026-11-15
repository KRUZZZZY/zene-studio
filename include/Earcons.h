/*
 * Earcons.h - short monitoring tones for transport and edit events
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef LMMS_EARCONS_H
#define LMMS_EARCONS_H

#include <QStringList>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::earcons
{

//! One monitoring tone. The order is also the bit order of the pending mask.
enum class Sound : int
{
	SelectionBoundary = 0,
	EditApplied,
	Refused,
	RecordStart,
	RecordStop,
	LoopWrap,
	Count
};

//! The group a sound belongs to; enabled and level are per category.
enum class Category : int
{
	Selection = 0,
	Edit,
	Refused,
	Record,
	Loop,
	Count
};

LMMS_EXPORT Category categoryOf(Sound sound);
//! The category names in enum order: selection, edit, refused, record, loop.
LMMS_EXPORT const QStringList& categoryNames();

/*! Synthesize every sound for @a rate into preallocated buffers. CONTROL thread only.
 *  A no-op when @a rate is unchanged. Two banks and an atomic active-bank index let a
 *  following play() keep reading a complete bank while the next one is filled. */
LMMS_EXPORT void prepare(sample_rate_t rate);

/*! Queue @a sound for the next device buffer. Any thread, including the audio thread:
 *  one atomic load (the category's enabled flag) and, when enabled, one release
 *  fetch_or. No allocation, no lock and no signal. */
LMMS_EXPORT void play(Sound sound) noexcept;

LMMS_EXPORT void setEnabled(Category category, bool enabled);
LMMS_EXPORT bool enabled(Category category);
//! Set a category's level in [0, 1]. Returns false (and changes nothing) for NaN, inf
//! or a value outside [0, 1]; 0 and 1 are accepted.
LMMS_EXPORT bool setLevel(Category category, float level);
LMMS_EXPORT float level(Category category);

/*! Audio thread: consume the pending mask and (re)start one voice per set bit at
 *  position 0. Single consumer, no allocation, no lock. */
LMMS_EXPORT void beginBuffer() noexcept;

/*! Audio thread: the sum of the active voices' next samples, each scaled by its
 *  category's level. 0 when no voice is active. */
LMMS_EXPORT float nextSample() noexcept;

/*! Monitoring only: these tones are never recorded or rendered. Only
 *  AudioEngine::renderNextBuffer - the device output path - calls beginBuffer() and
 *  nextSample(), so the export and recorder paths (renderNextPeriod) never see them. */

} // namespace lmms::earcons

#endif // LMMS_EARCONS_H
