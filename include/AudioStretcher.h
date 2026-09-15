/*
 * AudioStretcher.h - pitch-preserving time stretching (WSOLA), the DSP the
 *                    warp/clip path selects instead of plain resampling.
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
 */

#ifndef LMMS_AUDIO_STRETCHER_H
#define LMMS_AUDIO_STRETCHER_H

#include <array>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

/*! Pitch-preserving time stretch — WSOLA (waveform-similarity overlap-add).
 *
 *  WHY THIS EXISTS. Every rate change in this engine used to be plain
 *  resampling: `Sample::play` hands the ratio to `AudioResampler`
 *  (libsamplerate) and the pitch moves with the rate, so a 2x warp is an
 *  octave up (docs/WARP.md §3). This class is the second answer: it changes
 *  how long the audio lasts while leaving the waveform's own period — the
 *  pitch — where it was.
 *
 *  HOW. The output is built grain by grain from the source, at 50 % overlap
 *  with a periodic Hann window (which is exactly constant-overlap-add, so a
 *  rate of 1 rebuilds the waveform). A hop advances the OUTPUT by the
 *  synthesis hop `Hs` and the SOURCE by `Hs * speed`, which is what makes the
 *  two rates independent of each other. Before each grain is mixed in, the
 *  grain's start is searched over ±`searchRadius` source frames for the offset
 *  that best continues what has already been written (normalised
 *  cross-correlation against the accumulator's carry-over region). That search
 *  is the whole difference from a resampler: a resampler has nothing to align
 *  to, so its waveform period shrinks with the ratio; WSOLA re-aligns every
 *  grain to the period already in the output, so the period survives and only
 *  the grain positions move. Two grains out of phase by a half period would
 *  cancel; the correlation is what prevents that.
 *
 *  NUMBERS (measured, docs/PITCH-STRETCH.md): on a 440 Hz tone, speed 2.0,
 *  44.1 kHz, the resample control renders 880 Hz and this renders 441 Hz —
 *  the pitch is preserved to within the measurement's own resolution, on the
 *  same input, through the same call. The cost is the alignment search:
 *  (2 * searchRadius + 1) * (grainFrames / 2) multiply-adds per synthesis hop,
 *  i.e. ~257 * 512 = 131 k per 512 output frames at the default parameters.
 *
 *  REALTIME. Everything is a fixed-size member array sized at `prepare()`:
 *  `process()` allocates nothing, locks nothing and grows nothing (I8), so it
 *  can run on the audio thread for as long as the caller wants. `prepare()`
 *  itself allocates nothing either (it fills the window table), but it is the
 *  one call that is not free, so a caller prepares once and reuses.
 *
 *  LIMITATIONS, stated rather than discovered later:
 *   * the alignment search reaches ±`searchRadius` frames, so a source whose
 *     period is longer than that (below ~345 Hz at the default 128 frames and
 *     44.1 kHz) is beyond its reach and is stretched as plain overlap-add;
 *   * the sample-rate conversion on this path is a linear interpolation
 *     between source frames (the same class as the engine's default `Linear`
 *     converter), not the resampler's sinc filters;
 *   * `speed == 1.0` is NOT a bit-identical copy (the grains are re-windowed
 *     and re-aligned). A caller that has nothing to stretch must not route
 *     through here — the warp path bypasses this class entirely at speed 1.
 */
class LMMS_EXPORT AudioStretcher
{
public:
	//! The stretch algorithms this class can run. One today, and the enum is
	//! here so a second one is a new enumerator rather than a new class the
	//! clip, the persistence and the command surface each have to learn.
	enum class Algorithm
	{
		//! Waveform-similarity overlap-add: time-domain, no transform, the
		//! algorithm the clip path selects.
		Wsola = 0
	};

	/*! The quality/complexity dial, in frames. Defaults, and what they buy:
	 *
	 *  * `grainFrames` = 1024 — the analysis window, 23 ms at 44.1 kHz. Short
	 *    enough to keep transients local, long enough to hold several periods
	 *    of a musical pitch (it holds 7 periods of 300 Hz, 2.3 of 100 Hz).
	 *    The synthesis hop is always half of it (50 % overlap, which is what
	 *    makes the Hann window constant-overlap-add).
	 *  * `searchRadius` = 128 — how far the alignment may move a grain, i.e.
	 *    the longest period that can be found: 44.1 kHz / 128 = 345 Hz. It is
	 *    also the linear term of the cost: halving it halves the render time.
	 */
	struct Parameters
	{
		int grainFrames = 1024;
		int searchRadius = 128;
	};

