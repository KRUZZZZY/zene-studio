/*
 * LinkPhaseLockTest.cpp - the Ableton Link follower's beat-grid arithmetic
 *                         (include/LinkPhaseLock.h), case by case with numbers
 *                         worked by hand, and the audio-thread follower
 *                         (include/LinkAudioFollower.h) held to the realtime rule.
 *
 * The arithmetic needs no network and no engine, so every rule the header states
 * is pinned here with exact inputs: the count-in, the join of a running session,
 * the bounded nudge, the re-lock of a seek, and sync switched on mid-song.
 *
 * The realtime half: 64 periods of followLinkSession() - with a tempo committed
 * through the mailbox, a start/stop edge, a count-in, a lock and a re-lock jump
 * among them - allocate nothing on the calling thread. The count goes through
 * the shared AllocationProbe.h (via LinkRealtimeProbe.h's allocationsDuring()),
 * whose operator new replaces the WHOLE program's since BUGS_FOUND 13.9's fix.
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

#include <QtTest>

#include "AbletonLinkSession.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "LinkAudioFollower.h"
#include "LinkPhaseLock.h"
#include "Song.h"
// The shared allocation probe (its operator new replaces the whole program's; naming it here is
// what links it - tests/CMakeLists.txt) and the Link tests' allocationsDuring() helper over it.
#include "AllocationProbe.h"
#include "LinkRealtimeProbe.h"

using namespace lmms;
using link::PhaseAction;
using link::PhaseLock;
using link::PhaseLockInput;

namespace
{

PhaseLockInput at(double linkBeat, double zeneBeat, bool started = false)
{
	PhaseLockInput in;
	in.linkBeat = linkBeat;
	in.zeneBeat = zeneBeat;
	in.quantum = 4.0;
	in.periodBeats = 0.0116;   // 256 frames at 44.1 kHz and 120 BPM
	in.started = started;
	return in;
}

} // namespace

class LinkPhaseLockTest : public QObject
{
	Q_OBJECT
private slots:
	void phaseHelpers()
	{
		QVERIFY(std::fabs(PhaseLock::signedDistance(3.9, 0.1, 4.0) + 0.2) < 1e-12);
		QVERIFY(std::fabs(PhaseLock::signedDistance(0.1, 3.9, 4.0) - 0.2) < 1e-12);
		QCOMPARE(PhaseLock::nextPhaseMatch(5.0, 1.0, 4.0), 5.0);
		QCOMPARE(PhaseLock::nextPhaseMatch(5.5, 1.0, 4.0), 9.0);
		QCOMPARE(PhaseLock::nextPhaseMatch(-1.0, 2.0, 4.0), 2.0);
	}

	//! Started on its own at bar 1 while the session is 1.5 beats into its bar:
	//! hold until the session reaches its next bar line, then play from there.
	void aLocalStartCountsInToTheSessionsPhase()
	{
		PhaseLock lock;
		auto out = lock.step(at(1.5, 0.0, true));
		QCOMPARE(out.action, PhaseAction::Hold);
		QCOMPARE(lock.offset(), -4.0);
		QCOMPARE(out.targetBeat, -2.5);
		out = lock.step(at(3.99, 0.0));
		QCOMPARE(out.action, PhaseAction::Hold);
		out = lock.step(at(4.0, 0.0));
		QCOMPARE(out.action, PhaseAction::Nudge);
		QVERIFY(lock.locked());
		QCOMPARE(out.moveBeats, 0.0);
	}

	//! Started because a session that began at its beat 8 is playing (start/stop
	//! sync), half a beat late: catch up in one jump, phase-aligned.
	void joiningARunningSessionCatchesUp()
	{
		PhaseLock lock;
		PhaseLockInput in = at(8.5, 0.0, true);
		in.joinRunning = true;
		in.sessionStartBeat = 8.0;
		const auto out = lock.step(in);
		QCOMPARE(out.action, PhaseAction::Jump);
		QCOMPARE(out.targetBeat, 0.5);
		QCOMPARE(out.moveBeats, 0.5);
	}

	//! Locked: a small error is corrected by at most 5 % of one period.
	void aLockedHeadIsNudgedWithinTheBound()
	{
		PhaseLock lock;
		lock.step(at(0.0, 0.0, true));
		QVERIFY(lock.locked());
		const auto ahead = lock.step(at(1.01, 1.0));
		QCOMPARE(ahead.action, PhaseAction::Nudge);
		QVERIFY(std::fabs(ahead.errorBeats - 0.01) < 1e-12);
		QVERIFY(std::fabs(ahead.moveBeats - PhaseLock::MaxNudgeFraction * 0.0116) < 1e-12);
		const auto behind = lock.step(at(2.0, 2.0001));
		QVERIFY(std::fabs(behind.moveBeats + 0.0001) < 1e-12);
	}

	//! A seek to beat 16.3 while the session reads beat 3: re-lock to the
	//! phase-aligned position nearest the seek (15.0), not back to where it was.
	void aSeekReLocksToTheNearestAlignedPosition()
	{
		PhaseLock lock;
		lock.step(at(0.0, 0.0, true));
		const auto out = lock.step(at(3.0, 16.3));
		QCOMPARE(out.action, PhaseAction::Jump);
		QCOMPARE(out.targetBeat, 15.0);
		QCOMPARE(lock.offset(), 12.0);
	}

	//! Sync switched on while the song plays at beat 5 and the session reads
	//! 2.25: no count-in - re-lock in place to 6.25 (phase 2.25, 1.25 away).
	void switchedOnWhilePlayingReLocksInPlace()
	{
		PhaseLock lock;
		const auto out = lock.step(at(2.25, 5.0, false));
		QCOMPARE(out.action, PhaseAction::Jump);
		QCOMPARE(out.targetBeat, 6.25);
	}

	//! The audio-thread follower, through the function Song::processNextBuffer()
	//! calls, allocates nothing over 64 periods that exercise every branch.
	void theFollowerPeriodDoesNotAllocate()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		Song* song = Engine::getSong();
		AbletonLinkSession session(120.0);
		LinkAudioFollower& follower = LinkAudioFollower::instance();
		follower.attach(&session);
		follower.setQuantum(4.0);
		follower.setStartStopSync(true);
		follower.setEngaged(true);
		followLinkSession(*song, false);   // the first engaged period
		song->playSong();
		follower.noteTransportEdge();

		LinkFollowerReading before = follower.reading();
		// Only the follower's own call is counted: the UI-side request and the
		// seek below stand in for other threads and are made outside the count.
		std::uint64_t allocations = 0;
		for (int period = 0; period < 64; ++period)
		{
			if (period == 10) { follower.requestTempo(133.0); }
			if (period == 30) { song->getTimeline().setTicks(48 * 64, false); }
			allocations += lmms::test::link::allocationsDuring([&] { followLinkSession(*song, true); });
		}
		const LinkFollowerReading after = follower.reading();
		qInfo("allocations=%llu over 64 periods; periods %llu, holds %llu, jumps %llu",
			static_cast<unsigned long long>(allocations),
			static_cast<unsigned long long>(after.periods - before.periods),
			static_cast<unsigned long long>(after.holds - before.holds),
			static_cast<unsigned long long>(after.jumps - before.jumps));

		follower.setEngaged(false);
		followLinkSession(*song, false);
		follower.attach(nullptr);
		song->stop();
		Engine::destroy();
		QCOMPARE(after.periods - before.periods, std::uint64_t{64});
		QVERIFY2(after.jumps > before.jumps, "no re-lock jump ran - the claim would not cover it");
		QCOMPARE(allocations, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(LinkPhaseLockTest)
#include "LinkPhaseLockTest.moc"
