/*
 * Lv2MidiHandoffTest.cpp - Lv2Proc's MIDI input handoff is bounded and lock-free
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

/*! Cluster new-p14-156: Lv2Proc handed MIDI input to the plug-in through a ringbuffer guarded by an
 *  atomic_flag spinlock. handleMidiInputEvent() runs on the GUI thread (live input) and on the audio
 *  thread (the song's own notes), so a producer that lost the flag spun for as long as the holder was
 *  descheduled: an unbounded wait, on the audio thread. The handoff is now the bounded, lock-free
 *  MidiHandoffQueue: a full queue or an exhausted reservation budget refuses and counts the event,
 *  and no producer waits for another. This test drives the processor's own handoff. */

#include "lmmsconfig.h"

#include <QtTest>

#ifdef LMMS_HAVE_LV2

#include <QDir>
#include <QFile>
#include <type_traits>
#include <utility>

#include "AllocationProbe.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Lv2ControlBase.h"
#include "Lv2Manager.h"
#include "Lv2Proc.h"
#include "MidiEvent.h"
#include "MidiHandoffQueue.h"
#include "PluginFactory.h"
#include "RealtimeScope.h"
#include "Song.h"
#include "Track.h"
#include <lilv/lilv.h>

using namespace lmms;

namespace
{

//! The URI of the LV2 fixture instrument this build puts on LV2_PATH; it has a MIDI input.
constexpr const char* FixtureUri = "urn:zene:test:lv2-instrument";

/*! Whether Lv2Proc exposes the bounded, lock-free MIDI handoff the fix adds. The seam is absent on
 *  the code the bug was reported in, so this test then FAILS instead of silently passing. */
template<typename Proc, typename = void>
struct HasMidiHandoffSeam : std::false_type {};

template<typename Proc>
struct HasMidiHandoffSeam<Proc, std::void_t<decltype(std::declval<Proc&>().midiInputHandoffForTest())>>
	: std::true_type {};

/*! Whether handleMidiInputEvent() in @a file still spin-waits on a producer lock. Reading the
 *  function's own text keeps the check on the exact construct the finding names - an atomic_flag's
 *  test_and_set spin - however the queue around it is spelled. */
bool midiProducerPathSpins(const QString& file, QString* body)
{
	QFile source{file};
	if (!source.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		*body = QStringLiteral("cannot read %1").arg(file);
		return true;
	}
	const QString text = QString::fromUtf8(source.readAll());
	const qsizetype start = text.indexOf(QStringLiteral("void Lv2Proc::handleMidiInputEvent"));
	if (start < 0)
	{
		*body = QStringLiteral("handleMidiInputEvent not found in %1").arg(file);
		return true;
	}
	const qsizetype end = text.indexOf(QStringLiteral("\n}"), start);
	*body = text.mid(start, end < 0 ? -1 : end - start);
	return body->contains(QStringLiteral("test_and_set"))
		|| body->contains(QStringLiteral("atomic_flag"))
		|| body->contains(QStringLiteral("m_ringLock"));
}

//! A processor of the LV2 fixture instrument (it has a MIDI input), or null with @a why set.
Lv2Proc* fixtureProcessor(QString* why)
{
	const PluginFactory::PluginInfo& info = getPluginFactory()->pluginInfo("lv2instrument");
	if (info.isNull()) { *why = QStringLiteral("this build has no lv2instrument module"); return nullptr; }
	Lv2Manager* manager = Engine::getLv2Manager();
	if (manager == nullptr || manager->getPlugin(QString::fromUtf8(FixtureUri)) == nullptr)
	{
		*why = QStringLiteral("the LV2 fixture instrument is not in this build's LV2 world");
		return nullptr;
	}
	auto* track = dynamic_cast<InstrumentTrack*>(
		Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track == nullptr) { *why = QStringLiteral("no instrument track"); return nullptr; }
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("uri"), QString::fromUtf8(FixtureUri));
	Plugin::Descriptor::SubPluginFeatures::Key key(info.descriptor,
		QStringLiteral("Zene LV2 Test Instrument"), attributes);
	Instrument* instrument = track->loadInstrument(QStringLiteral("lv2instrument"), &key);
	auto* controls = dynamic_cast<Lv2ControlBase*>(instrument);
	if (controls == nullptr || controls->control(0) == nullptr)
	{
		*why = QStringLiteral("the LV2 fixture instrument did not instantiate");
		return nullptr;
	}
	return controls->control(0);
}

