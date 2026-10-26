/*
 * ClipPolarityTest.cpp - a clip's polarity and the waveform gain curve: the model,
 *                        the render, the commands and the file
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

/*! Q050-125: a clip's polarity (its rendered sign) and the gain curve the waveform is drawn
 *  with. The claims:
 *  1. Inverting a clip's polarity mirrors its rendered frames EXACTLY - equal magnitude, opposite
 *     sign, sample for sample - while the source buffer's own samples are unchanged, so a second
 *     clip playing the same buffer is unaffected.
 *  2. The polarity is additive state on the clip's element: written only when set, and an element
 *     without it loads non-inverted (every project written before this).
 *  3. `clip.set_polarity` sets, reads back and undoes as ONE step, and refuses a MIDI clip.
 *  4. The gain curve the waveform draws (clipGainCurveAt, scaled by the polarity sign) matches the
 *     audio the play handle renders - so the drawn waveform follows the gain, the envelope and the
 *     polarity together, which is the acceptance criterion's own sentence.
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <memory>
#include <vector>

#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClipOverlays.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A one-second buffer whose samples are NOT symmetric: a clipped ramp, so a sign flip is visible
//! and a magnitude comparison is meaningful.
std::shared_ptr<const SampleBuffer> rampBuffer()
{
	std::vector<SampleFrame> frames(kSampleRate);
	for (int i = 0; i < kSampleRate; ++i)
	{
		const float v = 0.9f * static_cast<float>(i) / kSampleRate - 0.4f;
		frames[static_cast<std::size_t>(i)] = SampleFrame(v, -v);
	}
	return std::make_shared<const SampleBuffer>(frames.data(), frames.size(), kSampleRate);
}

int oneSecondTicks()
{
	return static_cast<int>(kSampleRate / Engine::framesPerTick(kSampleRate));
}

//! Renders @a clip's own window through the production play handle into @a out.
void renderClip(SampleClip* clip, std::vector<SampleFrame>* out)
{
	SamplePlayHandle handle(clip);
	const auto total = static_cast<std::size_t>(handle.totalFrames());
	out->assign(total, SampleFrame(0.f, 0.f));
	std::size_t done = 0;
	while (done < total)
	{
		const std::size_t block = std::min<std::size_t>(1024, total - done);
		std::span<SampleFrame> span(out->data() + done, block);
		handle.play(span);
		done += block;
	}
}

} // namespace

class ClipPolarityTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Criterion 2's "without changing the source sample": the rendered frames flip sign exactly,
	//! the source buffer keeps every sample, and a SECOND clip on the same buffer is untouched.
	void invertingOneClipFlipsItsRenderAndLeavesTheSourceAlone()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(track != nullptr);
		auto* inverted = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		auto* plain = dynamic_cast<SampleClip*>(track->createClip(TimePos(4 * oneSecondTicks())));
		QVERIFY(inverted != nullptr && plain != nullptr);
		const auto shared = rampBuffer();
		inverted->setSampleBuffer(shared);
		plain->setSampleBuffer(shared);
		inverted->changeLength(TimePos(oneSecondTicks()));
		plain->changeLength(TimePos(oneSecondTicks()));

		std::vector<SampleFrame> beforeInverted;
		std::vector<SampleFrame> beforePlain;
		renderClip(inverted, &beforeInverted);
		renderClip(plain, &beforePlain);
		QVERIFY(!beforeInverted.empty());
		QCOMPARE(beforeInverted.size(), beforePlain.size());

		// The source samples, before anything is flipped.
		const SampleFrame* samples = shared->data();
		std::vector<float> source(shared->size());
		for (std::size_t i = 0; i < shared->size(); ++i) { source[i] = samples[i].left(); }

		ClipEdits edits = inverted->clipEdits();
		edits.polarityInverted = true;
		inverted->setClipEdits(edits);

		std::vector<SampleFrame> afterInverted;
		std::vector<SampleFrame> afterPlain;
		renderClip(inverted, &afterInverted);
		renderClip(plain, &afterPlain);

		QCOMPARE(afterInverted.size(), beforeInverted.size());
		for (std::size_t i = 0; i < afterInverted.size(); ++i)
		{
			QVERIFY2(std::abs(afterInverted[i].left() + beforeInverted[i].left()) < 1e-6f,
				qPrintable(QStringLiteral("frame %1 renders %2, the mirror of %3 is %4")
					.arg(i).arg(afterInverted[i].left()).arg(beforeInverted[i].left()).arg(-beforeInverted[i].left())));
			QVERIFY2(std::abs(afterInverted[i].right() + beforeInverted[i].right()) < 1e-6f,
				qPrintable(QStringLiteral("frame %1's right channel did not flip").arg(i)));
		}
		// The untouched clip renders identically: the flip is the clip's, not the buffer's.
		QCOMPARE(afterPlain.size(), beforePlain.size());
		for (std::size_t i = 0; i < afterPlain.size(); ++i)
		{
			QVERIFY2(afterPlain[i].left() == beforePlain[i].left() && afterPlain[i].right() == beforePlain[i].right(),
				qPrintable(QStringLiteral("inverting one clip changed the clip sharing its buffer at frame %1").arg(i)));
		}
		// And the shared source itself: not one sample moved (both channels).
		QVERIFY2(source.size() == shared->size(), "the source buffer changed size");
		for (std::size_t i = 0; i < source.size(); ++i)
		{
			QVERIFY2(samples[i].left() == source[i] && samples[i].right() == -source[i],
				qPrintable(QStringLiteral("the inversion wrote into the source at frame %1").arg(i)));
		}
	}

	//! Criterion 3 through the agent surface: set, read back, one undo, and the state command reports it.
	void theCommandSetsReadsBackAndUndoesInOneStep()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(rampBuffer());
		const QString id = control::clipIdOf(clip);

		QCOMPARE(clip->clipEdits().hasPolarity(), false);
		const ControlResult set = run(QStringLiteral("clip.set_polarity"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("inverted"), true}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("inverted")).toBool(), true);
		QCOMPARE(clip->clipEdits().hasPolarity(), true);

		// The same fact through the ordinary clip state, so an agent need not know this verb.
		const ControlResult state = run(QStringLiteral("arrangement.get_state"));
		QVERIFY(state.ok);
		bool found = false;
		for (const QJsonValue& entry : state.result.value(QStringLiteral("clips")).toArray())
		{
			if (entry.toObject().value(QStringLiteral("id")).toString() != id) { continue; }
			found = true;
			QCOMPARE(entry.toObject().value(QStringLiteral("inverted")).toBool(), true);
		}
		QVERIFY2(found, "the clip is not in arrangement.get_state");

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->clipEdits().hasPolarity(), false);

		// A request that changes nothing records no step: one undo after it reaches the real edit.
		const ControlResult again = run(QStringLiteral("clip.set_polarity"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("inverted"), true}});
		QVERIFY(again.ok);
		const ControlResult same = run(QStringLiteral("clip.set_polarity"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("inverted"), true}});
		QVERIFY(same.ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->clipEdits().hasPolarity(), false);

		// A MIDI clip carries no render for it, so it is refused rather than written and ignored.
		auto* instrument = Track::create(Track::Type::Instrument, song);
		Clip* midi = instrument->createClip(TimePos(0));
		const ControlResult refused = run(QStringLiteral("clip.set_polarity"),
			{{QStringLiteral("clip"), control::clipIdOf(midi)}, {QStringLiteral("inverted"), true}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
	}

	//! Repeating an unchanged clip gain does not shadow the real edit with an empty undo step.
	void settingTheSameGainRecordsNoSecondUndoStep()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(rampBuffer());
		const QString id = control::clipIdOf(clip);
		const auto setGain = [&](double db) {
			return run(QStringLiteral("clip.set_gain"),
				{{QStringLiteral("clip"), id}, {QStringLiteral("gain_db"), db}});
		};
		QVERIFY(setGain(-6.0).ok);
		QVERIFY(setGain(-6.0).ok);
		QCOMPARE(setGain(-61.0).errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(std::fabs(gainLinearToDb(clip->clipEdits().gain)) < 1e-5f);
	}

	//! Persistence: written only when set, read back exactly, and reset by absence so an old file
	//! loads non-inverted.
	void theClipElementCarriesItOnlyWhenInverted()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		clip->changeLength(TimePos(192));
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement neutral = clip->saveState(doc, root);
		QVERIFY2(!neutral.hasAttribute(QStringLiteral("invert")),
			"a non-inverted clip wrote the polarity attribute, so a neutral project no longer serialises as before");

		ClipEdits edits = clip->clipEdits();
		edits.polarityInverted = true;
		clip->setClipEdits(edits);
		const QDomElement written = clip->saveState(doc, root);
		QCOMPARE(written.attribute(QStringLiteral("invert")), QStringLiteral("1"));

		auto* loaded = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		loaded->restoreState(written);
		QCOMPARE(loaded->clipEdits().hasPolarity(), true);
		// Reset-on-absence: an element with neither attribute loads it off, even onto a clip that had it on.
		auto* legacy = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		legacy->setClipEdits(edits);
		legacy->restoreState(neutral);
		QCOMPARE(legacy->clipEdits().hasPolarity(), false);
	}

	//! The drawn waveform's claim: the gain curve the interface samples (clipGainCurveAt, times the
	//! polarity sign) is the multiplier the rendered audio actually got. One render with a gain, an
	//! envelope and a polarity flip; each measured frame is compared with the curve at its tick.
	void theDrawnCurveIsTheRenderedMultiplier()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(rampBuffer());
		const int ticks = oneSecondTicks();
		clip->changeLength(TimePos(ticks));
		ClipEdits edits;
		edits.gain = gainDbToLinear(-6.f);
		edits.polarityInverted = true;
		edits.envelope[0] = {0, 0.f};
		edits.envelope[1] = {ticks / 2, -12.f};
		edits.envelope[2] = {ticks, 0.f};
		edits.envelopeCount = 3;
		clip->setClipEdits(edits);

		std::vector<SampleFrame> plain;
		ClipEdits off = clip->clipEdits();
		off.polarityInverted = false;
		off.gain = 1.0f;
		off.envelopeCount = 0;
		clip->setClipEdits(off);
		renderClip(clip, &plain);
		clip->setClipEdits(edits);
		std::vector<SampleFrame> shaped;
		renderClip(clip, &shaped);
		QCOMPARE(shaped.size(), plain.size());
		QVERIFY(!plain.empty());

		const double framesPerTick = Engine::framesPerTick(kSampleRate);
		for (std::size_t i = 0; i < plain.size(); i += 997)
		{
			const double tick = static_cast<double>(i) / framesPerTick;
			const float drawn = clipGainCurveAt(edits, ticks, tick) * edits.polaritySign();
			const float expected = plain[i].left() * drawn;
			QVERIFY2(std::abs(shaped[i].left() - expected) < 2e-3f,
				qPrintable(QStringLiteral("frame %1 (tick %2) renders %3; the drawn curve predicts %4")
					.arg(i).arg(tick).arg(shaped[i].left()).arg(expected)));
		}
	}

	//! The waveform's per-column gain is a function of the TICK, not of a pixel: a partial repaint (the
	//! view's pixmap x origin is not 0) and a clip whose audio starts after its own start must both draw
	//! the curve the render applies. `drawnGainAt` is the one function the view hands the thumbnail.
	void theDrawnCurveIsIndependentOfTheRepaintedPixelRange()
	{
		const int ticks = 192;
		ClipEdits edits;
		edits.gain = gainDbToLinear(-6.f);
		edits.envelope[0] = {0, 0.f};
		edits.envelope[1] = {ticks / 2, -12.f};
		edits.envelope[2] = {ticks, 0.f};
		edits.envelopeCount = 3;
		// t=0.25 across the drawn audio must be the curve at tick 48 - whatever the view's pixel origin.
		QVERIFY(std::abs(gui::drawnGainAt(edits, ticks, 0, ticks, 0.25f)
			- clipGainCurveAt(edits, ticks, ticks * 0.25)) < 1e-6f);
		QVERIFY(std::abs(gui::drawnGainAt(edits, ticks, 0, ticks, 0.5f)
			- clipGainCurveAt(edits, ticks, ticks * 0.5)) < 1e-6f);
		// A clip whose audio starts 48 ticks into it: t=0 is tick 48, not tick 0, so the source tick is
		// what the curve is asked for at both ends of the drawn audio.
		QVERIFY(std::abs(gui::drawnGainAt(edits, ticks, 48, ticks - 48, 0.0f)
			- clipGainCurveAt(edits, ticks, 48)) < 1e-6f);
		QVERIFY(std::abs(gui::drawnGainAt(edits, ticks, 48, ticks - 48, 1.0f)
			- clipGainCurveAt(edits, ticks, ticks)) < 1e-6f);
		// Polarity and the clip gain ride the same multiplier, and t is clamped to the audio.
		ClipEdits inverted = edits;
		inverted.polarityInverted = true;
		QVERIFY(std::abs(gui::drawnGainAt(inverted, ticks, 0, ticks, 0.25f)
			+ gui::drawnGainAt(edits, ticks, 0, ticks, 0.25f)) < 1e-6f);
		QCOMPARE(gui::drawnGainAt(edits, ticks, 0, ticks, 2.0f), gui::drawnGainAt(edits, ticks, 0, ticks, 1.0f));
	}
};

QTEST_GUILESS_MAIN(ClipPolarityTest)
#include "ClipPolarityTest.moc"
