// Copyright (c) 2026 Zene Studio contributors
// SPDX-License-Identifier: GPL-2.0-or-later

//! Cluster new-p21-169: a sustained sfizz FlexEG stage of zero duration must jump straight to
//! its target level, as a zero-duration timed stage does, rather than dividing by its zero time.

#include <QtTest>

#include <absl/types/span.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "FlexEGDescription.h"
#include "FlexEnvelope.h"
#include "Resources.h"

class SfzFlexEnvelopeZeroStageTest : public QObject
{
	Q_OBJECT

private:
	static constexpr unsigned numFrames = 64;
	static constexpr double sampleRate = 48000.0;

	// Stage 0 is a zero-length point at startLevel, which the envelope passes at once. Stage 1 is
	// the sustain point, reached at startLevel and ramping to sustainLevel over sustainTime.
	static sfz::FlexEGDescription makeDescription(float startLevel, float sustainLevel, float sustainTime)
	{
		sfz::FlexEGPoint start;
		start.time = 0.0f;
		start.level = startLevel;

		sfz::FlexEGPoint sustain;
		sustain.time = sustainTime;
		sustain.level = sustainLevel;

		sfz::FlexEGDescription desc;
		desc.sustain = 1;
		desc.points = { start, sustain };
		return desc;
	}

	// Starts an envelope with no trigger delay and returns its first numFrames output frames.
	static std::vector<float> runEnvelope(const sfz::FlexEGDescription& desc)
	{
		sfz::Resources resources;
		sfz::FlexEnvelope envelope { resources };
		envelope.setSampleRate(sampleRate);
		envelope.configure(&desc);
		envelope.start(0);

		std::vector<float> out(numFrames, -1.0f);
		envelope.process(absl::MakeSpan(out));
		return out;
	}

private slots:
	// Without the guard, the sustained zero-time stage evaluates 1 / 0 and lands one float
	// rounding step away from its target (0.25 + (0.03 - 0.25) != 0.03 in float).
	void zeroDurationSustainHoldsTargetLevelExactly()
	{
		const float target = 0.03f;
		const std::vector<float> out = runEnvelope(makeDescription(0.25f, target, 0.0f));
		QVERIFY(std::all_of(out.begin(), out.end(), [target](float v) { return v == target; }));
	}

	// Covers the other direction: the zero-time sustain stage must never produce a non-finite
	// sample, and from a 0 start to 1 it reaches 1.
	void zeroDurationSustainStaysFinite()
	{
		const std::vector<float> out = runEnvelope(makeDescription(0.0f, 1.0f, 0.0f));
		for (float v : out) {
			QVERIFY(std::isfinite(v));
			QVERIFY(std::abs(v - 1.0f) < 1e-6f);
		}
	}

	// Guard against over-reach: a sustain stage with real duration still ramps from its start
	// level and has not reached its target after 64 frames of a 2 ms ramp.
	void nonZeroDurationSustainStillRamps()
	{
		const float startLevel = 0.25f;
		const float target = 0.75f;
		const std::vector<float> out = runEnvelope(makeDescription(startLevel, target, 0.002f));
		QVERIFY(out.front() > startLevel);
		QVERIFY(out.back() < target - 0.05f);
		QVERIFY(std::is_sorted(out.begin(), out.end()));
	}
};

QTEST_APPLESS_MAIN(SfzFlexEnvelopeZeroStageTest)

#include "SfzFlexEnvelopeZeroStageTest.moc"