	AudioStretcher();
	~AudioStretcher() = default;

	AudioStretcher(const AudioStretcher&) = delete;
	AudioStretcher& operator=(const AudioStretcher&) = delete;

	/*! Sizes the state and fills the window table. Idempotent: a second call
	 *  with parameters already in force does nothing at all, so a caller may
	 *  prepare per instance without paying for a table it already has.
	 *  Clamps rather than throws — the engine has no way to report an
	 *  exception from the audio thread. */
	void prepare(const Parameters& params);
	//! The defaults form (a separate overload: a default argument of
	//! `Parameters()` is ill-formed inside this class).
	void prepare() { prepare(Parameters{}); }

	//! Drops the carry-over and puts the analysis cursor back at source frame 0.
	void reset();

	/*! Puts the analysis cursor at \a sourceFrame (source frames, may be
	 *  fractional). Only meaningful before a stream starts: the carry-over
	 *  region belongs to the previous position. */
	void seek(double sourceFrame);

	/*! Stretches: emits \a dstFrames output frames into \a dst, reading the
	 *  source span \a src (frames [0, srcFrames)) from the internal cursor.
	 *
	 *  \param speed source frames consumed per output frame. 1.0 is
	 *         "unwarped" (ignoring the sample-rate ratio), 2.0 plays the
	 *         source twice as fast — which is a time compression, pitch
	 *         unchanged. It is the reciprocal of `SamplePlayHandle`'s
	 *         converter ratio, and it is deliberately NOT a ratio: it says how
	 *         much source is eaten per output frame, which is the quantity the
	 *         warp mapping already speaks in.
	 *
	 *  Reads past either end of the source are silence, so the tail of a
	 *  window is emitted (and faded) rather than cut. Returns the number of
	 *  frames written — always \a dstFrames when the class is prepared, and 0
	 *  when it is not.
	 */
	f_cnt_t process(const SampleFrame* src, f_cnt_t srcFrames, SampleFrame* dst,
		f_cnt_t dstFrames, double speed);

	//! Where the analysis cursor stands in source frames: how much of the
	//! source has been consumed so far, to the sample.
	double sourcePosition() const { return m_sourcePos; }

	const Parameters& parameters() const { return m_params; }
	int grainFrames() const { return m_grain; }
	int synthesisHop() const { return m_hop; }
	bool isPrepared() const { return m_prepared; }

	//! The fixed caps, so a caller can size a scratch buffer or a test can
	//! prove a parameter was clamped.
	static constexpr int MaxGrainFrames = 1024;
	static constexpr int MaxSearchRadius = 256;

private:
	//! One synthesis hop: search, window, overlap-add, emit, carry.
	void produceHop(const SampleFrame* src, f_cnt_t srcFrames, double speed);

	Parameters m_params{};
	int m_grain = 0;
	int m_hop = 0;
	bool m_prepared = false;
	//! What is left of the previous grains in the synthesis window, i.e. the
	//! region the new grain is correlated against and added onto.
	std::array<SampleFrame, MaxGrainFrames> m_accum{};
	//! The frames of the hop currently being handed out (the synthesis hop is
	//! always half the grain, so this is the grain's own bound halved).
	std::array<SampleFrame, MaxGrainFrames / 2> m_hopOut{};
	std::array<float, MaxGrainFrames> m_window{};
	f_cnt_t m_hopReady = 0;
	f_cnt_t m_hopRead = 0;
	//! The analysis cursor, in source frames. Doubles because a fractional hop
	//! (a stretch that is not an integer ratio, or a sample-rate ratio folded
	//! in) must carry its fraction rather than round it away every hop.
	double m_sourcePos = 0.0;
	//! False until the first hop: the first grain has nothing to align to.
	bool m_started = false;
};

} // namespace lmms

#endif // LMMS_AUDIO_STRETCHER_H
