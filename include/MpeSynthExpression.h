/*
 * MpeSynthExpression.h - per-note MPE pressure and timbre applied to the
 *                        built-in (per-note) synthesisers (Q050-311)
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

#ifndef LMMS_MPE_SYNTH_EXPRESSION_H
#define LMMS_MPE_SYNTH_EXPRESSION_H

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class NotePlayHandle;
class SampleFrame;

/*! Q050-311: the built-in synthesisers never see MIDI (Instrument::handleMidiEvent
 *  is the base no-op), so the channel pressure and CC74 an MPE note carries were
 *  stored and forwarded but never heard on them. This is the per-note stage that
 *  makes them audible on every per-note instrument (TripleOscillator, Monstro,
 *  Organic, Kicker, ... - anything that is not single-streamed), applied by
 *  InstrumentTrack::processAudioBuffer right after the note's envelopes:
 *
 *    - pressure (MPE "Z", 0..127) -> note intensity: a gain of 1 + p/127
 *      (unity at 0, +6 dB at full pressure);
 *    - timbre (CC74, MPE "Y", 0..127) -> brightness: a high-band emphasis
 *      y = x + b * (x - lowpass(x)), b = 3 * t/127, around a 1 kHz one-pole
 *      corner (flat at 0, up to +12 dB above the corner at full CC74).
 *
 *  Both mappings are NEUTRAL at 0, and the stage is the exact identity there, so
 *  a note whose captured axes are 0 renders bit-identically to a note with none,
 *  and a note without expression (every old project) is never touched at all.
 *  Values change per period (live input, streams) and are ramped linearly across
 *  the period so a change does not click.
 *
 *  Realtime contract: plain arithmetic on the note's own State (a value member
 *  of NotePlayHandle) - no allocation, lock, I/O or signal.
 */
class LMMS_EXPORT MpeSynthExpression
{
public:
	static constexpr float FullScale = 127.f;
	//! Gain at full pressure (+6 dB).
	static constexpr float MaxPressureGain = 2.f;
	//! High-band emphasis amount at full CC74 (1 + 3 = +12 dB above the corner).
	static constexpr float MaxBrightness = 3.f;
	//! The one-pole lowpass corner the emphasis is taken around.
	static constexpr float BrightnessCornerHz = 1000.f;

	//! The per-note running state. Default-constructed by the handle (pool
	//! reuse placement-news the handle, so a reused note starts fresh).
	struct State
	{
		bool primed = false;
		float gain = 1.f;
		float brightness = 0.f;
		float low[2] = { 0.f, 0.f };
	};

	//! 1 + p/127, p clamped to 0..127.
	static float pressureGain( int pressure );
	//! 3 * t/127, t clamped to 0..127.
	static float brightness( int timbre );

	/*! Shape \a frames frames of \a buf in place towards the given axis values,
	 *  ramping from the state's last values (or starting at them on the note's
	 *  first block). Pure DSP, unit-tested in MpeSynthExpressionTest. */
	static void process( State& state, SampleFrame* buf, f_cnt_t frames,
		int pressure, int timbre, float sampleRate );

	/*! The playback entry point: \a buf already starts at the note's own offset.
	 *  Does nothing for a note without expression or stream. */
	static void applyToNote( NotePlayHandle& note, SampleFrame* buf, f_cnt_t frames );

private:
	static float lowpassCoefficient( float sampleRate );
	static void trackLowpass( State& state, const SampleFrame* buf, f_cnt_t frames, float a );
	static void shapeRamped( State& state, SampleFrame* buf, f_cnt_t frames, float a,
		float targetGain, float targetBrightness );
};

} // namespace lmms

#endif // LMMS_MPE_SYNTH_EXPRESSION_H
