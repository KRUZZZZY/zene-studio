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
 *   - theAudioPathNeverWaitsForThePluginMutex(): the audio path must never WAIT for
 *     the mutex. The main thread holds it for as long as a reload takes (the client
 *     handshake is seconds), and the old export policy made the audio thread wait for
 *     it - indefinitely - so a render could stall for the whole reload.
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
 * No case waits for a wall-clock outcome and none of them races: the lock state is
 * read with QMutex::tryLock(), which never blocks, and the one place where a thread
 * has to be waited for (the audio thread's lock attempt) is waited for with a
 * bounded acquire whose only job is to turn a hang into a failure - the assertion
 * itself is the value the audio path returns.
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

#include <atomic>
#include <thread>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "Song.h"
#include "ZynAddSubFx.h"

using namespace lmms;

namespace
{

//! The instrument with the members a white-box regression test drives.
class TestInstrument final : public ZynAddSubFxInstrument
{
public:
	using ZynAddSubFxInstrument::ZynAddSubFxInstrument;

	using ZynAddSubFxInstrument::forwardQueuedMidi;
	using ZynAddSubFxInstrument::m_midiHandoff;
	using ZynAddSubFxInstrument::m_pluginMutex;
	using ZynAddSubFxInstrument::processLock;

#ifdef LMMS_TESTING
	using ZynAddSubFxInstrument::m_testForceExport;
	using ZynAddSubFxInstrument::m_testHook;
	using ZynAddSubFxInstrument::m_testLastForwardedOffset;
#endif
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

	//! The audio path must not wait for the plugin mutex.
	void theAudioPathNeverWaitsForThePluginMutex()
	{
		InstrumentTrack track{Engine::getSong()};
		TestInstrument instrument{&track};

		// The main thread holds the plugin mutex, the way a reload does while it
		// waits for the remote client.
		instrument.m_pluginMutex.lock();

		// The export policy is the one that used to wait: with the song exporting,
		// the audio path blocked on the mutex for as long as the main thread held it.
		instrument.m_testForceExport = true;

		QSemaphore answered;
		std::atomic<bool> tookTheLock{false};
		std::thread audioThread{[&]
			{
				tookTheLock.store(instrument.processLock());
				answered.release();
			}};

		// The audio path has to come back on its own. The bounded acquire only turns
		// a hang into a failure; the assertion is the value the audio path returns.
		const bool returned = answered.tryAcquire(1, 2000);

		bool mainHoldsTheLock = true;
		if (!returned)
		{
			// The audio thread is stuck in the lock: release it so the thread can finish.
			instrument.m_pluginMutex.unlock();
			mainHoldsTheLock = false;
		}
		audioThread.join();
		if (tookTheLock.load())
		{
			// The audio thread got the lock after the release: it holds it now.
			instrument.m_pluginMutex.unlock();
			mainHoldsTheLock = false;
		}
		if (mainHoldsTheLock) { instrument.m_pluginMutex.unlock(); }

		QVERIFY2(returned, "the audio path waited for the plugin mutex instead of skipping the period");
		QVERIFY2(!tookTheLock.load(), "the audio path took the plugin mutex the main thread was holding");
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

private:
	bool m_engineInitialised = false;
};

QTEST_MAIN(ZynAddSubFxInstrumentTest)

#include "ZynAddSubFxInstrumentTest.moc"
