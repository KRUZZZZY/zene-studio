/*
 * WarpQuantizeTest.cpp - quantise audio: a clip's transients warped onto the grid
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

/*! ED-137 (0.5.0 L07): warp.quantize on a sample clip of four bursts each 10 ticks after a quarter
 *  note - the markers land on the quarters (strength 1), stay where the bursts are (strength 0), one
 *  undo takes them back, and silent audio is refused not_found. detectOnsetFrames finds the bursts. */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioBurstSupport.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ImportDetectionDsp.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int BurstTicks[] = {10, 58, 106, 154};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

std::vector<SampleFrame> clicks(bool bursts)
{
	return test::burstAudio(200, bursts ? std::initializer_list<int>{10, 58, 106, 154} : std::initializer_list<int>{});
}

} // namespace

class WarpQuantizeTest : public QObject
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
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void theDetectorFindsEveryBurst()
	{
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const std::vector<SampleFrame> data = clicks(true);
		std::vector<float> mono;
		for (const SampleFrame& frame : data) { mono.push_back(frame.left()); }
		const std::vector<std::int64_t> onsets = detection::detectOnsetFrames(mono.data(), mono.size(), rate);
		QCOMPARE(onsets.size(), std::size_t{4});
		for (std::size_t i = 0; i < onsets.size(); ++i)
		{
			const double expected = BurstTicks[i] * Engine::framesPerTick();
			QVERIFY2(std::abs(static_cast<double>(onsets[i]) - expected) < 2048.0, qPrintable(QStringLiteral("onset %1 at %2, burst at %3")
				.arg(i).arg(onsets[i]).arg(expected)));
		}
	}

	void theTransientsLandOnTheGrid()
	{
		SampleClip* clip = test::sampleClipOf(clicks(true));
		const QString id = control::clipIdOf(clip);
		const ControlResult done = run(QStringLiteral("warp.quantize"), {{QStringLiteral("clip"), id}, {QStringLiteral("grid"), 48}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("transients")).toInt(), 4);
		const WarpMarkers& warp = clip->warpMarkers();
		QCOMPARE(warp.size(), 4);
		for (int i = 0; i < warp.size(); ++i) { QCOMPARE(static_cast<int>(warp[i].offsetTicks), 48 * i); }
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(clip->warpMarkers().empty());
		// Strength 0: the markers stay where the bursts are heard (within the detector's window).
		QVERIFY(run(QStringLiteral("warp.quantize"), {{QStringLiteral("clip"), id}, {QStringLiteral("grid"), 48},
			{QStringLiteral("strength"), 0.0}}).ok);
		for (int i = 0; i < clip->warpMarkers().size(); ++i)
		{
			QVERIFY(std::abs(clip->warpMarkers()[i].offsetTicks - BurstTicks[i]) <= 6);
		}
		QCOMPARE(run(QStringLiteral("warp.quantize"), {{QStringLiteral("clip"), id}, {QStringLiteral("strength"), 2.0}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	/*! A transient may play BEFORE the clip's own origin - the engine's rule for
	 *  marker offsets is "strictly increasing", not "non-negative", and
	 *  offsetTicksProperty() therefore declares the bound signed. A clip whose
	 *  existing map pins an audio frame 1000 ticks before its origin puts every
	 *  transient at a negative relative tick, and strength 0 (no movement) must
	 *  still emit all four of them. A sentinel of -1 for "nothing emitted yet"
	 *  instead drops every one of them, so quantize reports not_found "no
	 *  transient found" for a clip whose audio plainly has four. */
	void negativeRelativeTicksAreStillEmitted()
	{
		SampleClip* clip = test::sampleClipOf(clicks(true));
		const QString id = control::clipIdOf(clip);
		QVERIFY(run(QStringLiteral("warp.add"), {{QStringLiteral("clip"), id},
			{QStringLiteral("source_frame"), 0}, {QStringLiteral("offset_ticks"), -1000}}).ok);

		const ControlResult done = run(QStringLiteral("warp.quantize"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("grid"), 48},
				{QStringLiteral("strength"), 0.0}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("transients")).toInt(), 4);
		QCOMPARE(clip->warpMarkers().size(), 4);
		// Still strictly increasing in both coordinates, still all negative.
		for (int i = 0; i < clip->warpMarkers().size(); ++i)
		{
			QVERIFY(clip->warpMarkers()[i].offsetTicks < 0);
			if (i > 0)
			{
				QVERIFY(clip->warpMarkers()[i - 1].offsetTicks
					< clip->warpMarkers()[i].offsetTicks);
			}
		}
	}

	void silenceIsRefused()
	{
		SampleClip* clip = test::sampleClipOf(clicks(false));
		QCOMPARE(run(QStringLiteral("warp.quantize"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).errorKind,
			ControlErrorKind::NotFound);
		QVERIFY(clip->warpMarkers().empty());
	}
};

QTEST_MAIN(WarpQuantizeTest)
#include "WarpQuantizeTest.moc"
