/*
 * Lb302NotePathTest.cpp - the LB302 note path must stay realtime-safe.
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*
 * Finding new-p14-134 ("LADSPA retry exhaustion logs from the note path" -
 * the file and the line in the finding name the LB302 enqueue failsafe, and
 * plugins/Lb302/ holds no LADSPA code): the bounded note-enqueue path of the
 * LB302 called qDebug() when its retry budget ran out. That path is the
 * instrument's per-note callback and it runs on the audio render thread
 * (AudioEngine::renderStageInstruments -> NotePlayHandle::play ->
 * Instrument::playNote -> Lb302Synth::playNoteImpl), where logging formats a
 * message - it allocates - and takes Qt's logging lock.
 *
 * The plugin's own sources are compiled into this test binary (the
 * SlicerTZoomTest / Vst3EffectIntegrationTest pattern), so the code under test
 * is the shipped one and the test loads no plugin module: it runs on every
 * platform, with no platform skip.
 *
 * The failsafe is reached by structure and never by timing: the instrument's
 * note ring buffer holds a fixed number of notes, so enqueuing far more notes
 * than it can hold makes the enqueues past the capacity find the buffer full,
 * burn their retry budget and take the failsafe - the same number of times on
 * every run.
 */

#include <QtTest>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include <QCoreApplication>
#include <QString>

#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Lb302.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "TimePos.h"

namespace lmms
{

namespace
{

/*! Counts the allocations this binary makes while a watch is active.
 *
 * The watch is thread local, so only the allocations of the thread that
 * started it are counted - the render thread's own allocations are none of
 * this test's business.
 */
struct AllocationWatch
{
	inline static thread_local bool active = false;
	inline static thread_local std::size_t allocations = 0;
	inline static thread_local std::size_t deallocations = 0;

	static void begin() noexcept
	{
		allocations = 0;
		deallocations = 0;
		active = true;
	}

	static void end() noexcept { active = false; }

	static void allocated() noexcept { if (active) { ++allocations; } }
	static void freed() noexcept { if (active) { ++deallocations; } }
};

/*! Captures the log messages the OWNING thread emits while it is alive.
 *
 * The messages are counted and the first one is kept; they are not printed,
 * so a failing run stays readable. The state is thread local: a message from
 * another thread (the render thread) is neither recorded nor raced on.
 */
class LogCapture
{
public:
	LogCapture() : m_previous(qInstallMessageHandler(&LogCapture::handler))
	{
		s_count = 0;
		firstMessage().clear();
	}
	~LogCapture() { qInstallMessageHandler(m_previous); }
	LogCapture(const LogCapture&) = delete;
	LogCapture& operator=(const LogCapture&) = delete;

	int count() const { return s_count; }
	QString first() const { return firstMessage(); }

private:
	static void handler(QtMsgType, const QMessageLogContext&, const QString& message)
	{
		++s_count;
		if (firstMessage().isEmpty()) { firstMessage() = message; }
	}

	inline static thread_local int s_count = 0;
	// Function-local, not an inline static member: MSVC warns (C5046, an error under /WX) about the
	// thread-exit destructor of a non-trivial thread_local member of a type with internal linkage.
	static QString& firstMessage() { thread_local QString s; return s; }
	QtMessageHandler m_previous = nullptr;
};

//! The number of enqueues the test makes. The ring buffer of the instrument
//! holds far fewer notes than this, so the enqueues past its capacity take
//! the failsafe.
constexpr int EnqueueAttempts = 512;

} // namespace

} // namespace lmms

