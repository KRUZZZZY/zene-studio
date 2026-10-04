/*
 * CompSlip.h - a comped audio take sounds its slipped segments slipped (0.5.0 E2)
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
#ifndef LMMS_COMP_SLIP_H
#define LMMS_COMP_SLIP_H

#include <array>
#include <memory>
#include <optional>

#include "LmmsTypes.h"
#include "Sample.h"
#include "lmms_export.h"

namespace lmms
{

class SampleClip;
class SampleFrame;
class TakeLaneModel;

/*! 0.5.0 E2: the slipped reads of one take's play handle.
 *
 *  A composite segment's slip (TakeLaneSegment::sourceOffset, include/TakeLane.h) says: over the segment, read
 *  the lane's take N ticks LATER than its own place. The take's SamplePlayHandle already renders the take at its
 *  own place and gates it by its lane's spans (R3.1); this object renders the same source a second time, N ticks
 *  ahead, and writes that read over the frames of the slipped spans before the gate runs. So the comp's ramps at
 *  every span edge (SamplePlayHandle::applyComp) cover the jump in the take too, and a span with no slip is the
 *  handle's own read, untouched.
 *
 *  One READER per distinct slip of the clip's lane (at most MaxReaders; a further slip plays unslipped), each a
 *  Sample::PlaybackState that starts the slip's frames after the handle's own and is played every period with the
 *  handle's own ratio - LOCKSTEP, so it stays exactly the slip ahead wherever the handle is. A read past the
 *  take's window is silence: the take does not reach there.
 *
 *  Scope, stated: built only for a take that renders linearly at its own pitch (no warp markers, no source tempo,
 *  not through the pitch-keeping stretcher) - the lockstep needs one ratio for both reads, which a warp's
 *  position-dependent rate does not give. Such a take, and an auditioned lane, play unslipped. A slipped span
 *  sounds where the take's clip is on the timeline (its handle's lifetime); the slip chooses what it reads there.
 *
 *  Threading: create() runs where the handle is constructed (it allocates the readers, as the handle's own
 *  construction does); render() is the audio thread's and neither allocates nor locks.
 */
class LMMS_EXPORT CompSlipReaders
{
public:
	static constexpr int MaxReaders = 4;
	static constexpr int MaxSpans = 32;

	/*! The readers for @a clip's slipped spans in @a comp, or nullptr when the clip has none (the handle then pays
	 *  nothing). @a sourceIn is the source frame the handle starts on; @a outputFramesPerTick converts the spans
	 *  to output frames from the clip's start (the frame base SamplePlayHandle's own comp spans use). */
	static std::unique_ptr<CompSlipReaders> create(const TakeLaneModel& comp, const SampleClip& clip, f_cnt_t sourceIn,
		double outputFramesPerTick);

	/*! Audio thread: plays every reader over @a frames with @a ratio (the handle's own) and writes each slipped
	 *  span's frames of @a dst - output frame @a clipFrame from the clip's start onwards - from its reader. */
	void render(const Sample& sample, SampleFrame* dst, f_cnt_t frames, f_cnt_t clipFrame, double ratio);

	int readerCount() const { return m_readerCount; }
	int spanCount() const { return m_spanCount; }

private:
	struct Span
	{
		f_cnt_t from = 0;
		f_cnt_t to = 0;
		int reader = 0;
	};

	CompSlipReaders() = default;

	//! The reader of @a slipTicks: the one already open for it, a new one, or -1 when all are taken.
	int readerFor(int slipTicks);
	void addSpan(int fromTick, int toTick, int slipTicks, double outputFramesPerTick);
	void openReaders(f_cnt_t sourceIn, float sourceFramesPerTick);
	//! Copies the frames of reader @a reader's spans inside [@a at, @a at + @a frames) from @a src to @a dst.
	void copySpans(int reader, const SampleFrame* src, SampleFrame* dst, f_cnt_t frames, f_cnt_t at) const;

	std::array<int, MaxReaders> m_slips{};
	std::array<std::optional<Sample::PlaybackState>, MaxReaders> m_readers;
	int m_readerCount = 0;
	std::array<Span, MaxSpans> m_spans{};
	int m_spanCount = 0;
};

} // namespace lmms

#endif // LMMS_COMP_SLIP_H
