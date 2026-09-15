/*
 * import-detection-proof.cpp - the box-local proof for import detection
 *                              (feature row 34 of docs/FEATURE-LIST-0.3.0.md).
 *
 * WHY THIS FILE EXISTS. The registered proof for this feature is the QTest
 * tests/src/core/ImportDetectionTest.cpp (ctest `ImportDetectionTest`), which
 * links the whole product. A box that cannot build the product cannot run that
 * test, and "the accuracy is stated honestly" then has nothing behind it. This
 * driver compiles the SAME arithmetic - include/ImportDetectionDsp.h and
 * src/core/ImportDetectionDsp.cpp, the Qt-free unit the engine calls - with one
 * g++ command, synthesises the same fixtures with a KNOWN answer, and prints
 * what it measured. It is tooling, not product code: it lives under tools/ and
 * is measured by the tools-scope ratchets.
 *
 * Build and run (no Qt, no FFTW, no libsndfile):
 *   g++ -std=c++20 -O2 -Iinclude tools/import-detection-proof.cpp \
 *       src/core/ImportDetectionDsp.cpp -o /tmp/import-detection-proof
 *   /tmp/import-detection-proof; echo EXIT=$?
 *
 * WHAT IT PROVES: that the arithmetic recovers the tempo and the key of a
 * SYNTHESISED signal whose answer is known by construction (a click track at a
 * chosen BPM, an A-major scale with an A bass at a chosen pitch). WHAT IT DOES
 * NOT PROVE, and the reason docs/IMPORT-DETECTION.md says so in a sentence: the
 * accuracy of the same estimate on REAL MUSIC is unverified on this box - no
 * real-world corpus was measured here.
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

#include "ImportDetectionDsp.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace lmms::detection;

namespace
{

constexpr int kSampleRate = 44100;
//! The click track's own answer, and the tolerance this proof holds itself to.
constexpr double kClickBpmA = 128.0;
constexpr double kClickBpmB = 90.0;
constexpr double kBpmTolerance = 0.5;
//! The scale fixture's answer: A major, 220 Hz root.
constexpr int kExpectedTonic = 9; // A
constexpr double kTonicHz = 220.0;

int failures = 0;

void check(const char* name, bool passed, const char* detail)
{
	std::printf("%-56s %s  %s\n", name, passed ? "PASS" : "FAIL", detail);
	if (!passed) { ++failures; }
}

/*! A click track: a short decaying 1 kHz burst every beat, starting at
 *  \a firstBeatSeconds. The answer is known by construction. */
std::vector<float> clickTrack(double bpm, double seconds, double firstBeatSeconds)
{
	std::vector<float> mono(static_cast<std::size_t>(seconds * kSampleRate), 0.0f);
	const double beatSeconds = 60.0 / bpm;
	const int burstFrames = kSampleRate / 40; // 25 ms
	for (double beat = firstBeatSeconds; beat < seconds; beat += beatSeconds)
	{
		const std::size_t start = static_cast<std::size_t>(beat * kSampleRate);
		for (int i = 0; i < burstFrames && start + i < mono.size(); ++i)
		{
			const double envelope = std::exp(-8.0 * i / burstFrames);
			mono[start + i] += static_cast<float>(
				0.8 * envelope * std::sin(2.0 * std::numbers::pi * 1000.0 * i / kSampleRate));
		}
	}
	return mono;
}

/*! A tone at \a hz for \a seconds, with short fades so it has no click of its
 *  own (a click would be a transient the tempo half would see). */
void addTone(std::vector<float>& mono, double hz, double startSeconds, double seconds, double amplitude)
{
	const std::size_t start = static_cast<std::size_t>(startSeconds * kSampleRate);
	const std::size_t length = static_cast<std::size_t>(seconds * kSampleRate);
	const std::size_t fade = static_cast<std::size_t>(0.02 * kSampleRate);
	for (std::size_t i = 0; i < length && start + i < mono.size(); ++i)
	{
		double envelope = 1.0;
		if (i < fade) { envelope = static_cast<double>(i) / fade; }
		else if (i + fade >= length) { envelope = static_cast<double>(length - i) / fade; }
		mono[start + i] += static_cast<float>(
			amplitude * envelope * std::sin(2.0 * std::numbers::pi * hz * i / kSampleRate));
	}
}

//! The A major scale (A B C# D E F# G# A) over an A bass drone: the answer is
//! "A major" by construction, and the bass is what makes the tonic measurable.
std::vector<float> majorScaleFixture()
{
	std::vector<float> mono(static_cast<std::size_t>(8.0 * kSampleRate), 0.0f);
	const double scale[8] = {220.00, 246.94, 277.18, 293.66, 329.63, 369.99, 415.30, 440.00};
	for (int repeat = 0; repeat < 2; ++repeat)
	{
		for (int note = 0; note < 8; ++note)
		{
			addTone(mono, scale[note], repeat * 3.0 + note * 0.35, 0.32, 0.35);
		}
	}
	addTone(mono, 110.0, 0.0, 8.0, 0.45); // the tonic an octave down
	return mono;
}