// The watch has to see every allocation this binary makes, so the global
// operators are replaced. The plugin under test is linked into this binary,
// so its allocations are counted as well.
void* operator new(std::size_t size)
{
	lmms::AllocationWatch::allocated();
	if (void* const memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
	throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
	lmms::AllocationWatch::freed();
	std::free(memory);
}

void operator delete[](void* memory) noexcept
{
	::operator delete(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
	::operator delete(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
	::operator delete(memory);
}

class Lb302NotePathTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	//! new-p14-134: the retry exhaustion of the note path must not log.
	void lb302RetryExhaustionDoesNotLog();

	//! The drop the failsafe takes is counted, and reported off the audio thread.
	void lb302RetryExhaustionIsCountedAndReportedOffTheAudioThread();

private:
	//! Enqueues enough notes to take the enqueue failsafe, and reports what the
	//! note path did while it did so.
	//! @param logged the number of log messages the note path emitted
	//! @param allocations the number of allocations the note path made
	//! @param deallocations the number of frees the note path made
	void exhaustTheEnqueueRetries(int& logged, std::size_t& allocations, std::size_t& deallocations);
};

void Lb302NotePathTest::initTestCase()
{
	lmms::Engine::init(true);
	lmms::NotePlayHandleManager::init();
	QVERIFY2(lmms::Engine::audioEngine() != nullptr, "engine failed to initialise");
	lmms::Engine::audioEngine()->audioDev()->stopProcessing();
	QVERIFY(lmms::Engine::audioEngine()->framesPerPeriod() > 0);
}

void Lb302NotePathTest::cleanupTestCase()
{
	lmms::Engine::destroy();
}

void Lb302NotePathTest::exhaustTheEnqueueRetries(
	int& logged, std::size_t& allocations, std::size_t& deallocations)
{
	using namespace lmms;

	InstrumentTrack track{Engine::getSong()};
	Lb302Synth synth{&track};

	const auto frames = Engine::audioEngine()->framesPerPeriod();
	const Note note{TimePos{static_cast<tick_t>(frames)}, TimePos{0}, DefaultKey};
	NotePlayHandle handle{&track, 0, frames, note, nullptr, -1, NotePlayHandle::Origin::MidiClip};
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	const std::span<SampleFrame> buffer{data};

	QString firstMessage;
	{
		LogCapture capture;
		AllocationWatch::begin();
		for (int attempt = 0; attempt < EnqueueAttempts; ++attempt)
		{
			synth.playNote(&handle, buffer);
		}
		AllocationWatch::end();
		logged = capture.count();
		firstMessage = capture.first();
	}
	allocations = AllocationWatch::allocations;
	deallocations = AllocationWatch::deallocations;

	if (logged > 0)
	{
		qWarning("the LB302 note path logged: %s", qPrintable(firstMessage));
	}
}

void Lb302NotePathTest::lb302RetryExhaustionDoesNotLog()
{
	int logged = 0;
	std::size_t allocations = 0;
	std::size_t deallocations = 0;
	exhaustTheEnqueueRetries(logged, allocations, deallocations);

	QVERIFY2(logged == 0,
		qPrintable(QString{"the LB302 note path logged %1 message(s) when its enqueue retries "
			"ran out"}.arg(logged)));
	QVERIFY2(allocations == 0 && deallocations == 0,
		qPrintable(QString{"the LB302 note path allocated %1 time(s) and freed %2 time(s) "
			"when its enqueue retries ran out"}
			.arg(static_cast<qulonglong>(allocations))
			.arg(static_cast<qulonglong>(deallocations))));
}

void Lb302NotePathTest::lb302RetryExhaustionIsCountedAndReportedOffTheAudioThread()
{
	using namespace lmms;

	InstrumentTrack track{Engine::getSong()};
	Lb302Synth synth{&track};

	const auto frames = Engine::audioEngine()->framesPerPeriod();
	const Note note{TimePos{static_cast<tick_t>(frames)}, TimePos{0}, DefaultKey};
	NotePlayHandle handle{&track, 0, frames, note, nullptr, -1, NotePlayHandle::Origin::MidiClip};
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	const std::span<SampleFrame> buffer{data};

	{
		// The capture keeps a failing run readable: if the note path logs, the
		// messages are counted here instead of filling the log.
		LogCapture capture;
		for (int attempt = 0; attempt < EnqueueAttempts; ++attempt)
		{
			synth.playNote(&handle, buffer);
		}
	}

	// The drop is counted in a lock-free counter the control thread reads.
	quint64 dropped = 0;
	QVERIFY2(QMetaObject::invokeMethod(&synth, "droppedNoteCount", Qt::DirectConnection,
		Q_RETURN_ARG(quint64, dropped)),
		"the LB302 instrument does not expose the dropped-note counter");
	QVERIFY2(dropped > 0, "the LB302 enqueue failsafe did not record a dropped note");

	// The report runs on the control thread and names the count.
	int reported = 0;
	QString report;
	{
		LogCapture capture;
		QVERIFY2(QMetaObject::invokeMethod(&synth, "reportDroppedNotes", Qt::DirectConnection),
			"the LB302 instrument does not expose the dropped-note report");
		reported = capture.count();
		report = capture.first();
	}
	QVERIFY2(reported == 1,
		qPrintable(QString{"the dropped-note report logged %1 message(s)"}.arg(reported)));
	QVERIFY2(report.contains(QString::number(dropped)),
		qPrintable(QString{"the dropped-note report does not name the count: %1"}.arg(report)));

	quint64 afterReport = 1;
	QVERIFY(QMetaObject::invokeMethod(&synth, "droppedNoteCount", Qt::DirectConnection,
		Q_RETURN_ARG(quint64, afterReport)));
	QVERIFY2(afterReport == 0, "the dropped-note report did not drain the counter");
}

QTEST_MAIN(Lb302NotePathTest)

#include "Lb302NotePathTest.moc"