/*! The fixed handoff's contract, driven on the processor's own queue: events come out in order, a
 *  full queue refuses (and counts) instead of waiting, a producer whose reservation keeps losing is
 *  refused after a bounded number of attempts, the consumer drains the queue, and producing
 *  allocates nothing. The competing reservation is injected, so no thread or clock is involved. */
template<typename Proc, std::enable_if_t<HasMidiHandoffSeam<Proc>::value, int> = 0>
void checkMidiInputHandoff(Proc& proc)
{
	using Queue = std::remove_reference_t<decltype(proc.midiInputHandoffForTest())>;
	using Event = typename Queue::EventType;
	Queue& queue = proc.midiInputHandoffForTest();
	QVERIFY2(proc.hasNoteInput(), "the fixture instrument has a MIDI input");
	QCOMPARE(queue.dropped(), std::uint64_t{0});

	// An event as the producers build it: the key names the event, the offset is its position.
	const auto note = [](f_cnt_t key, f_cnt_t offset)
	{
		Event event{};
		event.ev = MidiEvent(MidiNoteOn, 0, static_cast<int16_t>(key), 100);
		event.time = TimePos(offset);
		event.offset = offset;
		return event;
	};

	// Normal path: the production producer's events come out in order, time and offset intact.
	for (f_cnt_t offset = 0; offset < 4; ++offset)
	{
		proc.handleMidiInputEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(offset), offset);
	}
	for (f_cnt_t offset = 0; offset < 4; ++offset)
	{
		Event event{};
		QVERIFY2(queue.pop(event), "an event the producer queued is in the handoff");
		QCOMPARE(event.offset, offset);
		QCOMPARE(static_cast<int>(event.ev.type()), static_cast<int>(MidiNoteOn));
		QCOMPARE(event.time.getTicks(), static_cast<tick_t>(offset));
	}
	{
		Event empty{};
		QVERIFY(!queue.pop(empty));
	}

	// Exhaustion: the queue holds its capacity, and one more producer event is REFUSED (and counted),
	// not made to wait. The accepted events are still in order, and the queue recovers once drained.
	const auto capacity = Queue::capacity();
	QCOMPARE(queue.consumed(), std::uint64_t{4});
	const auto droppedBefore = queue.dropped();
	for (std::size_t i = 0; i < capacity; ++i)
	{
		proc.handleMidiInputEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(0),
			static_cast<f_cnt_t>(i));
	}
	QCOMPARE(queue.enqueued(), std::uint64_t{4} + capacity);
	proc.handleMidiInputEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(0), -1);
	QCOMPARE(queue.enqueued(), std::uint64_t{4} + capacity);
	QCOMPARE(queue.dropped(), droppedBefore + 1);
	for (std::size_t i = 0; i < capacity; ++i)
	{
		Event event{};
		QVERIFY(queue.pop(event));
		QCOMPARE(event.offset, static_cast<f_cnt_t>(i));
	}
	{
		Event empty{};
		QVERIFY(!queue.pop(empty));
	}
	proc.handleMidiInputEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(0), 7);
	{
		Event recovered{};
		QVERIFY2(queue.pop(recovered), "the handoff recovers once the audio thread drained it");
		QCOMPARE(recovered.offset, f_cnt_t{7});
	}

	// Contention: a reservation that keeps losing the producer race is refused after the queue's
	// bounded budget. Nothing here waits for the competing producer, which is the whole point.
	const auto droppedBeforeContention = queue.dropped();
	std::uint64_t refusedAt = 0;
	std::uint32_t attempts = 0;
	std::uint32_t competitors = 0;
	bool competitorsAccepted = true;
	const bool accepted = queue.pushWithTestHook(note(70, 0), &refusedAt, &attempts,
		[&](std::uint32_t)
		{
			if (queue.push(note(static_cast<f_cnt_t>(80 + competitors), 0))) { ++competitors; }
			else { competitorsAccepted = false; }
		});
	QVERIFY2(!accepted, "a producer that loses the reservation race is refused, not left waiting");
	QVERIFY(competitorsAccepted);
	QCOMPARE(attempts, Queue::MaxPushAttempts);
	QCOMPARE(competitors, Queue::MaxPushAttempts);
	QCOMPARE(queue.dropped(), droppedBeforeContention + 1);
	for (std::uint32_t i = 0; i < competitors; ++i)
	{
		Event event{};
		QVERIFY(queue.pop(event));
		QCOMPARE(event.ev.key(), static_cast<int16_t>(80 + i));
	}

	// The consumer side: copyModelsFromCore() takes every queued event and leaves none.
	queue.push(note(90, 0));
	queue.push(note(91, 1));
	QCOMPARE(queue.enqueued() - queue.consumed(), std::uint64_t{2});
	proc.copyModelsFromCore();
	QCOMPARE(queue.enqueued(), queue.consumed());
	{
		Event empty{};
		QVERIFY(!queue.pop(empty));
	}

	// Audio thread: producing an event allocates and frees nothing.
	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	{
		const RealtimeScope realtime;
		for (f_cnt_t offset = 0; offset < 8; ++offset)
		{
			proc.handleMidiInputEvent(MidiEvent(MidiNoteOn, 0, 60, 100), TimePos(0), offset);
		}
	}
	lmms::test::tlCountAllocations = false;
	QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
	for (f_cnt_t offset = 0; offset < 8; ++offset)
	{
		Event event{};
		QVERIFY(queue.pop(event));
	}
}

