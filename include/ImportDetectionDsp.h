/*
 * ImportDetectionDsp.h - the detection ARITHMETIC for import detection (feature
 *                        row 34 of docs/FEATURE-LIST-0.3.0.md): a tempo (BPM)
 *                        estimate from an onset envelope and a key estimate from
 *                        a chroma vector.
 *
 * WHY THIS IS A SEPARATE, Qt-FREE UNIT. The arithmetic has no Qt type, no file
 * I/O and no engine state: it takes one mono channel of floats and returns two
 * plain values. That is what lets the registered QTest feed it a SYNTHESISED
 * signal with a known answer, and what lets a developer compile this one file
 * with a bare compiler on a box where the whole product does not build - the
 * proof this lane actually ran.
 *
 * NO NEW DEPENDENCY (the decision BACKLOG.md item 10 asks for explicitly:
 * "aubio is GPL-2.0-or-later (compatible), Essentia is AGPL-3.0 (a blocker), and
 * a hand-rolled spectral-flux detector needs no dependency at all. Choose
 * explicitly."). The FFT below is a hand-rolled iterative radix-2 transform, so
 * this feature adds NO library, NO licence question and NO link requirement:
 * the onset envelope is spectral flux and the key is chroma over the same
 * transform.
 *
 * WHAT IS NOT HERE: no file reading (SampleDecoder does that), no project state
 * (ProjectKey does that), no command surface (ControlCommandsDetect.cpp), and no
 * claim about real music. The accuracy this lane can PROVE is stated in
 * docs/IMPORT-DETECTION.md: it is measured on synthesised input with a known
 * answer, and real-world detection accuracy is UNVERIFIED on this box.
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
 */

