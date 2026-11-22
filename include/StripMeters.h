/*
 * StripMeters.h - what one mixer strip's meter reads: its own mode, a VU meter and a loudness meter
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

#ifndef LMMS_STRIP_METERS_H
#define LMMS_STRIP_METERS_H

#include <array>
#include <atomic>

#include <QStringList>

#include "LmmsTypes.h"
#include "LufsMeter.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

class MixerChannel;

/*! Q050-108: one strip's live loudness - BS.1770 momentary (400 ms) and short-term (3 s) loudness and the
 *  true peak since the strip entered the LUFS mode, measured on the channel's post-fader signal by the
 *  tree's own LufsMeter. Built on the control thread (stripmeters::setMode) the first time a strip asks for
 *  it and kept with the channel; the mixer worker feeds it (fixed state, a fixed scratch block for the
 *  post-fader copy: no allocation, no lock) and publishes the three readings as relaxed atomics any thread
 *  may read. A reset is asked for by the control thread and carried out by the worker on its next feed, so
 *  only the worker ever touches the meter. The master's loudness tap (MasterLoudnessTap.h) is separate. */
class LMMS_EXPORT StripLoudness
{
public:
	explicit StripLoudness(sample_rate_t sampleRate);

	//! Control thread: start a fresh measurement at the next feed.
	void requestReset() noexcept { m_resetPending.store(true, std::memory_order_release); }
	//! Mixer worker: @a frames frames of @a buffer, scaled by @a gain (the fader), into the meter.
	void feed(const SampleFrame* buffer, float gain, f_cnt_t frames) noexcept;
	//! Mixer worker: a muted period - @a frames of silence, so the readings fall as the signal did.
	void feedSilence(f_cnt_t frames) noexcept;

	float momentaryLufs() const noexcept { return m_momentary.load(std::memory_order_relaxed); }
	float shortTermLufs() const noexcept { return m_shortTerm.load(std::memory_order_relaxed); }
	float truePeakDbtp() const noexcept { return m_truePeak.load(std::memory_order_relaxed); }

private:
	static constexpr int ScratchFrames = 256;
	void resetIfAsked() noexcept;
	void publish() noexcept;

	LufsMeter m_meter;
	std::array<SampleFrame, ScratchFrames> m_scratch{};
	std::atomic<bool> m_resetPending{true};
	std::atomic<float> m_momentary{LufsMeter::MinusInfinity};
	std::atomic<float> m_shortTerm{LufsMeter::MinusInfinity};
	std::atomic<float> m_truePeak{LufsMeter::MinusInfinity};
};

/*! Q050-108: each strip picks what its meter shows - 'default' follows the mixer view's meter_mode (every
 *  older project, and every new channel), 'peak' (after the fader), 'rms', 'pre_fader', 'vu' or 'lufs'
 *  (short-term loudness and true peak). Saved with the channel (MixerStrip.h, the 'metermode' attribute,
 *  written only off its default). The shell's loudness readout and meter.get_state stay the master's. */
namespace stripmeters
{

enum class Mode
{
	Default = 0,
	Peak = 1,
	Rms = 2,
	PreFader = 3,
	Vu = 4,
	Lufs = 5
};

//! The VU meter's integration time: a steady tone reaches 99 % of its reading in 300 ms (IEC 60268-17).
constexpr float VuIntegrationMs = 300.f;

LMMS_EXPORT const QStringList& modeNames();
LMMS_EXPORT int mode(const MixerChannel& channel);
//! Control thread: set the strip's mode; entering 'lufs' builds its loudness meter (once) and starts it afresh,
//! entering 'vu' starts the needle from rest.
LMMS_EXPORT void setMode(MixerChannel& channel, int mode);
//! Mixer worker: the period's post-fader signal (@a buffer times @a gain) into the meter its mode needs.
LMMS_EXPORT void measure(MixerChannel& channel, const SampleFrame* buffer, float gain, f_cnt_t frames);
//! Mixer worker: a muted channel's period - the VU needle and the loudness fall.
LMMS_EXPORT void measureSilence(MixerChannel& channel, f_cnt_t frames);
//! The VU reading of side @a side, in VU against @a referenceDbfs (the dBFS sine peak that reads 0 VU -
//! meter.set_reference); -infinity in silence.
LMMS_EXPORT float vu(const MixerChannel& channel, int side, double referenceDbfs);
//! The channel's loudness meter, or nullptr until the strip first entered 'lufs'.
LMMS_EXPORT const StripLoudness* loudness(const MixerChannel& channel);

} // namespace stripmeters

} // namespace lmms

#endif // LMMS_STRIP_METERS_H
