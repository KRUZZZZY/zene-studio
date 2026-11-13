/*
 * OfflineRenderBarrier.h - lets an export wait for out-of-thread device
 *                          renderers BETWEEN periods (Q050-481.2).
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

#ifndef LMMS_OFFLINE_RENDER_BARRIER_H
#define LMMS_OFFLINE_RENDER_BARRIER_H

#include "lmms_export.h"

namespace lmms::offlinerender
{

/*!
 * A device that renders on a thread of its own and hands its frames to the
 * render period through a lock-free ring (the VST2-through-Carla hosts,
 * plugins/CarlaVst2/CarlaVst2Renderer). The render period never waits for it:
 * a period it has not finished is silence. During a live performance that is
 * the right trade; in an export it would put silence into the file. So the
 * exporter (ProjectRenderer::run) calls waitForAll() before each period -
 * outside AudioEngine::renderNextPeriod and its RealtimeScope - and each
 * participant returns once the frames the coming period will take are ready.
 */
class LMMS_EXPORT Participant
{
public:
	virtual ~Participant() = default;
	//! Exporter thread, between two periods: returns once the next period's frames are ready, or after
	//! @a limitMs (a stalled device must not hang the export forever).
	virtual void waitUntilCaughtUp(int limitMs) = 0;
};

//! Control/GUI thread: from now on the exporter waits for @a participant between periods.
LMMS_EXPORT void add(Participant* participant);
//! Before @a participant goes away; waits for an exporter that is waiting on it right now.
LMMS_EXPORT void remove(Participant* participant);

//! Exporter thread, never inside a render period: waits for every participant, each at most @a limitMs.
LMMS_EXPORT void waitForAll(int limitMs = 2000);

} // namespace lmms::offlinerender

#endif // LMMS_OFFLINE_RENDER_BARRIER_H