/*! The candidate vocabulary this proof scores against. The PRODUCT fills this
 *  list from the pre-existing ChordTable scale table (see
 *  src/core/ImportDetection.cpp); the proof carries the two templates that
 *  matter for its fixtures, as masks relative to the tonic. */
std::vector<std::uint32_t> candidateMasks()
{
	return {
		0x0AB5, // major      {0,2,4,5,7,9,11}
		0x05AD, // minor      {0,2,3,5,7,8,10}
	};
}

} // namespace

int main()
{
	std::printf("import-detection-proof: DSP unit %s, %s\n", TempoMethodName, KeyMethodName);
	std::printf("sample rate %d Hz, tempo band %.0f..%.0f BPM, chroma band %.0f..%.0f Hz\n",
		kSampleRate, MinDetectionBpm, MaxDetectionBpm, ChromaMinHz, ChromaMaxHz);

	// --- tempo, two known answers ------------------------------------------
	const struct { const char* name; double bpm; } clicks[] = {
		{"click track synthesised at 128 BPM", kClickBpmA},
		{"click track synthesised at 90 BPM", kClickBpmB},
	};
	for (const auto& click : clicks)
	{
		const std::vector<float> mono = clickTrack(click.bpm, 10.0, 0.5);
		const TempoEstimate tempo = estimateTempo(mono.data(), mono.size(), kSampleRate);
		char detail[192];
		std::snprintf(detail, sizeof(detail),
			"detected %.3f BPM (confidence %.3f, %d transients, first at %.3f s)",
			tempo.bpm, tempo.confidence, tempo.onsets, tempo.firstOnsetSeconds);
		check(click.name, tempo.found && std::abs(tempo.bpm - click.bpm) <= kBpmTolerance, detail);
	}

	// The first transient is part of the answer: the fixture's first click is at
	// 0.5 s, and the estimate has to find it.
	{
		const std::vector<float> mono = clickTrack(kClickBpmA, 10.0, 0.5);
		const TempoEstimate tempo = estimateTempo(mono.data(), mono.size(), kSampleRate);
		char detail[128];
		std::snprintf(detail, sizeof(detail), "first transient at %.3f s (synthesised at 0.500 s)",
			tempo.firstOnsetSeconds);
		check("the first transient is found", std::abs(tempo.firstOnsetSeconds - 0.5) <= 0.03, detail);
	}

	// --- key, a known answer ------------------------------------------------
	{
		const std::vector<float> mono = majorScaleFixture();
		const std::vector<std::uint32_t> masks = candidateMasks();
		const KeyEstimate key = estimateKey(mono.data(), mono.size(), kSampleRate, masks);
		const char* tonic = key.candidateIndex >= 0 && key.tonicPitchClass == kExpectedTonic
			? pitchClassName(key.tonicPitchClass) : "?";
		char detail[256];
		std::snprintf(detail, sizeof(detail),
			"detected %s (pc %d, template %d, score %.3f, margin %.3f) for a fixture rooted at %.1f Hz",
			tonic, key.tonicPitchClass, key.candidateIndex, key.score, key.margin, kTonicHz);
		check("A major scale over an A bass detects tonic A, major",
			key.found && key.tonicPitchClass == kExpectedTonic && key.candidateIndex == 0, detail);
	}

	// --- the negatives: what the estimate does when there is nothing to find ---
	{
		const std::vector<float> silence(static_cast<std::size_t>(4.0 * kSampleRate), 0.0f);
		const TempoEstimate tempo = estimateTempo(silence.data(), silence.size(), kSampleRate);
		check("silence reports no tempo rather than a number", !tempo.found, "found == false");

		std::vector<float> steady(static_cast<std::size_t>(6.0 * kSampleRate));
		for (std::size_t i = 0; i < steady.size(); ++i)
		{
			steady[i] = static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * 440.0 * i / kSampleRate));
		}
		const TempoEstimate tone = estimateTempo(steady.data(), steady.size(), kSampleRate);
		char detail[128];
		std::snprintf(detail, sizeof(detail), "found %d, %d transients", tone.found ? 1 : 0, tone.onsets);
		check("a steady tone with no transients reports no tempo", !tone.found, detail);

		const std::vector<std::uint32_t> noMasks;
		const KeyEstimate none = estimateKey(steady.data(), steady.size(), kSampleRate, noMasks);
		check("an empty vocabulary reports no key", !none.found, "found == false");
	}

	std::printf("\n%d check(s) failed\n", failures);
	return failures == 0 ? 0 : 1;
}
