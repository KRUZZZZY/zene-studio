/*
 * ClipOffsetFadePairTest.cpp - an offset fade is a PAIR (round 3 of the owner decision of 2026-10-08): a fade-out that
 *                              ends early always has the fade-in that brings the clip back, so the gain never steps;
 *                              a lone offset is refused by clip.set_fade, dropped on load, and ignored by the render
 *                              and the gain curve. And a crossfade a clip is too short for is reported, not counted.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

#include "ArrangementEdits.h"
#include "ClipEdits.h"
#include "ControlClipLinkSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr double kHalfPi = 1.5707963267948966;

std::shared_ptr<const SampleBuffer> constantBuffer(float level)
{
	const std::size_t frames = 4 * 44100;
	return std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(frames, SampleFrame(level, level)).data(), frames, 44100);
}

//! The widest step between neighbouring ticks of \p edits' gain curve over a clip \p length ticks long.
double widestStep(const ClipEdits& edits, int length)
{
	double widest = 0.0;
	double previous = clipGainCurveAt(edits, length, 0);
	for (int tick = 1; tick < length; ++tick)
	{
		const double gain = clipGainCurveAt(edits, length, tick);
		widest = std::max(widest, std::fabs(gain - previous));
		previous = gain;
	}
	return widest;
}

} // namespace

class ClipOffsetFadePairTest : public QObject
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

	void init() { Engine::getSong()->clearProject(); }

	//! clip.set_fade: a lone or mismatched offset is refused (nothing changes); a pair is accepted and clears with 0.
	void setFadeRefusesALoneOffsetAndAcceptsAPair()
	{
		SampleClip* clip = sampleClipAt(0, 400);
		const QString id = control::clipIdOf(clip);
		const auto setFade = [&](const QJsonObject& more) {
			QJsonObject args{{QStringLiteral("clip"), id}};
			for (auto it = more.begin(); it != more.end(); ++it) { args.insert(it.key(), it.value()); }
			return ControlRegistry::instance()->invoke(QStringLiteral("clip.set_fade"), args);
		};
		QVERIFY(!setFade({{QStringLiteral("fade_out"), 50}, {QStringLiteral("fade_out_offset"), 100}}).ok);
		QVERIFY(!setFade({{QStringLiteral("fade_in"), 50}, {QStringLiteral("fade_in_offset"), 100}}).ok);
		QCOMPARE(clip->clipEdits(), ClipEdits{});
		// A pair whose fade-in does not start where the fade-out ends (400 - 100 = 300) is refused too.
		QVERIFY(!setFade({{QStringLiteral("fade_out"), 50}, {QStringLiteral("fade_out_offset"), 100},
			{QStringLiteral("fade_in"), 50}, {QStringLiteral("fade_in_offset"), 250}}).ok);
		const ControlResult pair = setFade({{QStringLiteral("fade_out"), 50}, {QStringLiteral("fade_out_offset"), 100},
			{QStringLiteral("fade_in"), 50}, {QStringLiteral("fade_in_offset"), 300}});
		QVERIFY2(pair.ok, qPrintable(pair.errorMessage));
		QCOMPARE(clip->clipEdits().fadeInOffsetTicks, 300);
		// Editing a length leaves the offsets alone and is accepted; both offsets 0 clear the pair.
		QVERIFY(setFade({{QStringLiteral("fade_in"), 20}}).ok);
		QVERIFY(setFade({{QStringLiteral("fade_in_offset"), 0}, {QStringLiteral("fade_out_offset"), 0}}).ok);
		QCOMPARE(clip->clipEdits().fadeOutOffsetTicks, 0);
	}

	//! A project file with a lone offset loads as the ordinary fades; a pair loads as it was written.
	void aLoneOffsetLoadsAsTheOrdinaryFades()
	{
		SampleClip* clip = sampleClipAt(0, 400);
		QDomDocument doc;
		QDomElement lone = doc.createElement(QStringLiteral("clip"));
		clip->saveSettings(doc, lone);
		lone.setAttribute(QStringLiteral("fadeout"), 50);
		lone.setAttribute(QStringLiteral("fadeoutoffset"), 100);
		clip->loadSettings(lone);
		QCOMPARE(clip->clipEdits().fadeOutTicks, 50);
		QCOMPARE(clip->clipEdits().fadeOutOffsetTicks, 0);

		lone.setAttribute(QStringLiteral("fadein"), 50);
		lone.setAttribute(QStringLiteral("fadeinoffset"), 300);
		clip->loadSettings(lone);
		QCOMPARE(clip->clipEdits().fadeInOffsetTicks, 300);
		QCOMPARE(clip->clipEdits().fadeOutOffsetTicks, 100);
	}

	//! The gain curve (clip.fade_curve, the automation editor) never steps by more than a ramp's own slope, for a lone
	//! offset (ignored), a pair whose fade-in drifted (repaired) and a proper pair.
	void theGainCurveHasNoStep()
	{
		const double slope = kHalfPi / 50 * 1.05;
		ClipEdits lone;
		lone.fadeOutTicks = 50;
		lone.fadeOutOffsetTicks = 100;
		lone.fadeOutShape = FadeShape::EqualPower;
		QVERIFY(widestStep(lone, 400) <= slope);
		QCOMPARE(withValidOffsetFades(lone, 400).fadeOutOffsetTicks, 0);

		ClipEdits stale = lone;  // a clip resized after the crossfade: its fade-in no longer starts where the fade-out ends
		stale.fadeInTicks = 50;
		stale.fadeInOffsetTicks = 120;
		stale.fadeInShape = FadeShape::EqualPower;
		QVERIFY(widestStep(stale, 400) <= slope);
		QCOMPARE(withValidOffsetFades(stale, 400).fadeInOffsetTicks, 300);
		QVERIFY(widestStep(stale, 330) <= slope);  // and after a further resize

		ClipEdits good = stale;
		good.fadeInOffsetTicks = 300;
		QVERIFY(widestStep(good, 400) <= slope);
		QCOMPARE(withValidOffsetFades(good, 400), good);
		QCOMPARE(withValidOffsetFades(ClipEdits{}, 400), ClipEdits{});
	}

	//! The render path goes by the same rule: a clip with a lone offset or a drifted pair plays without a step.
	void theRenderHasNoStep()
	{
		for (const bool drifted : {false, true})
		{
			SampleClip* clip = sampleClipAt(0, 112);
			ClipEdits edits;
			edits.fadeOutTicks = 14;
			edits.fadeOutOffsetTicks = 28;
			edits.fadeOutShape = FadeShape::EqualPower;
			if (drifted)
			{
				edits.fadeInTicks = 14;
				edits.fadeInOffsetTicks = 70;  // the fade-out ends at tick 84
				edits.fadeInShape = FadeShape::EqualPower;
			}
			clip->setClipEdits(edits);
			SamplePlayHandle handle(clip);
			std::vector<SampleFrame> out;
			std::vector<SampleFrame> period(1024, SampleFrame(0.0f, 0.0f));
			for (int i = 0; i < 48; ++i)
			{
				std::fill(period.begin(), period.end(), SampleFrame(0.0f, 0.0f));
				handle.play(std::span<SampleFrame>(period));
				out.insert(out.end(), period.begin(), period.end());
			}
			double widest = 0.0;
			for (std::size_t f = 1; f < out.size(); ++f) { widest = std::max<double>(widest, std::fabs(out[f][0] - out[f - 1][0])); }
			QVERIFY2(widest < 0.01, qPrintable(QStringLiteral("a step of %1 (drifted %2)").arg(widest).arg(drifted)));
		}
	}

	//! Clamp mode accepts a one-tick edge fade; only explicit container mode needs both authored inner fades.
	void aTinyContainedClipClampsByDefaultAndRefusesContainerMode()
	{
		SampleClip* container = sampleClipAt(0, 400);
		SampleClip* tiny = sampleClipAt(100, 1);
		QString why;
		QCOMPARE(arrangement::applyOverlap(tiny, arrangement::Overlap::Crossfade, &why), 1);
		QVERIFY(why.isEmpty());
		QCOMPARE(container->clipEdits().fadeOutTicks, 1);
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, 0);
		QCOMPARE(tiny->clipEdits().fadeInTicks, 1);
		QCOMPARE(tiny->clipEdits().fadeInOffsetTicks, 0);

		const ControlResult refused = ControlRegistry::instance()->invoke(QStringLiteral("clip.crossfade"),
			QJsonObject{{QStringLiteral("out"), control::clipIdOf(container)}, {QStringLiteral("in"), control::clipIdOf(tiny)},
				{QStringLiteral("mode"), QStringLiteral("container")}});
		QVERIFY(!refused.ok);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("authored inner fade")), qPrintable(refused.errorMessage));

		const ControlResult modes = ControlRegistry::instance()->invoke(QStringLiteral("arrangement.modes"),
			QJsonObject{{QStringLiteral("overlap"), QStringLiteral("crossfade")}});
		QVERIFY(modes.ok);
		const ControlResult moved = ControlRegistry::instance()->invoke(QStringLiteral("clip.move"),
			QJsonObject{{QStringLiteral("clip"), control::clipIdOf(tiny)}, {QStringLiteral("position"), 120}});
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.modes"), QJsonObject{{QStringLiteral("overlap"), QStringLiteral("trim")}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QVERIFY2(moved.result.value(QStringLiteral("overlap_skipped")).toString().isEmpty(),
			qPrintable(QString::fromUtf8(QJsonDocument(moved.result).toJson())));
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, 0);
		QCOMPARE(tiny->clipEdits().fadeInOffsetTicks, 0);
		// Edge clamp also accepts a two-tick overlap; the old both-ends restriction belonged only to container mode.
		SampleClip* two = sampleClipAt(200, 2);
		QCOMPARE(arrangement::applyOverlap(two, arrangement::Overlap::Crossfade, &why), 1);
	}

private:
	SampleClip* sampleClipAt(int position, int length)
	{
		if (m_track == nullptr || Engine::getSong()->tracks().empty()) { m_track = new SampleTrack(Engine::getSong()); }
		auto* clip = dynamic_cast<SampleClip*>(m_track->createClip(TimePos(position)));
		clip->setSampleBuffer(constantBuffer(0.5f));
		clip->changeLength(TimePos(length));
		clip->setAutoResize(false);
		return clip;
	}

	SampleTrack* m_track = nullptr;
};

QTEST_MAIN(ClipOffsetFadePairTest)
#include "ClipOffsetFadePairTest.moc"
