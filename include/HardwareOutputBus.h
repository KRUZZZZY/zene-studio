/*
 * HardwareOutputBus.h - the device's output channels beyond the master: what each hardware output carries
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

#ifndef LMMS_HARDWARE_OUTPUT_BUS_H
#define LMMS_HARDWARE_OUTPUT_BUS_H

#include <vector>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 F1 (MX-040, MX-036, MX-061): the hardware outputs, as planar channels, one period at a time.
 *
 *  The engine's period stays stereo - the master, what an export, the meters and the recorder read.
 *  This bus holds everything ELSE a device output carries: a channel's direct out, a cue mix, a hardware
 *  insert's send. The mixer adds into the WRITE side at the end of its master mix; AudioEngine::swapBuffers
 *  flips the sides beside its own output double buffer, so the READ side is the period the device is fed
 *  (the same one-period offset the master has). The device feed asks sample() for each of its channels:
 *  what the bus holds there, plus the master if the master is placed on that channel (outputs 1/2 unless
 *  routed elsewhere).
 *
 *  Realtime: init() allocates both sides once, off the audio thread (the engine's constructor). swap(),
 *  add() and the reads only touch that storage - no allocation, no lock. A channel number outside the bus
 *  is dropped, never written. All of it runs on the render thread; nothing else touches it.
 *
 *  Inactive - nothing added, the master on outputs 1/2 - the device feed takes the engine's old path, so
 *  a project that routes nothing to the hardware plays exactly as before. */
class LMMS_EXPORT HardwareOutputBus
{
public:
	//! The output channels the bus can address: the engine's per-buffer ceiling, as the capture path's.
	static constexpr int MaxChannels = 128;

	//! Where the master sits on the outputs: first channel (negative: nowhere) and width (1 or 2).
	struct Placement
	{
		int start = 0;
		int width = 2;
		bool operator==(const Placement&) const = default;
	};

	//! Control thread: both sides for periods of @a frames, cleared.
	void init(f_cnt_t frames);
	f_cnt_t frames() const { return m_frames; }

	//! Render thread, once per period: the written side becomes the one the device reads, and the new
	//! write side is cleared (only the channels the last period used).
	void swap() noexcept;
	//! Render thread: add @a src (a stereo period) at channel @a start - both sides for @a width 2, their
	//! average for width 1 - times @a gain. Channels outside the bus are dropped.
	void add(int start, int width, const SampleFrame* src, f_cnt_t frames, float gain = 1.f) noexcept;
	//! Render thread, 0.5.0 F7 (MX-060): add one plane @a src at @a channel - a surround channel's speaker
	//! beyond its pair - times @a gains[f] when given, else @a gain. A channel outside the bus is dropped.
	void addPlane(int channel, const float* src, f_cnt_t frames, float gain = 1.f, const float* gains = nullptr) noexcept;
	//! Render thread: where the master of the period being written goes.
	void setMasterPlacement(Placement placement) noexcept { m_sides[m_write].master = placement; }

	//! The device side (render thread): whether the period being fed is anything but "the master on 1/2".
	bool active() const noexcept;
	//! Device channel @a channel at @a frame of the period being fed, with @a master that period's frame.
	//! A @a frame outside the period (before 0 or from frames() on) reads as silence.
	float sample(int channel, f_cnt_t frame, const SampleFrame& master) const noexcept;
	//! A one-channel device: the bus's first channel plus the master averaged, when it is placed there.
	//! A @a frame outside the period reads as silence, as in sample().
	float monoSample(f_cnt_t frame, const SampleFrame& master) const noexcept;
	//! The highest channel (plus one) the period being fed carries: the bus's and the master's.
	int channelsInUse() const noexcept;

private:
	struct Side
	{
		std::vector<float> data;  //!< MaxChannels planes of m_frames
		int used = 0;             //!< the channels written, plus one
		Placement master;
	};

	const Side& readSide() const { return m_sides[1 - m_write]; }

	Side m_sides[2];
	int m_write = 0;
	f_cnt_t m_frames = 0;
};

} // namespace lmms

#endif // LMMS_HARDWARE_OUTPUT_BUS_H
