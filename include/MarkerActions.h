/*
 * MarkerActions.h - commands run when playback crosses a marker
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
#ifndef LMMS_MARKER_ACTIONS_H
#define LMMS_MARKER_ACTIONS_H

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms::markeractions
{

/*! 0.5.0 L02 (WF-073): a marker's action runs when playback crosses it. A main-thread watcher reads
 *  the play position a few times a period and runs each crossed marker's command through the
 *  registry - the same call an agent makes, on the thread commands run on; the audio thread is
 *  never involved. A jump backwards (a loop, a seek) only moves the watcher's mark. */
LMMS_EXPORT void start();

//! One step of the watcher: the markers crossed between the last position seen and @a tick run
//! (exposed for tests; the watcher calls it with the song's position while it plays).
LMMS_EXPORT int advance(tick_t tick, bool playing);

} // namespace lmms::markeractions

#endif // LMMS_MARKER_ACTIONS_H
