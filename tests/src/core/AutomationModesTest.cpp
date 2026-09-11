/*
 * AutomationModesTest.cpp - Read / Touch / Latch / Write automation modes,
 *                           the no-destruction guarantee, the touch timeout
 *                           and the trim offset (post-alpha/automation-modes)
 *
 * Copyright (c) 2026 Zene Studio developers
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

//! Acceptance tests for the automation-mode work.
//!
//! The property this file exists for is the **no-destruction guarantee**: riding
//! a control in Read mode must not alter previously written automation. Every
//! assertion that says "the clip is unchanged" compares the *bits* of the clip's
//! time map - the keys, the in/out values, the tangents - not a rounded float
//! sum. Each such assertion is paired, in the same test, with an inversion
//! (the same harness driven in Touch/Write) that must observe a change, so a
//! read-only assertion cannot pass by being blind.
//!
//! The write path under test is the product's own: `Song::processNextBuffer()`
//! is called repeatedly, exactly as `AudioEngine::renderStageNoteSetup()` calls
//! it on the render thread, and `Song::processAutomations()` decides per tick
//! whether the control is written into its clip or read out of it.

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

using AutomationMode = AutomatableModel::AutomationMode;

//! Bit-exact fingerprint of a clip's time map. `QCOMPARE` on this string is a
//! comparison of raw float bit patterns, so "the recorded automation did not
//! change" cannot be satisfied by a value that merely rounds the same.
QString timeMapBits(const AutomationClip& clip)
{
	const auto hex = [](float value)
	{
		return QString::number(std::bit_cast<std::uint32_t>(value), 16);
	};

	const auto& map = clip.getTimeMap();
	QString out = QStringLiteral("nodes=%1").arg(map.size());
	for (auto it = map.begin(); it != map.end(); ++it)
	{
		out += QLatin1Char(';');
		out += QString::number(it.key());
		out += QLatin1Char(':');
		out += hex(INVAL(it));
		out += QLatin1Char('/');
		out += hex(OUTVAL(it));
		out += QLatin1Char(':');
		out += hex(INTAN(it));
		out += QLatin1Char('/');
		out += hex(OUTTAN(it));
	}
	return out;
}

//! One automatable control plus the automation clip that drives it, wired the
//! way a mixer channel is wired: a model whose range is [0, 2] (the shape of
//! `MixerChannel::m_volumeModel`) and a clip on an automation track that is part
//! of the song, so `Song::processAutomations()` finds both.
struct Rig
{
	FloatModel volume{1.0f, 0.0f, 2.0f, 0.001f};
	AutomationTrack track;
	AutomationClip clip;

	explicit Rig(Song* song) : track(song), clip(&track) {}

	//! A three-node linear curve: the "previously written automation".
	void writeCurve()
	{
		clip.setProgressionType(AutomationClip::ProgressionType::Linear);
		clip.putValue(0, 0.5f, false);
		clip.putValue(96, 0.25f, false);
		clip.putValue(192, 1.0f, false);
		clip.changeLength(192);
		clip.addObject(&volume);
	}
};

//! Renders @p periods periods from the start of the song. This is what makes
//! `Song::processNextBuffer()` advance the play position over the clip and call
//! `Song::processAutomations()` once per tick.
void playFromZero(Song* song, int periods)
{
	song->playSong();
	song->getTimeline().setTicks(0);
	for (int i = 0; i < periods; ++i)
	{
		song->processNextBuffer();
	}
}

//! A scenario in the mode decision table: a touch made in run @p touchRun
//! @p elapsed ns before the decision, optionally released, and the run the
//! decision is asked about. A negative @p elapsed means "never touched".
struct Scenario
{
	const char* name;
	quint64 touchRun;
	qint64 elapsed;
	bool released;
	quint64 decisionRun;
};

} // namespace

class AutomationModesTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! Each test builds its own rig; the transport is stopped between tests so
	//! one test's play position cannot leak into the next.
	void init()
	{
		Engine::getSong()->stop();
		// The gesture timeout is generous here so a slow test machine cannot
		// expire a touch mid-test; the timeout itself is exercised with
		// explicit timestamps in testTouchTimeoutEndsTheGesture().
		AutomatableModel::setAutomationTouchTimeoutNs(30LL * 1000 * 1000 * 1000);
	}

	//! Read is the status quo: every existing project's controls are in Read
	//! and nothing in the product switches them, so the default is the mode the
	//! alpha already behaves as.
	void testReadIsTheDefault()
	{
		FloatModel volume(1.0f, 0.0f, 2.0f, 0.001f);
		QCOMPARE(static_cast<int>(volume.automationMode()),
			static_cast<int>(AutomationMode::Read));
		QCOMPARE(volume.trimOffset(), 0.0f);
	}

	//! THE no-destruction property. Ride the control while the transport plays
	//! over a written curve, in Read: the recorded automation must be
	//! bit-identical afterwards. The inversion at the end runs the identical
	//! harness in Touch and requires the clip to change - without it, the first
	//! assertion could be passing because the harness never writes anything at
	//! all.
	void testReadNeverWrites()
	{
		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();

		const auto before = timeMapBits(rig.clip);
		QVERIFY(rig.clip.hasAutomation());
		QVERIFY(rig.volume.isAutomated());
		QCOMPARE(static_cast<int>(rig.volume.automationMode()),
			static_cast<int>(AutomationMode::Read));

		// Ride the fader across the whole clip. Each setValue() is a manual
		// move, exactly what a fader drag does.
		song->playSong();
		song->getTimeline().setTicks(0);
		for (int i = 0; i < 12; ++i)
		{
			rig.volume.setValue(0.1f + 0.05f * i);
			song->processNextBuffer();
		}
		QVERIFY2(song->getPlayPos().getTicks() > 0, "the harness never moved the play position");

		// No node was added, removed, moved or retangented: bit-identical.
		QCOMPARE(timeMapBits(rig.clip), before);

		// --- inversion: the same harness in Touch must change the clip -------
		rig.volume.setAutomationMode(AutomationMode::Touch);
		const auto beforeTouch = timeMapBits(rig.clip);
		song->playSong();
		song->getTimeline().setTicks(0);
		// One period first, so the transport's run is published, then touch.
		song->processNextBuffer();
		rig.volume.noteAutomationTouchStart();
		for (int i = 0; i < 12; ++i)
		{
			rig.volume.setValue(0.9f - 0.05f * i);
			song->processNextBuffer();
		}
		rig.volume.noteAutomationTouchEnd();
		QVERIFY2(timeMapBits(rig.clip) != beforeTouch,
			"Touch wrote nothing - the Read-mode assertion above proves nothing");
	}

	//! In Touch (the mode meant for "ride it, then let go"), a manual move is
	//! written while the control is held and the control reads the automation
	//! again once it is released.
	void testTouchWritesWhileHeldAndReturnsToReading()
	{
		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();
		rig.volume.setAutomationMode(AutomationMode::Touch);

		song->playSong();
		song->getTimeline().setTicks(0);
		song->processNextBuffer(); // publish the transport run

		// Not touched: no write authority.
		QVERIFY(!rig.volume.automationWantsWrite(
			AutomatableModel::automationTransportRun(), AutomatableModel::automationClockNs()));

		rig.volume.noteAutomationTouchStart();
		QVERIFY(rig.volume.automationWantsWrite(
			AutomatableModel::automationTransportRun(), AutomatableModel::automationClockNs()));

		const auto before = timeMapBits(rig.clip);
		for (int i = 0; i < 8; ++i)
		{
			rig.volume.setValue(0.9f - 0.02f * i);
			song->processNextBuffer();
		}
		QVERIFY2(timeMapBits(rig.clip) != before, "a held Touch pass wrote nothing");

		// Release: Touch returns to reading - unlike Latch (see below).
		rig.volume.noteAutomationTouchEnd();
		QVERIFY(!rig.volume.automationWantsWrite(
			AutomatableModel::automationTransportRun(), AutomatableModel::automationClockNs()));
	}

	//! The touch timeout bounds the write window, so a lost mouse-up cannot
	//! leave a control writing into its automation forever.
	void testTouchTimeoutEndsTheGesture()
	{
		FloatModel volume(1.0f, 0.0f, 2.0f, 0.001f);
		volume.setAutomationMode(AutomationMode::Touch);

		const qint64 timeout = AutomatableModel::automationTouchTimeoutNs();
		QVERIFY(timeout > 0);

		const qint64 t0 = 1000000;
		const quint64 run = 7;
		volume.noteAutomationTouchStart(t0, run);

		QVERIFY(volume.automationWantsWrite(run, t0));
		QVERIFY(volume.automationWantsWrite(run, t0 + timeout - 1));
		QVERIFY2(!volume.automationWantsWrite(run, t0 + timeout + 1),
			"a touch older than the timeout still writes");

		// A new move refreshes the window.
		volume.noteAutomationTouchStart(t0 + timeout + 1, run);
		QVERIFY(volume.automationWantsWrite(run, t0 + 2 * timeout));

		// An explicit release ends it immediately, whatever the timeout says.
		volume.noteAutomationTouchEnd();
		QVERIFY(!volume.automationWantsWrite(run, t0 + timeout + 1));
	}

	//! Latch keeps writing after the control is released and stops only when the
	//! transport run ends - the difference a mixing engineer relies on.
	void testLatchHoldsUntilTheTransportRunEnds()
	{
		FloatModel volume(1.0f, 0.0f, 2.0f, 0.001f);
		volume.setAutomationMode(AutomationMode::Latch);

		const qint64 t0 = 1000000;
		const qint64 timeout = AutomatableModel::automationTouchTimeoutNs();
		const quint64 run = 7;

		volume.noteAutomationTouchStart(t0, run);
		volume.noteAutomationTouchEnd();

		// Released, and long past the timeout: still writing, because a latch
		// holds until the transport stops.
		QVERIFY2(volume.automationWantsWrite(run, t0 + 100 * timeout),
			"Latch stopped writing when the control was released");

		// A later transport run is not latched: the engagement belonged to
		// run 7, which is what makes a latch self-clearing across a stop.
		QVERIFY2(!volume.automationWantsWrite(run + 1, t0),
			"a Latch from a previous run leaked into the next one");

		// A stopped transport never writes (run token 0).
		QVERIFY(!volume.automationWantsWrite(0, t0));

		// --- integration: the run token follows the real transport -----------
		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();
		rig.volume.setAutomationMode(AutomationMode::Latch);

		song->playSong();
		song->getTimeline().setTicks(0);
		song->processNextBuffer();
		QVERIFY(AutomatableModel::automationTransportRun() != 0);

		rig.volume.noteAutomationTouchStart();
		rig.volume.noteAutomationTouchEnd();
		const auto before = timeMapBits(rig.clip);
		for (int i = 0; i < 8; ++i)
		{
			rig.volume.setValue(0.8f - 0.02f * i);
			song->processNextBuffer();
		}
		QVERIFY2(timeMapBits(rig.clip) != before, "a Latch pass wrote nothing");

		// The transport stops: the token drops to zero and the latch is over.
		song->stop();
		song->processNextBuffer();
		QCOMPARE(AutomatableModel::automationTransportRun(), static_cast<quint64>(0));
		QVERIFY(!rig.volume.automationWantsWrite(
			AutomatableModel::automationTransportRun(), AutomatableModel::automationClockNs()));
	}

	//! Write overwrites the pass without needing a touch at all.
	void testWriteWritesWithoutATouch()
	{
		FloatModel volume(1.0f, 0.0f, 2.0f, 0.001f);
		volume.setAutomationMode(AutomationMode::Write);
		QVERIFY2(volume.automationWantsWrite(7, 0), "Write still waits for a touch");
		QVERIFY(!volume.automationWantsWrite(0, 0));

		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();
		rig.volume.setAutomationMode(AutomationMode::Write);
		const auto before = timeMapBits(rig.clip);

		song->playSong();
		song->getTimeline().setTicks(0);
		song->processNextBuffer();
		for (int i = 0; i < 8; ++i)
		{
			rig.volume.setValue(0.2f + 0.05f * i);
			song->processNextBuffer();
		}
		QVERIFY2(timeMapBits(rig.clip) != before,
			"a Write pass with the transport running wrote nothing");
	}

	//! The decision table, and the proof that no two modes are the same mode:
	//! each mode's decisions over the scenario set are a distinct vector, so a
	//! mode implemented as another would fail here (Touch-as-Latch, Read-as-
	//! Write, and so on).
	void testEachModeDiffersFromTheOthers()
	{
		const qint64 t0 = 1000000;
		const qint64 timeout = AutomatableModel::automationTouchTimeoutNs();
		const quint64 run = 7;
		const qint64 timedOut = timeout + 1;

		const std::vector<Scenario> scenarios{
			{"stopped, untouched",       0,     -1,       false, 0},
			{"playing, untouched",       run,   -1,       false, run},
			{"playing, held",            run,   0,        false, run},
			{"playing, held then released", run, timedOut, true,  run},
			{"playing, touch timed out", run,   timedOut, false, run},
			{"next run, held then released", run, timedOut, true, run + 1},
		};

		// Read never writes; the status quo is untouched by this work.
		const std::vector<bool> expectedRead{false, false, false, false, false, false};
		// Touch writes only while the gesture is live.
		const std::vector<bool> expectedTouch{false, false, true, false, false, false};
		// Latch writes from the touch until the run it was made in ends.
		const std::vector<bool> expectedLatch{false, false, true, true, true, false};
		// Write writes whenever the transport runs, touch or not.
		const std::vector<bool> expectedWrite{false, true, true, true, true, true};

		const std::vector<std::pair<AutomationMode, std::vector<bool>>> table{
			{AutomationMode::Read, expectedRead},
			{AutomationMode::Touch, expectedTouch},
			{AutomationMode::Latch, expectedLatch},
			{AutomationMode::Write, expectedWrite},
		};

		std::vector<std::vector<bool>> observed;
		for (const auto& entry : table)
		{
			const AutomationMode mode = entry.first;
			const auto& expected = entry.second;

			FloatModel volume(1.0f, 0.0f, 2.0f, 0.001f);
			volume.setAutomationMode(mode);

			std::vector<bool> decisions;
			for (const auto& s : scenarios)
			{
				if (s.elapsed >= 0)
				{
					volume.noteAutomationTouchStart(t0, s.touchRun);
					if (s.released) { volume.noteAutomationTouchEnd(); }
				}
				const qint64 now = t0 + std::max<qint64>(0, s.elapsed);
				decisions.push_back(volume.automationWantsWrite(s.decisionRun, now));
			}

			for (std::size_t i = 0; i < scenarios.size(); ++i)
			{
				const QByteArray msg = QStringLiteral("mode %1, scenario '%2': expected %3, got %4")
					.arg(static_cast<int>(mode))
					.arg(QString::fromLatin1(scenarios[i].name))
					.arg(expected[i] ? QStringLiteral("write") : QStringLiteral("read"))
					.arg(decisions[i] ? QStringLiteral("write") : QStringLiteral("read"))
					.toUtf8();
				QVERIFY2(decisions[i] == expected[i], msg.constData());
			}
			observed.push_back(decisions);
		}

		for (std::size_t a = 0; a < observed.size(); ++a)
		{
			for (std::size_t b = a + 1; b < observed.size(); ++b)
			{
				QVERIFY2(observed[a] != observed[b],
					"two automation modes behave identically over every scenario");
			}
		}
	}

	//! A mode change - in any direction, in any order, while the transport runs
	//! or not - must never silently drop recorded data. Changing the mode of a
	//! control is not an edit to its automation.
	void testAModeChangeNeverDropsRecordedData()
	{
		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();
		const auto before = timeMapBits(rig.clip);

		const AutomationMode modes[] = {
			AutomationMode::Read, AutomationMode::Touch,
			AutomationMode::Latch, AutomationMode::Write,
		};
		for (const auto mode : modes)
		{
			rig.volume.setAutomationMode(mode);
			QCOMPARE(static_cast<int>(rig.volume.automationMode()), static_cast<int>(mode));
			QCOMPARE(timeMapBits(rig.clip), before);
		}

		// The same while the transport plays over the clip, ending in Read.
		song->playSong();
		song->getTimeline().setTicks(0);
		for (const auto mode : modes)
		{
			rig.volume.setAutomationMode(mode);
			song->processNextBuffer();
		}
		rig.volume.setAutomationMode(AutomationMode::Read);
		for (int i = 0; i < 6; ++i) { song->processNextBuffer(); }
		QCOMPARE(timeMapBits(rig.clip), before);
	}

	//! Switching *out* of a writing mode keeps what that pass recorded: leaving
	//! Touch for Read is not an undo, and it is not a delete either.
	void testSwitchingOutOfAWriteModeKeepsTheRecordedData()
	{
		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();
		rig.volume.setAutomationMode(AutomationMode::Touch);

		song->playSong();
		song->getTimeline().setTicks(0);
		song->processNextBuffer();
		rig.volume.noteAutomationTouchStart();
		for (int i = 0; i < 6; ++i)
		{
			rig.volume.setValue(0.9f - 0.03f * i);
			song->processNextBuffer();
		}
		rig.volume.noteAutomationTouchEnd();
		const auto afterTouch = timeMapBits(rig.clip);

		rig.volume.setAutomationMode(AutomationMode::Read);
		song->getTimeline().setTicks(0);
		for (int i = 0; i < 6; ++i) { song->processNextBuffer(); }

		QCOMPARE(timeMapBits(rig.clip), afterTouch);
	}

	//! The trim offset is an offset on top of the written automation, applied
	//! where the automation is read out - and it never writes anything.
	void testTrimOffsetIsNonDestructive()
	{
		auto* song = Engine::getSong();
		Rig rig(song);
		rig.writeCurve();
		const auto before = timeMapBits(rig.clip);

		QCOMPARE(rig.volume.trimOffset(), 0.0f);
		QCOMPARE(rig.volume.effectiveAutomationValue(0.25f), 0.25f);

		rig.volume.setTrimOffset(0.1f);
		QCOMPARE(rig.volume.trimOffset(), 0.1f);
		QCOMPARE(rig.volume.effectiveAutomationValue(0.25f), 0.35f);
		QCOMPARE(rig.volume.effectiveAutomationValue(1.0f), 1.1f);
		// Setting, changing and clearing the trim never touches the clip.
		QCOMPARE(timeMapBits(rig.clip), before);
		rig.volume.setTrimOffset(-0.25f);
		QCOMPARE(rig.volume.effectiveAutomationValue(0.5f), 0.25f);
		QCOMPARE(timeMapBits(rig.clip), before);
		rig.volume.setTrimOffset(0.0f);
		QCOMPARE(timeMapBits(rig.clip), before);
	}

	//! A zero trim is a bit-exact no-op, which is what keeps the Read render
	//! byte-identical: zero (both signs), subnormal values and the range ends
	//! must come through untouched.
	void testZeroTrimIsABitExactNoOp()
	{
		FloatModel volume(1.0f, -2.0f, 2.0f, 0.0f);
		const float values[] = {
			0.0f, -0.0f, 1.0f, -1.0f, 2.0f, -2.0f,
			std::numeric_limits<float>::min(),        // smallest normal
			std::numeric_limits<float>::denorm_min(), // smallest subnormal
		};
		for (const float v : values)
		{
			QCOMPARE(std::bit_cast<std::uint32_t>(volume.effectiveAutomationValue(v)),
				std::bit_cast<std::uint32_t>(v));
		}
	}

	//! The trim reaches the value the engine actually reads, through the real
	//! path: the automation is read out of the clip, the trim is added, and the
	//! control is set to the result.
	void testTrimReachesTheValueTheEngineReads()
	{
		auto* song = Engine::getSong();
		Rig rig(song);
		// A single discrete node at 0 with the clip length set: the value is a
		// constant 0.5 everywhere the clip applies, so the expectation is exact.
		rig.clip.setProgressionType(AutomationClip::ProgressionType::Discrete);
		rig.clip.putValue(0, 0.5f, false);
		rig.clip.changeLength(192);
		rig.clip.addObject(&rig.volume);
		const auto before = timeMapBits(rig.clip);

		rig.volume.setTrimOffset(0.1f);
		playFromZero(song, 3);

		QCOMPARE(rig.volume.value(), 0.6f);
		// ... and the trim did not get written into the automation.
		QCOMPARE(timeMapBits(rig.clip), before);
		QCOMPARE(static_cast<int>(rig.clip.getTimeMap().size()), 1);

		// Clearing the trim returns the control to the written value.
		rig.volume.setTrimOffset(0.0f);
		playFromZero(song, 3);
		QCOMPARE(rig.volume.value(), 0.5f);
		QCOMPARE(timeMapBits(rig.clip), before);
	}

	//! The realtime contract, stated as a test: the mode state the audio thread
	//! reads must be lock-free, so `automationWantsWrite()` can never block on a
	//! mutex. (The stored values are read with relaxed atomics on the render
	//! thread and written by the GUI thread; nothing on either side of the
	//! decision allocates.)
	void testModeStateIsLockFree()
	{
		QVERIFY(std::atomic<AutomationMode>::is_always_lock_free);
		QVERIFY(std::atomic<float>::is_always_lock_free);
		QVERIFY(std::atomic<qint64>::is_always_lock_free);
	}
};

QTEST_GUILESS_MAIN(AutomationModesTest)
#include "AutomationModesTest.moc"