#ifndef LMMS_IMPORT_DETECTION_DSP_H
#define LMMS_IMPORT_DETECTION_DSP_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace lmms
{

namespace detection
{

// ---------------------------------------------------------------------------
// The window, the band and the bounds. Every one of them is reported on the wire
// so a caller never has to guess what was measured.
// ---------------------------------------------------------------------------

//! Tempo analysis: 1024-frame Hann frames, 512-frame hop (23.2 ms at 44.1 kHz).
constexpr int TempoFrameSize = 1024;
constexpr int TempoHopSize = 512;

/*! Key analysis: a longer window, because a chroma bin has to resolve a semitone
 *  at the bottom of the band (a 8192-point transform at 44.1 kHz is 5.4 Hz per
 *  bin; C3 at 130.8 Hz is 7.8 Hz wide, so the low end is usable and the band
 *  below ChromaMinHz is EXCLUDED rather than guessed at). */
constexpr int ChromaFrameSize = 8192;
constexpr int ChromaHopSize = 4096;
//! The band a chroma bin is filled from. Below it a semitone is narrower than a
//! bin; above it the harmonic series of everything has already piled up.
constexpr double ChromaMinHz = 110.0;
constexpr double ChromaMaxHz = 3000.0;

/*! The band the tempo estimate may land in. 40..240 BPM is the range this
 *  project DECLARES: outside it a "tempo" is either a subdivision or a
 *  multi-bar grouping, and neither is what the tempo map wants. The half/double
 *  ambiguity WITHIN the band is a stated limit (docs/IMPORT-DETECTION.md): a
 *  track at 120 BPM can be reported as 240, and this lane does not resolve it. */
constexpr double MinDetectionBpm = 40.0;
constexpr double MaxDetectionBpm = 240.0;

//! How much of a file is analysed at most, in seconds. The analysis is offline
//! but it is not free, and beyond a minute the extra frames do not change a
//! tempo or a key that a whole track agrees on. A caller may lower it.
constexpr double MaxAnalysisSeconds = 300.0;
constexpr double DefaultAnalysisSeconds = 60.0;

/*! A template covering more than this many of the twelve pitch classes carries
 *  no key information (the 12-note set is every note), so a caller's candidate
 *  list is filtered by this rule before it is scored. Stated as a bound rather
 *  than hidden: the filter is what keeps a noisy recording from "detecting"
 *  Chromatic. */
constexpr int MaxTemplateDegrees = 9;

//! One tempo estimate.
struct TempoEstimate
{
	bool found = false;
	//! Beats per minute, from the autocorrelation of the onset envelope.
	double bpm = 0.0;
	/*! The normalised autocorrelation peak the lag was chosen on: the envelope's
	 *  correlation with itself at that lag, in 0..1. It is a MEASURE OF HOW
	 *  PERIODIC the onset envelope is at that lag - not a probability, and not
	 *  an accuracy figure. */
	double confidence = 0.0;
	//! The lag the estimate came from, in seconds (60 / bpm, rounded to a hop).
	double periodSeconds = 0.0;
	//! The FIRST transient the onset envelope carries: where a clip would have
	//! to start for its first hit to land on the grid.
	double firstOnsetSeconds = 0.0;
	std::int64_t firstOnsetFrame = 0;
	//! How many transients the envelope found over the analysed span.
	int onsets = 0;
};

//! One key estimate.
struct KeyEstimate
{
	bool found = false;
	/*! Index into the candidate masks the caller passed in - the caller owns the
	 *  vocabulary (the engine layer maps this index to the pre-existing
	 *  ChordTable scale name, so this unit invents no key vocabulary). */
	int candidateIndex = -1;
	//! 0 == C, 9 == A. The tonic the winning template was transposed to.
	int tonicPitchClass = 0;
	//! Degree count of the winning template (5..MaxTemplateDegrees).
	int templateDegrees = 0;
	//! The winning template's score (see templateScore()).
	double score = 0.0;
	/*! best - runner-up. A RANK MARGIN in the score's own units: it says how
	 *  much better the winner is than the next candidate, not how likely it is
	 *  to be right. */
	double margin = 0.0;
	//! The chroma the estimate was made from, normalised so its largest bin is 1.
	std::array<double, 12> chroma{};
};

//! The name of the tempo method, as the control surface reports it.
constexpr const char* TempoMethodName = "spectral-flux-autocorrelation";
//! The name of the key method, as the control surface reports it.
constexpr const char* KeyMethodName = "chroma-tonic-weighted-set-match";

//! "C", "C#", "D", ... for 0..11; "" outside that range.
const char* pitchClassName(int pitchClass);

//! How many bits of \a mask are set (the template's degree count).
int maskDegreeCount(std::uint32_t mask);

/*! The score of one candidate: the chroma mass sitting on the TONIC of the
 *  template plus half the mean chroma mass sitting on its other degrees.
 *
 *  WHY THE TONIC IS WEIGHTED. The template's degree SET alone cannot tell
 *  relative keys apart - C major and A aeolian are the same seven pitch classes
 *  - so a plain set match ties on exactly the question a key estimate exists to
 *  answer. The chroma mass on the tonic breaks that tie, and it is the same
 *  signal the ear uses: music in C major sits on C.
 *
 *  The half-weight is this project's own, DECLARED rather than borrowed: no
 *  published key-profile constant (Krumhansl-Kessler or otherwise) is copied
 *  into this file, because a borrowed profile would carry a claim about real
 *  music this lane cannot measure. See docs/IMPORT-DETECTION.md section 4. */
double templateScore(const std::array<double, 12>& chroma, std::uint32_t mask, int tonic);

/*! The tempo estimate. \a mono is one channel of \a frames floats at
 *  \a sampleRate; at most \a maxSeconds of it is read from the start. Silence or
 *  a signal with no transients reports found == false - a refusal, not a guess.
 *  Allocation happens here: this is an IMPORT-TIME call, never an audio-thread
 *  one (the realtime rule of AGENTS.md section 4). */
TempoEstimate estimateTempo(const float* mono, std::size_t frames, int sampleRate,
	double maxSeconds = DefaultAnalysisSeconds);

/*! The key estimate. \a candidateMasks is the caller's vocabulary: every scale
 *  template the caller is willing to name, as a 12-bit mask of pitch classes
 *  relative to the tonic. Masks with more than MaxTemplateDegrees bits are
 *  skipped (and reported by index, so the caller can see which). */
KeyEstimate estimateKey(const float* mono, std::size_t frames, int sampleRate,
	std::span<const std::uint32_t> candidateMasks,
	double maxSeconds = DefaultAnalysisSeconds);

} // namespace detection

} // namespace lmms

#endif // LMMS_IMPORT_DETECTION_DSP_H
