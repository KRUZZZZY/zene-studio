/*
 * RecorderTaps.h - recording the master or a channel's output instead of the input
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
#ifndef LMMS_RECORDER_TAPS_H
#define LMMS_RECORDER_TAPS_H

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class MixerChannel;
class SampleFrame;
enum class SidechainTapPoint : int;

/*! 0.5.0 L02/L03 (WF-072, WF-086): where a record route takes its audio from. By default the engine's
 *  input (the route's input channel); here it can be the master output instead (resampling), or one
 *  mixer channel's signal at a tap point - before its inserts, before its fader, or after it - one side
 *  of it each route (routes are mono files). The mixer's workers and the render thread push into the
 *  route's own ring at those points (each route has one source, so one producer); the table is atomics,
 *  changed on the UI thread; nothing is allocated or locked on the audio path. A change is pending until
 *  beginPeriod() publishes it at the next period boundary, so one route has one source per period. A
 *  tapped route is skipped by the input path. */
namespace recordertaps
{

enum class Source : int { Input = 0, Master = 1, Channel = 2 };

struct Tap
{
	Source source = Source::Input;
	int channelId = -1;  //!< the mixer channel's persistent id (Source::Channel)
	int point = 0;       //!< a SidechainTapPoint value (Source::Channel)
	int side = 0;        //!< 0 left, 1 right
};

//! Point route @a route at @a tap (Source::Input puts it back on the engine's input). UI thread. The
//! change is pending: it takes effect at the next period boundary, so one route has one source per period.
LMMS_EXPORT void set(int route, const Tap& tap);
//! The tap set for route @a route (pending included: the UI and command replies see it immediately).
LMMS_EXPORT Tap get(int route);
//! The requested/pending UI view of route @a route: whether it takes something other than the input.
//! Includes get/command replies, so a tap is visible here before the next period. Audio code must use
//! audioTapped() for the published per-period value; this is not the audio decision.
LMMS_EXPORT bool tapped(int route) noexcept;

//! Render thread, at the very top of a period: publish every pending tap as the active one for this period.
LMMS_EXPORT void beginPeriod() noexcept;
//! Audio thread: whether route @a route's active tap takes something other than the input (skips the input
//! path). Reads only the value beginPeriod() published, so it matches the tap producers' snapshot.
LMMS_EXPORT bool audioTapped(int route) noexcept;

//! Mixer worker: channel @a channel reached tap point @a point this period.
LMMS_EXPORT void channelAt(const MixerChannel& channel, SidechainTapPoint point, f_cnt_t frames) noexcept;
//! Render thread: the master output of this period, before any listen bus replaces it.
LMMS_EXPORT void master(const SampleFrame* buffer, f_cnt_t frames) noexcept;

} // namespace recordertaps

} // namespace lmms

#endif // LMMS_RECORDER_TAPS_H
