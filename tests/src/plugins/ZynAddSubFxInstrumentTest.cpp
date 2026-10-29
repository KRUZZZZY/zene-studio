/*
 * ZynAddSubFxInstrumentTest.cpp - the Zyn instrument's audio path and its plugin lock.
 *
 * WHAT THIS DRIVES
 *
 * The ZynAddSubFx instrument (plugins/ZynAddSubFx/ZynAddSubFx.cpp) keeps its
 * plugin instance - the in-process synth or the client process - behind a mutex
 * that also guards the instance's life: a hosting-mode switch deletes it and
 * creates another one. The audio path takes that mutex, and so do the main
 * thread's operations (a preset load, a reload, the hosting getters).
 *
 * The test compiles the plug-in's own ZynAddSubFx.cpp - the way the VST and CLAP
 * tests compile theirs - so it can drive the class directly, and it adds three
 * cases, one per defect:
 *
 *   - theExportPathDoesNotWaitForThePluginMutex(): the audio path must never WAIT for
 *     the mutex. The main thread holds it for as long as a reload takes (the client
 *     handshake is seconds), and the old export policy made the audio thread wait for
 *     it - indefinitely - so a render could stall for the whole reload. The case asks
 *     the instrument for the wait it allows itself, in the export path, and checks the
 *     attempt that follows: no wall-clock timeout, no thread, no race.
 *
 *   - thePluginChoiceAndItsUseAreUnderOneLock(): loadFile() chooses a plugin and
 *     then uses it. The choice and the use have to happen under one lock, or a
 *     concurrent hosting-mode switch can delete the instance between them.
 *
 *   - theHandoffKeepsTheEventOffset(): a MIDI event's frame offset must survive the
 *     hand-off. The queue used to drop it (offset 0), so every event was applied at
 *     the period start instead of at its scheduled position.
 *
 * HOW THE CASES ARE DETERMINISTIC
 *
 * No case waits for a wall-clock outcome, none of them sleeps and none of them races:
 * the lock state is read with QMutex::tryLock(), which never blocks, and the wait the
 * audio path allows itself is asked of the instrument instead of being measured. The
 * offsets are injected values (42, 100), never measured.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <QtTest>

#include <array>
#include <cmath>
#include <vector>

#include "AudioBufferView.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LocalZynAddSubFx.h"
#include "MidiEvent.h"
#include "Song.h"
#include "ZynAddSubFx.h"

using namespace lmms;

namespace
{

//! The instrument with the members a white-box regression test drives.
class TestInstrument : public ZynAddSubFxInstrument
{
public:
	using ZynAddSubFxInstrument::ZynAddSubFxInstrument;

	using ZynAddSubFxInstrument::audioLockWaitMs;
	using ZynAddSubFxInstrument::forwardQueuedMidi;
	using ZynAddSubFxInstrument::processImpl;
	using ZynAddSubFxInstrument::m_midiHandoff;
	using ZynAddSubFxInstrument::m_pluginMutex;
	using ZynAddSubFxInstrument::processLock;
	using ZynAddSubFxInstrument::renderIsExporting;

#ifdef LMMS_TESTING
	using ZynAddSubFxInstrument::m_testHook;
	using ZynAddSubFxInstrument::m_testLastForwardedOffset;
#endif
};

//! The instrument in the export path, which is the path that used to wait.
class ExportingTestInstrument final : public TestInstrument
{
public:
	using TestInstrument::TestInstrument;

	auto renderIsExporting() const -> bool override { return true; }
};

} // namespace

class ZynAddSubFxInstrumentTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		m_engineInitialised = true;
		QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		// Only what this test started: Engine::destroy() dereferences the project
		// journal, which only Engine::init() ever sets.
		if (m_engineInitialised) { Engine::destroy(); }
	}

	//! The export path must not wait for the plugin mutex.
	void theExportPathDoesNotWaitForThePluginMutex()
	{
		InstrumentTrack track{Engine::getSong()};
		ExportingTestInstrument instrument{&track};

		// The export path is the one that used to wait: the old policy took the mutex
		// with an infinite timeout while the song was exporting.
		QVERIFY(instrument.renderIsExporting());

		// The wait the audio path allows itself: none. This is the assertion that fails
		// against the old policy, which waited for as long as the main thread held the
		// mutex.
		QCOMPARE(instrument.audioLockWaitMs(), 0);

		// And the attempt itself: with the mutex held, it comes back at once and does
		// not take the lock - the period is skipped, not delayed.
		instrument.m_pluginMutex.lock();
		const bool locked = instrument.processLock();
		instrument.m_pluginMutex.unlock();
		QVERIFY2(!locked, "the audio path took the plugin mutex the main thread was holding");
	}

	//! The choice of a plugin and its use have to happen under one lock.
	void thePluginChoiceAndItsUseAreUnderOneLock()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		// The hook runs where loadFile() has chosen the plugin and is about to use it.
		// tryLock() never blocks, so it reports whether the mutex is free there.
		bool hookRan = false;
		bool lockHeldAtTheUse = false;
		instrument.m_testHook = [&]
			{
				hookRan = true;
				if (!instrument.m_pluginMutex.tryLock(0))
				{
					lockHeldAtTheUse = true;
				}
				else
				{
					instrument.m_pluginMutex.unlock();
				}
			};

		// A preset that is not there: the point of the case is the lock, not the preset.
		instrument.loadFile(QStringLiteral("/nonexistent/zyn-preset.xmz"));

		QVERIFY2(hookRan, "loadFile() did not reach the point where it uses the plugin");
		QVERIFY2(lockHeldAtTheUse,
			"loadFile() chose the plugin and used it without holding the plugin mutex, so a "
			"hosting-mode switch could delete the instance in between");
	}

	//! The hosting getters read the plugin pointer under the lock, too.
	void theHostingGettersReadThePluginUnderTheLock()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		bool hookRan = false;
		bool lockHeldAtTheRead = false;
		instrument.m_testHook = [&]
			{
				hookRan = true;
				if (!instrument.m_pluginMutex.tryLock(0))
				{
					lockHeldAtTheRead = true;
				}
				else
				{
					instrument.m_pluginMutex.unlock();
				}
			};

		const QString state = instrument.hostingState();
		QVERIFY2(hookRan, "hostingState() did not reach the point where it reads the plugin");
		QVERIFY2(lockHeldAtTheRead,
			"hostingState() read the plugin pointer without holding the plugin mutex, so a "
			"hosting-mode switch could delete the instance in between");
		QCOMPARE(state, QStringLiteral("in-process"));

		hookRan = false;
		lockHeldAtTheRead = false;
		const qint64 pid = instrument.hostingProcessId();
		QVERIFY2(hookRan, "hostingProcessId() did not reach the point where it reads the plugin");
		QVERIFY2(lockHeldAtTheRead,
			"hostingProcessId() read the plugin pointer without holding the plugin mutex, so a "
			"hosting-mode switch could delete the instance in between");
		QCOMPARE(pid, 0);
	}

	//! A MIDI event's frame offset has to survive the hand-off.
	void theHandoffKeepsTheEventOffset()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		// The event arrives with a frame offset inside the period.
		const MidiEvent noteOn{MidiNoteOn, 0, 60, 100};
		QVERIFY(instrument.handleMidiEvent(noteOn, TimePos(), 42));

		// The queue carries it to the next processed period - with its offset.
		HandoffMidiEvent queued;
		QVERIFY(instrument.m_midiHandoff.pop(queued));
		QCOMPARE(static_cast<int>(queued.type), static_cast<int>(MidiNoteOn));
		QCOMPARE(static_cast<int>(queued.offset), 42);
	}

	//! The drain hands the offset over to the plugin.
	void theDrainForwardsTheEventOffset()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		const MidiEvent noteOn{MidiNoteOn, 0, 60, 100};
		QVERIFY(instrument.handleMidiEvent(noteOn, TimePos(), 42));

		instrument.forwardQueuedMidi();
		QCOMPARE(instrument.m_testLastForwardedOffset, 42);
	}

	//! Renders one period through the instrument and returns the two planes.
	//! Leading return types on these private-slots helpers: the Qt 5.15.3 moc cannot parse a
	//! trailing one there (ci-v041-1).
	static std::array<std::vector<float>, 2> renderPeriod(TestInstrument& instrument, f_cnt_t frames)
	{
		std::array<std::vector<float>, 2> planes{std::vector<float>(frames), std::vector<float>(frames)};
		float* data[2] = {planes[0].data(), planes[1].data()};
		instrument.processImpl(PlanarBufferView<const float, 0>{nullptr, 0},
			PlanarBufferView<float, 2>{data, frames});
		return planes;
	}

	//! Whether the two renders carry the same samples in [from, to).
	static bool sameSamples(const std::array<std::vector<float>, 2>& a,
		const std::array<std::vector<float>, 2>& b, f_cnt_t from, f_cnt_t to)
	{
		for (ch_cnt_t channel = 0; channel < 2; ++channel)
		{
			for (f_cnt_t frame = from; frame < to; ++frame)
			{
				if (std::fabs(a[channel][frame] - b[channel][frame]) > 1e-9f) { return false; }
			}
		}
		return true;
	}

	//! The first frame in [from, to) at which the two renders differ, or `to` when they
	//! do not differ at all.
	static f_cnt_t firstDifference(const std::array<std::vector<float>, 2>& a,
		const std::array<std::vector<float>, 2>& b, f_cnt_t from, f_cnt_t to)
	{
		for (f_cnt_t frame = from; frame < to; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < 2; ++channel)
			{
				if (std::fabs(a[channel][frame] - b[channel][frame]) > 1e-6f) { return frame; }
			}
		}
		return to;
	}

	//! The scheduled frame decides where in the period the note starts: it starts in the
	//! first block at or after that frame, and not before it.
	void theScheduledFrameDecidesWhereTheNoteStarts()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		const auto frames = Engine::audioEngine()->framesPerPeriod();
		const auto block = static_cast<f_cnt_t>(LocalZynAddSubFx::SynthBlockSize);

		// The period without the note: the reference the note is compared against.
		const auto reference = renderPeriod(instrument, frames);

		// The note, scheduled 100 frames into the next period.
		const MidiEvent noteOn{MidiNoteOn, 0, 60, 100};
		QVERIFY(instrument.handleMidiEvent(noteOn, TimePos(), 100));
		const auto withNote = renderPeriod(instrument, frames);

		// Zyn renders in blocks, so the note starts at the first block boundary at or
		// after the frame it was scheduled at - and not before it.
		const auto onset = ((100 + block - 1) / block) * block;
		QVERIFY(onset > 0 && onset < frames);

		QVERIFY2(sameSamples(reference, withNote, 0, onset),
			"the note started before the block boundary at or after its scheduled frame");
		const auto first = firstDifference(reference, withNote, 0, frames);
		QVERIFY2(first < onset + block,
			"the note started later than the first block at or after its scheduled frame");
	}

	//! A note scheduled exactly on a block boundary starts in THAT block: Zyn generates
	//! the block that starts at a boundary when the render reaches the boundary, so an
	//! event that sits on one has to be applied before the render gets there.
	void theScheduledFrameOnABlockBoundaryStartsInThatBlock()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		const auto frames = Engine::audioEngine()->framesPerPeriod();
		const auto block = static_cast<f_cnt_t>(LocalZynAddSubFx::SynthBlockSize);

		const auto reference = renderPeriod(instrument, frames);

		// The note, scheduled exactly on the boundary of the first block.
		const MidiEvent noteOn{MidiNoteOn, 0, 60, 100};
		QVERIFY(instrument.handleMidiEvent(noteOn, TimePos(), block));
		const auto withNote = renderPeriod(instrument, frames);

		// The note starts in the block that begins at the scheduled frame.
		QVERIFY2(sameSamples(reference, withNote, 0, block),
			"the note started before the block it was scheduled on");
		const auto first = firstDifference(reference, withNote, 0, frames);
		QVERIFY2(first < block + block,
			"the note started in the block after the one it was scheduled on");
	}

private:
	bool m_engineInitialised = false;
};

QTEST_MAIN(ZynAddSubFxInstrumentTest)

#include "ZynAddSubFxInstrumentTest.moc"
