/*
 * SmfInterchangeRoundTripTest.cpp - the Standard MIDI File round trip that reads
 *                                   the FILE back and compares the MAP.
 *
 * Feature row 33 of docs/FEATURE-LIST-0.3.0.md. This is the proof the feature
 * list names explicitly: a map with tempo AND metre changes is exported, the
 * session's map is cleared, the FILE is imported, and what is then compared is
 * the MAP - event for event and as sampled step functions - against an oracle
 * built from what the test authored. The file's hash is reported by the export
 * and is deliberately NOT the comparison.
 *
 * Four claims, each measured:
 *
 *  1. THE ROUND TRIP: export -> read the file back -> clear the map -> import ->
 *     compare (a) the events on the wire, (b) the engine's own TempoMap object,
 *     (c) `Song::tempoAtTick` and the map's metre at every event tick and its
 *     neighbours, against the oracle.
 *  2. THE TICK-0 SEED: a map whose first event is later than tick 0 exports the
 *     step in force at tick 0, and importing that file leaves every sampled
 *     tempo and metre UNCHANGED - the seed says what the engine already
 *     answered, because an SMF has no global tempo before its first event.
 *  3. ONE UNDO: `control.undo` after an import restores the map that was there
 *     before it, because the tempo map is not inside the Song's checkpoint.
 *  4. A FOREIGN DIVISION (96 and 1000 ppq) is scaled onto LMMS'
 *     48-ticks-per-quarter grid with the rounded events reported, and a file
 *     that needs more ticks than the map holds is REFUSED rather than
 *     truncated.
 *
 * The other half - the surface, the file's own bytes and the typed refusals - is
 * tests/src/core/SmfInterchangeTest.cpp. Both are registered ctests and both
 * share tests/src/core/SmfInterchangeTestSupport.h.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <vector>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"

#include "SmfInterchangeTestSupport.h"

using namespace lmms;
using namespace smfsupport;

namespace
{

//! One event, field by field: an equality failure that prints only "FALSE" says
//! nothing about WHICH half of WHICH event drifted, and every field here is a
//! candidate for a writer/reader disagreement.
QString eventText(const TempoMapEvent& event)
{
	return QStringLiteral("{tick %1, has_tempo %2, bpm %3, has_signature %4, %5/%6}")
		.arg(static_cast<qint64>(event.tick)).arg(event.hasTempo).arg(event.tempo)
		.arg(event.hasTimeSignature).arg(event.numerator).arg(event.denominator);
}

//! The first event the two maps disagree on - or the ACTIVE flag, when every
//! event agrees. The failure message of the claim at the end of this file.
QString firstDifference(const TempoMap& live, const TempoMap& rebuilt)
{
	const std::span<const TempoMapEvent> authored = live.all();
	const std::span<const TempoMapEvent> other = rebuilt.all();
	if (authored.size() != other.size())
	{
		return QStringLiteral("the maps hold %1 and %2 events")
			.arg(static_cast<int>(authored.size())).arg(static_cast<int>(other.size()));
	}
	for (std::size_t i = 0; i < authored.size(); ++i)
	{
		if (!(authored[i] == other[i]))
		{
			return QStringLiteral("event %1: live %2 vs rebuilt %3")
				.arg(static_cast<int>(i)).arg(eventText(authored[i]), eventText(other[i]));
		}
	}
	return QStringLiteral("every event is equal, so the maps differ in active (%1 vs %2)")
		.arg(live.active()).arg(rebuilt.active());
}

} // namespace

class SmfInterchangeRoundTripTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_directory.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every test starts from a project the map has never touched.
	void init()
	{
		Engine::getSong()->clearProject();
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}

	//! Claim 1: the required proof.
	void theRoundTripComparesTheMapAndNotTheHash()
	{
		const std::vector<PlainEvent> authored = {
			{0, 140, 4, 4},
			{3840, 0, 3, 4},     // LMMS tick 384: metre only
			{7680, 90, 0, 0},    // LMMS tick 768: tempo only
			{13440, 180, 7, 8}}; // LMMS tick 1344: both
		for (const PlainEvent& event : authored)
		{
			QJsonObject args{{QStringLiteral("tick"), static_cast<qint64>(event.lmmsTick())}};
			if (event.bpm > 0) { args.insert(QStringLiteral("bpm"), event.bpm); }
			if (event.numerator > 0)
			{
				args.insert(QStringLiteral("numerator"), event.numerator);
				args.insert(QStringLiteral("denominator"), event.denominator);
			}
			QVERIFY(run(QStringLiteral("transport.tempo_map_add"), args).ok);
		}
		const QJsonArray authoredState = mapEvents();
		QCOMPARE(authoredState.size(), 4);

		const QString file = path(m_directory, QStringLiteral("round-trip.mid"));
		const QJsonObject exported = run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}}).result;
		QCOMPARE(exported.value(QStringLiteral("seed_events")).toInt(), 0);
		QCOMPARE(exported.value(QStringLiteral("sha256")).toString().size(), 64);
		QCOMPARE(exported.value(QStringLiteral("event_count")).toInt(), 4);
		QCOMPARE(exported.value(QStringLiteral("last_tick")).toInt(), 1344);

		// (a) READ THE FILE BACK: the file's own conductor events, in LMMS
		// ticks, with no session state involved.
		const QJsonObject read = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), file}}).result;
		QCOMPARE(read.value(QStringLiteral("format")).toInt(), 1);
		QCOMPARE(read.value(QStringLiteral("ticks_per_quarter")).toInt(), 480);
		QCOMPARE(read.value(QStringLiteral("rounded_events")).toInt(), 0);
		QCOMPARE(read.value(QStringLiteral("superseded_events")).toInt(), 0);
		QCOMPARE(read.value(QStringLiteral("capacity_events")).toInt(), 0);
		QCOMPARE(read.value(QStringLiteral("importable")).toBool(), true);
		QCOMPARE(read.value(QStringLiteral("event_count")).toVariant().toLongLong(), 4LL);
		QCOMPARE(digest(read.value(QStringLiteral("events")).toArray()), digest(authoredState));

		// (b) THE MAP: clear the session's map, import the file, compare the map
		// - not the file, not its hash.
		QVERIFY(run(QStringLiteral("transport.tempo_map_clear")).ok);
		QCOMPARE(mapEvents().size(), 0);
		const QJsonObject imported = run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), file}}).result;
		QCOMPARE(imported.value(QStringLiteral("active")).toBool(), true);
		QCOMPARE(digest(mapEvents()), digest(authoredState));

		// ... and the engine's OWN object, not just its JSON view.
		TempoMap rebuilt;
		for (const QJsonValue& value : authoredState)
		{
			const QJsonObject entry = value.toObject();
			TempoMapEvent event;
			event.tick = static_cast<tick_t>(
				entry.value(QStringLiteral("tick")).toVariant().toLongLong());
			event.hasTempo = entry.value(QStringLiteral("has_tempo")).toBool();
			event.tempo = entry.value(QStringLiteral("bpm")).toInt();
			event.hasTimeSignature = entry.value(QStringLiteral("has_time_signature")).toBool();
			event.numerator = entry.value(QStringLiteral("numerator")).toInt();
			event.denominator = entry.value(QStringLiteral("denominator")).toInt();
			QVERIFY(rebuilt.addEvent(event));
		}
		rebuilt.setActive(true);
		QVERIFY2(Engine::getSong()->tempoMap().map() == rebuilt,
			qPrintable(firstDifference(Engine::getSong()->tempoMap().map(), rebuilt)));

		// (c) Both step functions, on and around every event tick, against the
		// oracle that reads the AUTHORED events.
		const int globalTempo = static_cast<int>(Engine::getSong()->getTempo());
		const QString globalMeter = QStringLiteral("%1/%2")
			.arg(Engine::getSong()->getTimeSigModel().numeratorModel().value())
			.arg(Engine::getSong()->getTimeSigModel().denominatorModel().value());
		for (tick_t tick : {static_cast<tick_t>(0), static_cast<tick_t>(1),
				static_cast<tick_t>(383), static_cast<tick_t>(384), static_cast<tick_t>(385),
				static_cast<tick_t>(767), static_cast<tick_t>(768), static_cast<tick_t>(769),
				static_cast<tick_t>(1343), static_cast<tick_t>(1344), static_cast<tick_t>(1345),
				static_cast<tick_t>(1920), static_cast<tick_t>(10000)})
		{
			QCOMPARE(Engine::getSong()->tempoAtTick(tick),
				oracleTempoAt(authored, globalTempo, tick));
			QCOMPARE(meterAt(tick), oracleMeterAt(authored, globalMeter, tick));
		}
	}

	//! Claim 2: the tick-0 seed keeps a session's meaning.
	void aMapWithoutATickZeroEventIsSeeded()
	{
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 384}, {QStringLiteral("bpm"), 90},
				{QStringLiteral("numerator"), 3}, {QStringLiteral("denominator"), 4}}).ok);

		// What the session answers BEFORE the export.
		Song* song = Engine::getSong();
		const int globalTempo = static_cast<int>(song->getTempo());
		std::vector<QPair<tick_t, int>> beforeTempo;
		std::vector<QPair<tick_t, QString>> beforeMeter;
		for (tick_t tick : {static_cast<tick_t>(0), static_cast<tick_t>(100),
				static_cast<tick_t>(383), static_cast<tick_t>(384), static_cast<tick_t>(1000)})
		{
			beforeTempo.push_back({tick, song->tempoAtTick(tick)});
			beforeMeter.push_back({tick, meterAt(tick)});
		}

		const QString file = path(m_directory, QStringLiteral("seeded.mid"));
		const QJsonObject exported = run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}}).result;
		// Both halves are seeded: the map names neither at tick 0.
		QCOMPARE(exported.value(QStringLiteral("seed_events")).toInt(), 2);
		QCOMPARE(exported.value(QStringLiteral("first_tick")).toInt(), 0);
		QCOMPARE(exported.value(QStringLiteral("event_count")).toInt(), 2);

		const QJsonObject read = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), file}}).result;
		QCOMPARE(read.value(QStringLiteral("event_count")).toVariant().toLongLong(), 2LL);
		const QJsonObject seed = read.value(QStringLiteral("events")).toArray().at(0).toObject();
		QCOMPARE(seed.value(QStringLiteral("tick")).toVariant().toLongLong(), 0LL);
		QCOMPARE(seed.value(QStringLiteral("bpm")).toInt(), globalTempo);
		QCOMPARE(seed.value(QStringLiteral("has_time_signature")).toBool(), true);

		// Import it: every sampled answer must be the one the session gave
		// before, with the tick-0 event doing the work the global values did.
		QVERIFY(run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), file}}).ok);
		for (const QPair<tick_t, int>& sample : beforeTempo)
		{
			QCOMPARE(song->tempoAtTick(sample.first), sample.second);
		}
		for (const QPair<tick_t, QString>& sample : beforeMeter)
		{
			QCOMPARE(meterAt(sample.first), sample.second);
		}
	}

	//! Claim 3: the map captured before an import comes back on control.undo.
	void importIsOneUndoStep()
	{
		// The map the file will carry.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 90},
				{QStringLiteral("numerator"), 5}, {QStringLiteral("denominator"), 4}}).ok);
		const QString file = path(m_directory, QStringLiteral("undo.mid"));
		QVERIFY(run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}}).ok);
		const QJsonArray fromFile = mapEvents();
		QCOMPARE(fromFile.size(), 1);

		// A DIFFERENT map is what the session holds when the import lands.
		QVERIFY(run(QStringLiteral("transport.tempo_map_clear")).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 192}, {QStringLiteral("bpm"), 140}}).ok);
		const QJsonArray before = mapEvents();
		QVERIFY(!digest(before).isEmpty());

		QVERIFY(run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), file}}).ok);
		QCOMPARE(digest(mapEvents()), digest(fromFile));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(digest(mapEvents()), digest(before));
	}

	//! Claim 4: a foreign division, and a file the map cannot hold.
	void aForeignDivisionIsScaledOntoTheLmmsGrid()
	{
		const QString exact = path(m_directory, QStringLiteral("ppq96.mid"));
		QVERIFY(writeFile(exact, plainFile(96, {{96, 90, 6, 8}})));  // one quarter note in
		const QJsonObject exactRead = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), exact}}).result;
		QCOMPARE(exactRead.value(QStringLiteral("ticks_per_quarter")).toInt(), 96);
		QCOMPARE(exactRead.value(QStringLiteral("rounded_events")).toInt(), 0);
		QCOMPARE(exactRead.value(QStringLiteral("event_count")).toVariant().toLongLong(), 1LL);
		const QJsonObject exactEvent =
			exactRead.value(QStringLiteral("events")).toArray().at(0).toObject();
		QCOMPARE(exactEvent.value(QStringLiteral("tick")).toVariant().toLongLong(), 48LL);
		QCOMPARE(exactEvent.value(QStringLiteral("bpm")).toInt(), 90);
		QCOMPARE(exactEvent.value(QStringLiteral("numerator")).toInt(), 6);
		QCOMPARE(exactEvent.value(QStringLiteral("denominator")).toInt(), 8);

		// 1000 ppq: 1 tick in is 0.048 of a LMMS tick, so it rounds to tick 0
		// and the report SAYS it rounded rather than hiding it.
		const QString rounded = path(m_directory, QStringLiteral("ppq1000.mid"));
		QVERIFY(writeFile(rounded, plainFile(1000, {{1, 120, 0, 0}})));
		const QJsonObject roundedRead = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), rounded}}).result;
		QCOMPARE(roundedRead.value(QStringLiteral("ticks_per_quarter")).toInt(), 1000);
		QCOMPARE(roundedRead.value(QStringLiteral("rounded_events")).toInt(), 1);
		QCOMPARE(roundedRead.value(QStringLiteral("events")).toArray().at(0).toObject()
			.value(QStringLiteral("tick")).toVariant().toLongLong(), 0LL);

		// A file needing more ticks than the map holds is reported as such
		// (capacity_events, importable false) and is NOT truncated into the map.
		std::vector<PlainEvent> many;
		for (int index = 0; index <= TempoMap::MaxEvents; index++)
		{
			many.push_back({static_cast<quint32>(index) * 480, 60 + index % 10, 0, 0});
		}
		const QString beyond = path(m_directory, QStringLiteral("beyond-capacity.mid"));
		QVERIFY(writeFile(beyond, plainFile(480, many)));
		const QJsonObject beyondRead = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), beyond}}).result;
		QCOMPARE(beyondRead.value(QStringLiteral("event_count")).toVariant().toLongLong(),
			static_cast<qint64>(TempoMap::MaxEvents + 1));
		QCOMPARE(beyondRead.value(QStringLiteral("capacity_events")).toInt(), 1);
		QCOMPARE(beyondRead.value(QStringLiteral("importable")).toBool(), false);

		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 90}}).ok);
		const QJsonArray before = mapEvents();
		const ControlResult refused = run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), beyond}});
		QVERIFY2(!refused.ok, "the import of a file the map cannot hold was accepted");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(digest(mapEvents()), digest(before));  // a refusal writes nothing
	}

private:
	QTemporaryDir m_directory;
};

QTEST_GUILESS_MAIN(SmfInterchangeRoundTripTest)
#include "SmfInterchangeRoundTripTest.moc"
