/*
 * TakeLaneCompPlaybackTest.cpp - R3.1: the composite PLAYS, and a lane can be auditioned
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

/*! Take lanes were comped into a composite that NOTHING played (include/TakeLane.h said so:
 *  "no playback path consumes the composite yet"). Now each take's play handle snapshots the
 *  spans the composite selects for its lane and sounds only there. Two takes of one passage -
 *  a constant 0.5 on lane 0, 0.25 on lane 1 - sit on top of each other; the composite takes
 *  lane 0 for the first half and lane 1 for the second, and each take is rendered through the
 *  real handle:
 *    * lane 0 sounds in the first half and is SILENT in the second, lane 1 the reverse, and
 *      the two sum to one continuous performance;
 *    * each switch is a short ramp, not a step (no click);
 *    * auditioning lane 1 plays it WHOLE and silences lane 0, and stopping the audition
 *      plays the comp again;
 *    * a track with no composite renders exactly as it did (no gate at all).
 */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

SampleClip* makeTake(SampleTrack& track, int rate, float level, int lane)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(2 * rate), SampleFrame(level, level));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->setLaneIndex(lane);
	return clip;
}

std::vector<SampleFrame> render(SampleClip* clip)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	const auto total = static_cast<int>(handle.totalFrames());
	std::vector<SampleFrame> out;
	std::vector<SampleFrame> buffer(512);
	for (int guard = 0; static_cast<int>(handle.framesDone()) < total && guard < total / 512 + 16; ++guard)
	{
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(buffer));
		out.insert(out.end(), buffer.begin(), buffer.end());
	}
	out.resize(static_cast<std::size_t>(total));
	return out;
}

double meanLevel(const std::vector<SampleFrame>& data, std::size_t from, std::size_t to)
{
	double sum = 0.0;
	for (std::size_t i = from; i < to; ++i) { sum += std::fabs(data[i][0]); }
	return to > from ? sum / static_cast<double>(to - from) : 0.0;
}

} // namespace

class TakeLaneCompPlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		m_rate = Engine::audioEngine()->outputSampleRate();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void theCompositePlaysEachLaneOnlyWhereItIsSelected()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* first = makeTake(track, m_rate, 0.5f, 0);
		SampleClip* second = makeTake(track, m_rate, 0.25f, 1);
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(QStringLiteral("take 1"));
		comp.addLane(QStringLiteral("take 2"));
		const int half = first->length().getTicks() / 2;
		QVERIFY(comp.selectSegment(0, half, 0));
		QVERIFY(comp.selectSegment(half, first->length().getTicks(), 1));

		const std::vector<SampleFrame> a = render(first);
		const std::vector<SampleFrame> b = render(second);
		QCOMPARE(a.size(), b.size());
		const std::size_t mid = static_cast<std::size_t>(std::llround(half * Engine::framesPerTick(m_rate)));
		const std::size_t guard = 256; // clear of the ramps
		QVERIFY(mid > guard && mid + guard < a.size());
		std::printf("COMP_EVIDENCE lane0 first/second half %.4f / %.4f, lane1 %.4f / %.4f\n",
			meanLevel(a, guard, mid - guard), meanLevel(a, mid + guard, a.size() - guard),
			meanLevel(b, guard, mid - guard), meanLevel(b, mid + guard, a.size() - guard));
		QVERIFY(std::fabs(meanLevel(a, guard, mid - guard) - 0.5) < 1e-6);
		QCOMPARE(meanLevel(a, mid + guard, a.size() - guard), 0.0);
		QCOMPARE(meanLevel(b, guard, mid - guard), 0.0);
		QVERIFY(std::fabs(meanLevel(b, mid + guard, a.size() - guard) - 0.25) < 1e-6);

		// The switch is a ramp: no frame jumps by the full difference in one step.
		float largestStep = 0.0f;
		for (std::size_t i = mid - guard; i < mid + guard; ++i)
		{
			const float here = a[i][0] + b[i][0];
			const float next = a[i + 1][0] + b[i + 1][0];
			largestStep = std::max(largestStep, std::fabs(next - here));
		}
		QVERIFY2(largestStep < 0.05f, qPrintable(QStringLiteral("the lane switch steps by %1").arg(largestStep)));
	}

	void auditioningALanePlaysItWholeAndSilencesTheOthers()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* first = makeTake(track, m_rate, 0.5f, 0);
		SampleClip* second = makeTake(track, m_rate, 0.25f, 1);
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane();
		comp.addLane();
		QVERIFY(comp.selectSegment(0, first->length().getTicks(), 0));

		comp.setAuditionLane(1);
		const std::vector<SampleFrame> a = render(first);
		const std::vector<SampleFrame> b = render(second);
		QCOMPARE(meanLevel(a, 0, a.size()), 0.0);
		QVERIFY(meanLevel(b, 256, b.size() - 256) > 0.2499);

		comp.setAuditionLane(-1);
		QVERIFY2(meanLevel(render(first), 256, a.size() - 256) > 0.4999, "stopping the audition did not bring the comp back");
	}

	//! No composite, no gate: the same clip renders byte-for-byte alike on a track with no
	//! lanes and on one whose lanes exist but select nothing (and nothing is auditioned).
	void aTrackWithNoCompositeRendersUnchanged()
	{
		SampleTrack plainTrack(Engine::getSong());
		const std::vector<SampleFrame> plain = render(makeTake(plainTrack, m_rate, 0.5f, 0));
		SampleTrack lanedTrack(Engine::getSong());
		SampleClip* laned = makeTake(lanedTrack, m_rate, 0.5f, 0);
		lanedTrack.takeLanes().addLane();
		const std::vector<SampleFrame> withLanes = render(laned);
		QCOMPARE(withLanes.size(), plain.size());
		for (std::size_t i = 0; i < plain.size(); ++i)
		{
			QVERIFY(withLanes[i][0] == plain[i][0] && withLanes[i][1] == plain[i][1]);
		}
		QVERIFY(std::fabs(meanLevel(plain, 256, plain.size() / 2) - 0.5) < 1e-6);
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(TakeLaneCompPlaybackTest)
#include "TakeLaneCompPlaybackTest.moc"
