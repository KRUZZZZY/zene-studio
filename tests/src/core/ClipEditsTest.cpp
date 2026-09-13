/*
 * ClipEditsTest.cpp - the clip fade / clip-gain MODEL: the envelope arithmetic,
 *                     the dB conversion and the project-file round trip.
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

//! The audio half of this feature is proved by tests/src/core/ClipFadesRenderTest.cpp
//! (renders, hashes, the crossfade sum). This file is the other half: the
//! arithmetic those renders are compared against, and the serialisation rules
//! that decide whether a project changes on load at all.
//!
//! The claims, in the shortest true form:
//!  1. An equal-power pair of ramps sums to unity POWER
//!     (`g_in^2 + g_out^2 == 1`), a linear pair to unity amplitude. That identity
//!     IS the crossfade contract, and it is asserted at 101 points.
//!  2. The envelope is a function of the frame, measured over exactly the region
//!     the fade names - not a proportion of whatever the render happened to be.
//!  3. A neutral `ClipEdits` writes NO attribute at all onto the clip's element
//!     (docs/CLIP-CAPTURE-DESIGN.md invariant I9), a clip with edits round-trips
//!     through that element, and an element that predates the attributes loads
//!     as neutral.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>

#include <cmath>
#include <memory>
#include <vector>

#include "ClipEdits.h"

#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;

std::shared_ptr<const SampleBuffer> constantBuffer(int seconds)
{
	const auto frames = static_cast<std::size_t>(seconds) * kSampleRate;
	return std::make_shared<const SampleBuffer>(
		std::vector<SampleFrame>(frames, SampleFrame(0.5f, 0.5f)).data(), frames, kSampleRate);
}

} // namespace


class ClipEditsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	// ------------------------------------------------------------------ shapes

	//! The crossfade contract as arithmetic: an equal-power pair sums to unity
	//! POWER at every point of the range they share, and a linear pair to unity
	//! amplitude. These two facts are why `clip.crossfade` defaults to
	//! equal_power and why the render test can predict a summed curve at all.
	void anEqualPowerPairSumsToUnityPower()
	{
		for (int i = 0; i <= 100; ++i)
		{
			const double x = i / 100.0;
			const float in = fadeShapeGain(FadeShape::EqualPower, x);
			const float out = fadeShapeGain(FadeShape::EqualPower, 1.0 - x);
			QVERIFY2(std::fabs(double(in) * in + double(out) * out - 1.0) < 1e-6,
				qPrintable(QStringLiteral("equal-power pair at x=%1: %2^2 + %3^2 != 1")
					.arg(x).arg(in).arg(out)));
			QVERIFY2(std::fabs(fadeShapeGain(FadeShape::Linear, x)
				+ fadeShapeGain(FadeShape::Linear, 1.0 - x) - 1.0f) < 1e-6f,
				"a linear pair does not sum to unity amplitude");
		}
	}

	//! The shapes themselves, at the points where a wrong formula shows: the
	//! quiet end is exactly silent, the loud end exactly unity, and the
	//! exponential ramp is the square of the progress (the DAW's convention for
	//! a "logarithmic" fade).
	void theShapeFormulasAreTheOnesTheWireNames()
	{
		for (const auto shape : { FadeShape::Linear, FadeShape::Exponential, FadeShape::EqualPower })
		{
			QCOMPARE(fadeShapeGain(shape, 0.0), 0.0f);
			QCOMPARE(fadeShapeGain(shape, 1.0), 1.0f);
			// Out-of-range input is clamped, never extrapolated: a project file
			// cannot make this function return a negative gain.
			QCOMPARE(fadeShapeGain(shape, -3.0), 0.0f);
			QCOMPARE(fadeShapeGain(shape, 4.0), 1.0f);
		}
		QCOMPARE(fadeShapeGain(FadeShape::Linear, 0.25), 0.25f);
		QCOMPARE(fadeShapeGain(FadeShape::Exponential, 0.5), 0.25f);

		FadeShape parsed = FadeShape::Linear;
		QVERIFY(fadeShapeFromName(QStringLiteral("equal_power"), &parsed));
		QCOMPARE(static_cast<int>(parsed), static_cast<int>(FadeShape::EqualPower));
		QVERIFY(fadeShapeFromName(QStringLiteral("exponential"), &parsed));
		QCOMPARE(static_cast<int>(parsed), static_cast<int>(FadeShape::Exponential));
		// An unknown name is refused and leaves the caller's value alone.
		QVERIFY(!fadeShapeFromName(QStringLiteral("s-curve"), &parsed));
		QCOMPARE(static_cast<int>(parsed), static_cast<int>(FadeShape::Exponential));
		QCOMPARE(fadeShapeName(FadeShape::EqualPower), QStringLiteral("equal_power"));
	}

	// ------------------------------------------------------------------- gain

	//! dB is the file format and the wire format; the model carries the linear
	//! factor. -6 dB is very nearly half, and the round trip is lossless to
	//! within float.
	void theGainConversionsAreInverses()
	{
		QVERIFY(std::fabs(gainDbToLinear(0.0f) - 1.0f) < 1e-6f);
		QVERIFY(std::fabs(gainLinearToDb(0.5f) + 6.0206f) < 1e-3f);
		for (const float db : { -60.0f, -24.0f, -6.0f, -0.5f, 0.0f, 3.0f, 24.0f })
		{
			QVERIFY2(std::fabs(gainLinearToDb(gainDbToLinear(db)) - db) < 1e-3f,
				qPrintable(QStringLiteral("%1 dB did not survive the round trip").arg(db)));
		}
		// Silence has no finite dB reading; the wire reports a floor rather than
		// an infinity JSON cannot carry.
		QVERIFY(gainLinearToDb(0.0f) < -100.0f);
	}

	// ---------------------------------------------------------------- envelope

	//! The envelope is a function of the frame inside the region the fade names.
	//! A fade-in of 100 frames is silent at frame 0, unity from frame 100 on, and
	//! strictly increasing between; a fade-out of 100 frames is its mirror on the
	//! LAST 100 frames of the clip, wherever the clip ends.
	void theEnvelopeRampsOverExactlyTheRegionTheFadeNames()
	{
		constexpr f_cnt_t kClip = 1000;

		// fade-in only
		QCOMPARE(clipFadeGainAt(0, kClip, 100, 0, FadeShape::Linear, FadeShape::Linear), 0.0f);
		QCOMPARE(clipFadeGainAt(100, kClip, 100, 0, FadeShape::Linear, FadeShape::Linear), 1.0f);
		QCOMPARE(clipFadeGainAt(999, kClip, 100, 0, FadeShape::Linear, FadeShape::Linear), 1.0f);
		QCOMPARE(clipFadeGainAt(50, kClip, 100, 0, FadeShape::Linear, FadeShape::Linear), 0.5f);

		// fade-out only: the region is the clip's LAST 100 frames, not its first
		QCOMPARE(clipFadeGainAt(899, kClip, 0, 100, FadeShape::Linear, FadeShape::Linear), 1.0f);
		QCOMPARE(clipFadeGainAt(900, kClip, 0, 100, FadeShape::Linear, FadeShape::Linear), 1.0f);
		QCOMPARE(clipFadeGainAt(950, kClip, 0, 100, FadeShape::Linear, FadeShape::Linear), 0.5f);
		// The region's LAST frame is one step above silence rather than exactly
		// silent: the region is [clipFrames - fadeFrames, clipFrames) and the
		// gain at its last frame is shape(1/fadeFrames). Choosing that over a
		// frame-shifted ramp is what makes this ramp exactly shape(1 - x) at
		// every point, which is what makes the crossfade identity exact.
		QCOMPARE(clipFadeGainAt(999, kClip, 0, 100, FadeShape::Linear, FadeShape::Linear), 0.01f);

		// monotonic across both ramps, which is what "no click" rests on
		float previous = -1.0f;
		for (f_cnt_t f = 0; f < 100; ++f)
		{
			const float value = clipFadeGainAt(f, kClip, 100, 0, FadeShape::Linear, FadeShape::Linear);
			QVERIFY2(value >= previous, "the fade-in is not monotonic");
			previous = value;
		}
		previous = 2.0f;
		for (f_cnt_t f = 900; f < kClip; ++f)
		{
			const float value = clipFadeGainAt(f, kClip, 0, 100, FadeShape::Linear, FadeShape::Linear);
			QVERIFY2(value <= previous, "the fade-out is not monotonic");
			previous = value;
		}

		// no fades at all: the identity the default path relies on
		for (f_cnt_t f = 0; f < kClip; f += 97)
		{
			QCOMPARE(clipFadeGainAt(f, kClip, 0, 0, FadeShape::Linear, FadeShape::Linear), 1.0f);
		}
		// a degenerate clip is unity rather than a division by zero
		QCOMPARE(clipFadeGainAt(0, 0, 10, 10, FadeShape::Linear, FadeShape::Linear), 1.0f);
	}

	// ------------------------------------------------------------- neutrality

	//! `isNeutral` is the single predicate the audio path branches on. It has to
	//! be true for every default and false for anything that would change audio.
	void onlyADefaultOrUnityClipIsNeutral()
	{
		QVERIFY(ClipEdits{}.isNeutral());
		ClipEdits unity = ClipEdits{};
		unity.gain = 1.0f;
		QVERIFY(unity.isNeutral());
		QVERIFY(ClipEdits{} == unity);

		ClipEdits soft = ClipEdits{};
		soft.gain = gainDbToLinear(-0.01f);
		QVERIFY(!soft.isNeutral());

		ClipEdits faded = ClipEdits{};
		faded.fadeInTicks = 1;
		QVERIFY(!faded.isNeutral());
		QVERIFY(faded.hasFade());

		ClipEdits outOnly = ClipEdits{};
		outOnly.fadeOutTicks = 1;
		QVERIFY(!outOnly.isNeutral());
	}

	// ------------------------------------------------------------- persistence

	//! I9: a clip with neutral edits writes NO new attribute, a clip with edits
	//! round-trips through its own element, and a file that predates the
	//! attributes loads as neutral.
	void editsRoundTripAndANeutralClipWritesNothing()
	{
		const QStringList attributes = { QStringLiteral("gain"), QStringLiteral("fadein"),
			QStringLiteral("fadeout"), QStringLiteral("fadeinshape"),
			QStringLiteral("fadeoutshape") };
		SampleTrack track(Engine::getSong());
		auto* subject = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(subject != nullptr);
		subject->setSampleBuffer(constantBuffer(1));

		QDomDocument doc;
		QDomElement neutralElement = doc.createElement(QStringLiteral("sampleclip"));
		doc.appendChild(neutralElement);
		subject->saveSettings(doc, neutralElement);
		for (const QString& attribute : attributes)
		{
			QVERIFY2(!neutralElement.hasAttribute(attribute),
				qPrintable(QStringLiteral("a neutral clip wrote '%1' - I9 is broken")
					.arg(attribute)));
		}

		ClipEdits edits = ClipEdits{};
		edits.gain = gainDbToLinear(-3.0f);
		edits.fadeInTicks = 48;
		edits.fadeOutTicks = 96;
		edits.fadeInShape = FadeShape::Exponential;
		edits.fadeOutShape = FadeShape::EqualPower;
		subject->setClipEdits(edits);

		QDomDocument edited;
		QDomElement editedElement = edited.createElement(QStringLiteral("sampleclip"));
		edited.appendChild(editedElement);
		subject->saveSettings(edited, editedElement);
		for (const QString& attribute : attributes)
		{
			QVERIFY2(editedElement.hasAttribute(attribute),
				qPrintable(QStringLiteral("an edited clip wrote no '%1'").arg(attribute)));
		}

		auto* reloaded = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(reloaded != nullptr);
		reloaded->loadSettings(editedElement);
		QCOMPARE(reloaded->clipEdits().fadeInTicks, 48);
		QCOMPARE(reloaded->clipEdits().fadeOutTicks, 96);
		QCOMPARE(static_cast<int>(reloaded->clipEdits().fadeInShape),
			static_cast<int>(FadeShape::Exponential));
		QCOMPARE(static_cast<int>(reloaded->clipEdits().fadeOutShape),
			static_cast<int>(FadeShape::EqualPower));
		QVERIFY2(std::fabs(reloaded->clipEdits().gain - edits.gain) < 1e-5f,
			"the clip gain did not survive a save/load round trip");

		// An OLD project: an element with none of the attributes, loaded onto a
		// clip that already carries edits, must come back neutral.
		auto* legacy = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(legacy != nullptr);
		legacy->setClipEdits(edits);
		legacy->loadSettings(neutralElement);
		QVERIFY2(legacy->clipEdits().isNeutral(),
			"a project file without the fade attributes did not load as neutral");

		// A malformed file - a negative length, an out-of-range shape index - is
		// clamped to the nearest legal value rather than carried into the render.
		QDomDocument malformed;
		QDomElement bad = malformed.createElement(QStringLiteral("sampleclip"));
		malformed.appendChild(bad);
		bad.setAttribute(QStringLiteral("fadein"), -20);
		bad.setAttribute(QStringLiteral("fadeout"), -5);
		bad.setAttribute(QStringLiteral("fadeinshape"), 99);
		auto* repaired = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(repaired != nullptr);
		repaired->loadSettings(bad);
		QCOMPARE(repaired->clipEdits().fadeInTicks, 0);
		QCOMPARE(repaired->clipEdits().fadeOutTicks, 0);
		QCOMPARE(static_cast<int>(repaired->clipEdits().fadeInShape),
			static_cast<int>(FadeShape::Linear));
	}
};

QTEST_GUILESS_MAIN(ClipEditsTest)
#include "ClipEditsTest.moc"