//! The code the finding was reported in: a producer that loses the flag spins on it. Reachable only
//! without the bounded, lock-free handoff, so the test FAILS (it does not silently pass) there.
template<typename Proc, std::enable_if_t<!HasMidiHandoffSeam<Proc>::value, int> = 0>
void checkMidiInputHandoff(Proc&)
{
	QFAIL("Lv2Proc hands its MIDI producers a contended spinlock (m_ringLock): cluster new-p14-156");
}

} // namespace

#endif // LMMS_HAVE_LV2

class Lv2MidiHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_HAVE_LV2
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
#ifdef LV2_TEST_INSTRUMENT_DIR
		// The fixture instrument is found the way lilv finds any plug-in: on LV2_PATH.
		qputenv("LV2_PATH", LV2_TEST_INSTRUMENT_DIR);
#endif
		Engine::init(true);
#endif
	}

	void cleanupTestCase()
	{
#ifdef LMMS_HAVE_LV2
		Engine::destroy();
#endif
	}

	void midiInputHandoffIsBoundedAndLockFree()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		QString why;
		Lv2Proc* proc = fixtureProcessor(&why);
		if (proc == nullptr) { QSKIP(qPrintable(why)); }

		// The finding's own construct: a producer spin (atomic_flag's test_and_set) in the MIDI path.
		QString producerBody;
		const bool producerSpins = midiProducerPathSpins(
			QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}
				.filePath(QStringLiteral("../src/core/lv2/Lv2Proc.cpp")),
			&producerBody);

		checkMidiInputHandoff(*proc);

		QVERIFY2(!producerSpins,
			qPrintable(QStringLiteral("Lv2Proc::handleMidiInputEvent still spins on the producer lock: ")
				+ producerBody.left(500)));
#endif
	}
};

QTEST_MAIN(Lv2MidiHandoffTest)
#include "Lv2MidiHandoffTest.moc"
