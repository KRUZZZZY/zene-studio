/*
 * TrackSynthesisLoadTest.cpp - each instrument track's synthesis load and the engine's late periods
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

/*! Q050-074.1, through the agent surface (mixer.cpu_load and track.overview) on a session the test builds:
 *  three tripleoscillator tracks on ONE mixer channel - two playing notes, one silent - render real periods.
 *  Each track's synthesis_percent is keyed by its id, the playing ones are above zero and the silent one is
 *  zero while all three share the channel's single cpu_percent (so it is not the channel's figure), the two
 *  reports agree, and a stopped song lets the figures decay. A period made late on purpose - a direct slot
 *  on nextAudioBuffer, which the render emits inside the timed period, spins past the deadline - is
 *  recorded at the song tick it started at and read back the same through mixer.cpu_load; a steady period
 *  with the meters running and a late mark being recorded allocates nothing. No timing value is asserted
 *  beyond "above zero" and "past the deadline". */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include <chrono>

#include "AllocationProbe.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Mixer.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! @a list's entry whose @a key is @a id, or an empty object.
QJsonObject entryWith(const QJsonArray& list, const QString& key, const QString& id)
{
	for (const QJsonValue& value : list)
	{
		if (value.toObject().value(key).toString() == id) { return value.toObject(); }
	}
	return {};
}

//! Spins the render thread past one period's deadline while armed - called inside the timed period.
struct LateMaker
{
	bool armed = false;
	void spin()
	{
		if (!armed) { return; }
		armed = false;
		const AudioEngine* engine = Engine::audioEngine();
		const auto period = std::chrono::microseconds(
			std::int64_t{1000000} * engine->framesPerPeriod() / engine->outputSampleRate());
		const auto until = std::chrono::steady_clock::now() + 3 * period;
		while (std::chrono::steady_clock::now() < until) {}
	}
};

} // namespace

class TrackSynthesisLoadTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_loud = nullptr;
	InstrumentTrack* m_soft = nullptr;
	InstrumentTrack* m_silent = nullptr;
	LateMaker m_late;

	InstrumentTrack* addTrack(const QString& name, int channel, const QList<int>& keys)
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		Instrument* instrument = track->loadInstrument(QStringLiteral("tripleoscillator"));
		if (instrument == nullptr || dynamic_cast<DummyInstrument*>(instrument) != nullptr) { return nullptr; }
		track->setName(name);
		track->mixerChannelModel()->setValue(channel);
		if (!keys.isEmpty())
		{
			auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
			for (const int key : keys) { clip->addNote(Note(TimePos(4 * 192), TimePos(0), key), false); }
		}
		return track;
	}

	static void render(int periods)
	{
		for (int i = 0; i < periods; ++i) { Engine::audioEngine()->renderNextPeriod(); }
	}

	static void playFrom(tick_t tick)
	{
		Song* song = Engine::getSong();
		song->stop();  // a case that failed while playing leaves the song rolling
		song->setPlayPos(tick, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		// A direct connection: the slot runs on the render thread, inside the period the profiler times.
		connect(Engine::audioEngine(), &AudioEngine::nextAudioBuffer, this, [this] { m_late.spin(); }, Qt::DirectConnection);

		Engine::getSong()->clearProject();
		const int channel = Engine::mixer()->createChannel();
		m_loud = addTrack(QStringLiteral("Loud"), channel, {48, 52, 55, 60, 64, 67});
		m_soft = addTrack(QStringLiteral("Soft"), channel, {60});
		m_silent = addTrack(QStringLiteral("Silent"), channel, {});
		if (m_loud == nullptr || m_soft == nullptr || m_silent == nullptr)
		{
			QSKIP("the tripleoscillator module is not built beside this test");
		}
	}

	void cleanupTestCase()
	{
		Engine::getSong()->stop();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void eachInstrumentReportsItsOwnSynthesisLoad()
	{
		playFrom(0);
		render(40);

		const ControlResult load = run(QStringLiteral("mixer.cpu_load"));
		QVERIFY2(load.ok, qPrintable(load.errorMessage));
		const QJsonArray tracks = load.result.value(QStringLiteral("tracks")).toArray();
		const QJsonObject loud = entryWith(tracks, QStringLiteral("track"), control::trackIdOf(m_loud));
		const QJsonObject soft = entryWith(tracks, QStringLiteral("track"), control::trackIdOf(m_soft));
		const QJsonObject silent = entryWith(tracks, QStringLiteral("track"), control::trackIdOf(m_silent));
		QVERIFY2(!loud.isEmpty() && !soft.isEmpty() && !silent.isEmpty(), "every instrument track is keyed by its id");
		QCOMPARE(loud.value(QStringLiteral("name")).toString(), QStringLiteral("Loud"));
		const double loudPercent = loud.value(QStringLiteral("synthesis_percent")).toDouble(-1.0);
		const double softPercent = soft.value(QStringLiteral("synthesis_percent")).toDouble(-1.0);
		QVERIFY2(loudPercent > 0.0, qPrintable(QStringLiteral("a playing track's synthesis: %1").arg(loudPercent)));
		QVERIFY2(softPercent > 0.0, qPrintable(QStringLiteral("a playing track's synthesis: %1").arg(softPercent)));
		QCOMPARE(silent.value(QStringLiteral("synthesis_percent")).toDouble(-1.0), 0.0);
		QVERIFY(load.result.value(QStringLiteral("synthesis_total_percent")).toDouble() >= loudPercent + softPercent - 1e-9);

		// track.overview: the same figure per track, beside the ONE channel figure all three share.
		const ControlResult overview = run(QStringLiteral("track.overview"));
		QVERIFY2(overview.ok, qPrintable(overview.errorMessage));
		const QJsonArray rows = overview.result.value(QStringLiteral("tracks")).toArray();
		const QJsonObject loudRow = entryWith(rows, QStringLiteral("id"), control::trackIdOf(m_loud));
		const QJsonObject silentRow = entryWith(rows, QStringLiteral("id"), control::trackIdOf(m_silent));
		QVERIFY2(!loudRow.isEmpty() && !silentRow.isEmpty(), "track.overview lists the tracks by id");
		QCOMPARE(loudRow.value(QStringLiteral("synthesis_cpu_percent")).toDouble(-1.0), loudPercent);
		QCOMPARE(silentRow.value(QStringLiteral("synthesis_cpu_percent")).toDouble(-1.0), 0.0);
		QCOMPARE(loudRow.value(QStringLiteral("channel")).toString(), silentRow.value(QStringLiteral("channel")).toString());
		QCOMPARE(loudRow.value(QStringLiteral("cpu_percent")).toDouble(), silentRow.value(QStringLiteral("cpu_percent")).toDouble());

		// Stopped, nothing synthesises: the figure decays instead of holding the last reading.
		Engine::getSong()->stop();
		render(200);
		const QJsonObject after = entryWith(run(QStringLiteral("mixer.cpu_load")).result.value(QStringLiteral("tracks")).toArray(),
			QStringLiteral("track"), control::trackIdOf(m_loud));
		QVERIFY2(after.value(QStringLiteral("synthesis_percent")).toDouble(-1.0) < loudPercent * 0.01,
			qPrintable(QStringLiteral("after 200 silent periods: %1").arg(after.value(QStringLiteral("synthesis_percent")).toDouble())));
	}

	void aLatePeriodIsMarkedAtItsSongPosition()
	{
		const tick_t at = 2 * 192;  // bar 3 in 4/4
		playFrom(at);
		const LatePeriodLog& log = Engine::audioEngine()->latePeriods();
		const std::uint64_t before = log.count();
		QCOMPARE(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks(), at);
		m_late.armed = true;
		render(1);
		QVERIFY(!m_late.armed);
		Engine::getSong()->stop();
		QVERIFY2(log.count() > before, "the late period was not counted");

		const ControlResult load = run(QStringLiteral("mixer.cpu_load"));
		QVERIFY2(load.ok, qPrintable(load.errorMessage));
		const QJsonObject late = load.result.value(QStringLiteral("late_periods")).toObject();
		QCOMPARE(static_cast<std::uint64_t>(late.value(QStringLiteral("count")).toDouble()), log.count());
		const QJsonArray marks = late.value(QStringLiteral("marks")).toArray();
		QVERIFY(!marks.isEmpty());
		QJsonObject mark;
		for (const QJsonValue& value : marks)
		{
			if (value.toObject().value(QStringLiteral("serial")).toDouble() > static_cast<double>(before)
				&& value.toObject().value(QStringLiteral("tick")).toInt() == at) { mark = value.toObject(); }
		}
		QVERIFY2(!mark.isEmpty(), "no mark at the period's song position");
		QCOMPARE(mark.value(QStringLiteral("position")).toString(), QStringLiteral("3.1"));
		QVERIFY(mark.value(QStringLiteral("playing")).toBool());
		QVERIFY(mark.value(QStringLiteral("took_us")).toDouble() > mark.value(QStringLiteral("period_us")).toDouble());
		QVERIFY(mark.value(QStringLiteral("percent")).toDouble() > 100.0);

		// The engine's own log holds the same mark the command reported.
		bool same = false;
		for (const LatePeriodLog::Mark& held : log.recent())
		{
			same = same || (static_cast<double>(held.serial) == mark.value(QStringLiteral("serial")).toDouble()
				&& held.tick == at && static_cast<double>(held.tookMicros) == mark.value(QStringLiteral("took_us")).toDouble());
		}
		QVERIFY(same);
	}

	void theRingKeepsTheNewestMarksAndTheWholeCount()
	{
		LatePeriodLog log;
		for (std::uint32_t i = 0; i < LatePeriodLog::Capacity + 5; ++i) { log.record(static_cast<tick_t>(i), true, 2000 + i, 1000); }
		QCOMPARE(log.count(), static_cast<std::uint64_t>(LatePeriodLog::Capacity + 5));
		const std::vector<LatePeriodLog::Mark> marks = log.recent();
		QCOMPARE(marks.size(), LatePeriodLog::Capacity);
		QCOMPARE(marks.front().serial, std::uint64_t{6});
		QCOMPARE(marks.front().tick, tick_t{5});
		QCOMPARE(marks.back().tookMicros, static_cast<std::uint32_t>(2000 + LatePeriodLog::Capacity + 4));
	}

	void aSteadyPeriodWithTheMetersAllocatesNothing()
	{
		playFrom(0);
		render(8);  // past the notes' first period, which builds their voices
		m_late.armed = true;  // and a late mark being recorded in the counted period
		const std::uint64_t before = Engine::audioEngine()->latePeriods().count();
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		Engine::audioEngine()->renderNextPeriod();
		lmms::test::tlCountAllocations = false;
		Engine::getSong()->stop();
		QVERIFY(Engine::audioEngine()->latePeriods().count() > before);
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(TrackSynthesisLoadTest)
#include "TrackSynthesisLoadTest.moc"
