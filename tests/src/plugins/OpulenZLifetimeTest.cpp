/*
 * OpulenZLifetimeTest.cpp - the OpulenZ instrument's teardown must not free
 * the emulator while the audio thread can still reach it.
 *
 * The instrument's destructor used to delete the emulator BEFORE it removed
 * the instrument's play handles, and it did so without the emulator mutex.
 * A play handle that the audio thread is processing at that moment can
 * therefore enter playImpl() and use the freed emulator.
 *
 * The test drives the real instrument code (the plugin's sources are compiled
 * into this translation unit) and watches the teardown from the outside:
 *
 *   - the play handle the instrument registers with the audio engine is
 *     replaced by a test double whose destructor reports when the engine
 *     disposed of it. That disposal happens inside
 *     AudioEngine::removePlayHandlesOfTypes(), which is also where the audio
 *     thread is synchronised with the control thread.
 *   - the emulator is replaced by a subclass whose destructor reports when
 *     the emulator was freed.
 *   - the emulator mutex is held for the whole teardown, exactly like
 *     playImpl() holds it while it renders, so the test can see whether the
 *     emulator was freed while a callback could still be inside it.
 *
 * Both events are recorded, so the order between them is a fact and not a
 * race: the test never sleeps and never decides the verdict by a timeout.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <QTest>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// The real InstrumentPlayHandle is kept out of this translation unit: the test
// substitutes its own double for it. Nothing else in the engine names the
// class (the engine only uses PlayHandle::Type::InstrumentPlayHandle), so the
// header's own include guard is enough to keep it out.
#define LMMS_INSTRUMENT_PLAY_HANDLE_H

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentView.h"
#include "NotePlayHandle.h"
#include "PlayHandle.h"
#include "Song.h"
#include "Track.h"

#include "embed.h"
#include "Knob.h"
#include "PixmapButton.h"

// The adplug emulator the plugin drives. Included before the rename below, so
// that the real class keeps its name.
#include <opl.h>
#include <temuopl.h>

// The plugin's own class, with its private members visible to this test. Its
// dependencies are included above, so the relaxation only reaches OpulenZ.h.
#define private public
#include "OpulenZ.h"
#undef private

namespace lmms
{

namespace
{

//! What the teardown did, in the order it did it.
struct LifetimeEvents
{
	std::mutex mutex;
	std::condition_variable cv;

	int sequence = 0;
	//! Sequence number of the play handle's disposal, -1 while it lives.
	int handleDestroyedAt = -1;
	//! Sequence number of the emulator's destruction, -1 while it lives.
	int emulatorDestroyedAt = -1;
	//! True when the emulator was freed while the emulator mutex was held.
	bool emulatorDestroyedWhileMutexHeld = false;
	//! True while the test holds the emulator mutex.
	bool emulatorMutexHeld = false;

	void recordHandleDestroyed()
	{
		{
			const std::lock_guard<std::mutex> lock(mutex);
			handleDestroyedAt = ++sequence;
		}
		cv.notify_all();
	}

	void recordEmulatorDestroyed()
	{
		{
			const std::lock_guard<std::mutex> lock(mutex);
			emulatorDestroyedAt = ++sequence;
			emulatorDestroyedWhileMutexHeld = emulatorMutexHeld;
		}
		cv.notify_all();
	}

	void setEmulatorMutexHeld(bool held)
	{
		const std::lock_guard<std::mutex> lock(mutex);
		emulatorMutexHeld = held;
	}

	//! Waits for the play handle to be disposed. The engine disposes of it in
	//! every teardown, so this is a handshake; the timeout is a watchdog that
	//! turns a hang into a failed assertion instead of a stuck test run.
	bool waitForHandleDestroyed()
	{
		std::unique_lock<std::mutex> lock(mutex);
		return cv.wait_for(lock, std::chrono::seconds(60),
			[this] { return handleDestroyedAt >= 0; });
	}
};

LifetimeEvents g_events;

} // namespace

//! The play handle the instrument registers with the audio engine. It is the
//! real class' contract (include/InstrumentPlayHandle.h) with a destructor
//! that reports when the engine disposed of it.
class TrackedPlayHandle : public PlayHandle
{
public:
	TrackedPlayHandle(Instrument* instrument, InstrumentTrack* instrumentTrack)
		: PlayHandle(Type::InstrumentPlayHandle)
		, m_instrument(instrument)
		, m_instrumentTrack(instrumentTrack)
	{
		setAudioBusHandle(instrumentTrack->audioBusHandle());
	}

	~TrackedPlayHandle() override { g_events.recordHandleDestroyed(); }

	void play(std::span<SampleFrame> buffer) override { m_instrument->play(buffer); }

	bool isFinished() const override { return false; }

	bool isFromTrack(const Track* track) const override { return m_instrumentTrack == track; }

private:
	Instrument* m_instrument;
	InstrumentTrack* m_instrumentTrack;
};

//! The name the plugin's sources use for their play handle.
using InstrumentPlayHandle = TrackedPlayHandle;

namespace
{

//! The emulator the plugin instantiates, with a destructor that reports the
//! moment the emulator is freed.
class TrackedTemuopl : public CTemuopl
{
public:
	using CTemuopl::CTemuopl;

	~TrackedTemuopl() override { g_events.recordEmulatorDestroyed(); }
};

} // namespace

} // namespace lmms

// The plugin's implementation, compiled into this translation unit so that the
// two classes above can stand in for the ones the plugin creates. The rename is
// undone right after, so the rest of the test sees the real names.
#define CTemuopl TrackedTemuopl
#include "../../../plugins/OpulenZ/OpulenZ.cpp"
#undef CTemuopl

namespace lmms
{

class OpulenZLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// No audio thread renders in this test (the dummy device never
		// calls back), so the test itself is the only thread that can reach
		// the instrument.
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	void teardownRemovesThePlayHandlesBeforeTheEmulatorIsFreed();
};

void OpulenZLifetimeTest::teardownRemovesThePlayHandlesBeforeTheEmulatorIsFreed()
{
	auto track = std::make_unique<InstrumentTrack>(Engine::getSong());
	auto* instrument = new OpulenzInstrument(track.get());
	// The test drives the instrument's own teardown, so the track must not
	// delete it a second time.
	instrument->setParent(nullptr);

	// An audio callback is inside the emulator: playImpl() holds the emulator
	// mutex for the whole render, so the mutex stays locked for the whole
	// teardown here as well.
	g_events.setEmulatorMutexHeld(true);
	OpulenzInstrument::emulatorMutex.lock();

	// The control thread deletes the instrument while that callback runs.
	std::thread destroyer([instrument] { delete instrument; });

	// The engine disposes of the play handle inside removePlayHandlesOfTypes(),
	// in the broken teardown as well as in the fixed one. Waiting for that
	// event is therefore a handshake and not a timeout: once it arrived, the
	// teardown either freed the emulator already (the broken order) or is
	// about to block on the emulator mutex (the fixed order).
	const bool handleRemoved = g_events.waitForHandleDestroyed();

	// The teardown may now continue.
	g_events.setEmulatorMutexHeld(false);
	OpulenzInstrument::emulatorMutex.unlock();
	destroyer.join();

	QVERIFY2(handleRemoved, "the instrument's play handle was never disposed of");
	QVERIFY2(g_events.emulatorDestroyedAt >= 0, "the emulator was never freed");

	QVERIFY2(g_events.handleDestroyedAt < g_events.emulatorDestroyedAt,
		qPrintable(QStringLiteral(
			"the emulator was freed before the instrument's play handles were removed "
			"(play handle disposed at %1, emulator freed at %2): an audio callback that "
			"is processing the handle can still reach the freed emulator")
			.arg(g_events.handleDestroyedAt).arg(g_events.emulatorDestroyedAt)));

	QVERIFY2(!g_events.emulatorDestroyedWhileMutexHeld,
		"the emulator was freed while the emulator mutex was held: a callback inside "
		"playImpl() would use the freed emulator");
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::OpulenZLifetimeTest)
#include "OpulenZLifetimeTest.moc"
