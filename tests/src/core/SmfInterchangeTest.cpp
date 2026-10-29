/*
 * SmfInterchangeTest.cpp - the Standard MIDI File conductor track: the
 *                          `interchange.*` surface and the file's own bytes.
 *
 * Feature row 33 of docs/FEATURE-LIST-0.3.0.md (tempo-map export / SMF cross-DAW
 * interchange). The engine half is include/SmfInterchange.h +
 * src/core/SmfInterchange.cpp and the surface is
 * src/core/ControlCommandsInterchange.cpp; the claims measured here, each by
 * running the thing rather than by arguing about it:
 *
 *  1. THE FOUR IDS ARE REGISTERED AND CLASSIFIED: the `interchange` group, both
 *     schemas, a description, no `requires` excuse (headless parity), the
 *     mutating flag, and an A16 row whose class matches what the command does.
 *  2. THE FILE IS A STANDARD MIDI FILE, checked BYTE BY BYTE by this suite's own
 *     parser (SmfInterchangeTestSupport.h - it shares no code with the module
 *     under test), so "another DAW can read it" rests on the file's own
 *     structure: MThd, format 1, one MTrk, division 480, the tempo meta event's
 *     three microseconds-per-quarter bytes, the time-signature meta event's
 *     nn/dd/cc/bb, end of track.
 *  3. THE TEMPO ENCODING IS EXACT: bpm -> microseconds -> bpm is the identity
 *     for every integer tempo the engine accepts (10..999).
 *  4. THE CONVENTION IS ON THE WIRE: `interchange.smf_convention` reports the
 *     PPQ, the LMMS ticks per quarter and the exact ratio between them.
 *  5. REFUSALS ARE TYPED and write nothing: a missing file, a file that is not a
 *     Standard MIDI File, an SMPTE division, a relative export path and an
 *     existing file without `overwrite`.
 *
 * The other half - the round trip that reads the FILE back and compares the MAP,
 * the tick-0 seed, the undo and the foreign divisions - is
 * tests/src/core/SmfInterchangeRoundTripTest.cpp. Both are registered ctests
 * (tests/CMakeLists.txt) and both share tests/src/core/SmfInterchangeTestSupport.h.
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

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <vector>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"

#include "SmfInterchange.h"
#include "SmfInterchangeTestSupport.h"

using namespace lmms;
using namespace smfsupport;

class SmfInterchangeTest : public QObject
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

	//! Claim 1 and 4: the group's contract.
	void theGroupIsRegisteredAndClassified()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList ids = {
			QStringLiteral("interchange.smf_convention"),
			QStringLiteral("interchange.smf_export"),
			QStringLiteral("interchange.smf_read"),
			QStringLiteral("interchange.smf_import")};
		for (const QString& id : ids)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(id));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("interchange"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY(cmd->requiresDecl.isEmpty());
			QCOMPARE(cmd->mutating, id == QStringLiteral("interchange.smf_import"));

			const control::ReversibilityEntry* row =
				control::ReversibilityTable::instance().lookup(id);
			QVERIFY2(row != nullptr, qPrintable(id + QStringLiteral(" has no contract row")));
			const control::ReversibilityClass expected =
				id == QStringLiteral("interchange.smf_import")
					? control::ReversibilityClass::TrueInverse
					: control::ReversibilityClass::NotMutating;
			QCOMPARE(row->cls, expected);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}

		const QJsonObject convention = run(QStringLiteral("interchange.smf_convention")).result;
		QCOMPARE(convention.value(QStringLiteral("ticks_per_quarter")).toInt(), 480);
		QCOMPARE(convention.value(QStringLiteral("lmms_ticks_per_quarter")).toInt(), 48);
		QCOMPARE(convention.value(QStringLiteral("smf_ticks_per_lmms_tick")).toInt(), 10);
		for (const QString& key : {QStringLiteral("tempo_unit"),
				QStringLiteral("time_signature_encoding"), QStringLiteral("track_shape"),
				QStringLiteral("tick_zero_rule"), QStringLiteral("stated_limits")})
		{
			QVERIFY2(!convention.value(key).toString().isEmpty(), qPrintable(key));
		}
	}

	//! Claim 2: the bytes, without the module's reader.
	void theFileIsAWellFormedStandardMidiFile()
	{
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 140},
				{QStringLiteral("numerator"), 4}, {QStringLiteral("denominator"), 4}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 384}, {QStringLiteral("bpm"), 90},
				{QStringLiteral("numerator"), 3}, {QStringLiteral("denominator"), 4}}).ok);

		const QString file = path(m_directory, QStringLiteral("well-formed.mid"));
		const QJsonObject exported = run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}}).result;
		QCOMPARE(exported.value(QStringLiteral("ticks_per_quarter")).toInt(), 480);
		QCOMPARE(exported.value(QStringLiteral("format")).toInt(), 1);
		QCOMPARE(exported.value(QStringLiteral("track_count")).toInt(), 1);
		QCOMPARE(exported.value(QStringLiteral("seed_events")).toInt(), 0);
		QCOMPARE(exported.value(QStringLiteral("sha256")).toString().size(), 64);

		QFile handle(file);
		QVERIFY(handle.open(QIODevice::ReadOnly));
		const QByteArray bytes = handle.readAll();
		handle.close();
		QCOMPARE(static_cast<qint64>(bytes.size()),
			exported.value(QStringLiteral("bytes")).toVariant().toLongLong());

		int format = 0;
		int trackCount = 0;
		int division = 0;
		std::vector<MetaEvent> events;
		QString error;
		QVERIFY2(parseConductorFile(bytes, &format, &trackCount, &division, &events, &error),
			qPrintable(error));
		QCOMPARE(format, 1);
		QCOMPARE(trackCount, 1);
		QCOMPARE(division, 480);
		QVERIFY(!events.empty());
		QCOMPARE(events.back().type, static_cast<unsigned char>(0x2F));  // end of track
		// The end-of-track meta event CLOSES the track: the writer emits it with a
		// delta of 0 after the last real event (src/core/SmfInterchange.cpp,
		// trackBody), so its ABSOLUTE tick is that last event's - 3840 here, the
		// second tempo/metre pair - and that is the track's end as any reader sees
		// it. This slot asserted tick == 0, which is the DELTA, not the position:
		// the file was well formed and the expectation was the transposed one.
		QCOMPARE(events.back().delta, static_cast<quint32>(0));
		QCOMPARE(events.back().tick, events[events.size() - 2].tick);

		// The two meta events the format defines for a conductor track, at the
		// ticks LMMS' own 48-per-quarter grid implies (384 -> 3840 at 480 ppq):
		// 140 bpm is 428571 us per quarter and 90 bpm is 666667, which is the
		// format's own arithmetic and not the module's report of it.
		int tempoEvents = 0;
		int meterEvents = 0;
		for (const MetaEvent& event : events)
		{
			if (event.type == 0x51)
			{
				tempoEvents++;
				QCOMPARE(event.payload.size(), 3);
				const int microseconds = (static_cast<unsigned char>(event.payload[0]) << 16)
					| (static_cast<unsigned char>(event.payload[1]) << 8)
					| static_cast<unsigned char>(event.payload[2]);
				if (event.tick == 0) { QCOMPARE(microseconds, 428571); }
				else
				{
					QCOMPARE(microseconds, 666667);
					QCOMPARE(event.tick, static_cast<quint32>(3840));
				}
			}
			if (event.type == 0x58)
			{
				meterEvents++;
				QCOMPARE(event.payload.size(), 4);
				QCOMPARE(static_cast<int>(static_cast<unsigned char>(event.payload[2])), 24);
				QCOMPARE(static_cast<int>(static_cast<unsigned char>(event.payload[3])), 8);
				if (event.tick == 3840)
				{
					QCOMPARE(static_cast<int>(static_cast<unsigned char>(event.payload[0])), 3);
					QCOMPARE(static_cast<int>(static_cast<unsigned char>(event.payload[1])), 2);
				}
			}
		}
		QCOMPARE(tempoEvents, 2);
		QCOMPARE(meterEvents, 2);
		QCOMPARE(exported.value(QStringLiteral("tempo_events")).toInt(), 2);
		QCOMPARE(exported.value(QStringLiteral("meter_events")).toInt(), 2);
		// The last event's tick, in LMMS' domain, is what the report says.
		QCOMPARE(exported.value(QStringLiteral("last_tick")).toInt(), 384);
	}

	//! Claim 3: the encoding is the identity on every tempo the engine accepts.
	//! 990 values, measured: the quotient spacing of 60e6/bpm over 10..999 is
	//! wider than one microsecond, so the rounding cannot collide.
	void theTempoEncodingIsExactForEveryAcceptedBpm()
	{
		for (int bpm = TempoMapMinTempo; bpm <= TempoMapMaxTempo; bpm++)
		{
			const qint64 microseconds = (60000000 + bpm / 2) / bpm;
			QVERIFY2(microseconds > 0 && microseconds <= 0xFFFFFF,
				qPrintable(QStringLiteral("bpm %1 does not fit three bytes").arg(bpm)));
			QCOMPARE(static_cast<int>(std::llround(60000000.0 / microseconds)), bpm);
		}
	}

	//! Claim 5: every refusal is typed, and none of them writes.
	void refusalsAreTyped()
	{
		// A missing file is not_found, on both readers.
		QCOMPARE(run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), path(m_directory, QStringLiteral("absent.mid"))}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), path(m_directory, QStringLiteral("absent.mid"))}}).errorKind,
			ControlErrorKind::NotFound);
		// A required argument that is absent is the registry's own refusal.
		QCOMPARE(run(QStringLiteral("interchange.smf_read")).errorKind,
			ControlErrorKind::InvalidArgs);
		// The typed refusals carry a message.
		QVERIFY(!run(QStringLiteral("interchange.smf_read")).errorMessage.isEmpty());

		// A file that is not a Standard MIDI File is invalid_args, naming why.
		const QString text = path(m_directory, QStringLiteral("not-midi.mid"));
		QVERIFY(writeFile(text, QByteArrayLiteral("this is not a MIDI file at all\n")));
		const ControlResult notMidi = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), text}});
		QVERIFY(!notMidi.ok);
		QCOMPARE(notMidi.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(notMidi.errorMessage.contains(QStringLiteral("MThd")));

		// An SMPTE division (the high bit set) has no tick grid to read.
		const QString smpte = path(m_directory, QStringLiteral("smpte.mid"));
		QByteArray smpteBytes = plainFile(480, {{0, 120, 4, 4}});
		smpteBytes[12] = static_cast<char>(0xE7);  // 25 fps, 40 subframes
		smpteBytes[13] = static_cast<char>(0x28);
		QVERIFY(writeFile(smpte, smpteBytes));
		QCOMPARE(run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), smpte}}).errorKind, ControlErrorKind::InvalidArgs);

		// A relative export path, and an existing file without `overwrite`.
		QCOMPARE(run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), QStringLiteral("relative.mid")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		const QString file = path(m_directory, QStringLiteral("exists.mid"));
		QVERIFY(run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}}).ok);
		QCOMPARE(run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}}).errorKind, ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}, {QStringLiteral("overwrite"), true}}).ok);

		// An import of a file the session is not: the map is untouched.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 90}}).ok);
		const QJsonArray before = mapEvents();
		const ControlResult refused = run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), text}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(digest(mapEvents()), digest(before));

		// A tempo outside the ENGINE's own bounds is refused by the import (the
		// reader reports it, the engine decides), and the map is untouched.
		const QString slow = path(m_directory, QStringLiteral("too-slow.mid"));
		QByteArray slowBytes = plainFile(480, {{0, 120, 4, 4}});
		// 0xFFFFFF microseconds per quarter is 4 bpm - below MinTempo (10).
		const int usOffset = slowBytes.indexOf(QByteArrayLiteral("\xFF\x51\x03"));
		QVERIFY(usOffset > 0);
		slowBytes[usOffset + 3] = static_cast<char>(0xFF);
		slowBytes[usOffset + 4] = static_cast<char>(0xFF);
		slowBytes[usOffset + 5] = static_cast<char>(0xFF);
		QVERIFY(writeFile(slow, slowBytes));
		const QJsonObject slowRead = run(QStringLiteral("interchange.smf_read"),
			{{QStringLiteral("path"), slow}}).result;
		QCOMPARE(slowRead.value(QStringLiteral("events")).toArray().at(0).toObject()
			.value(QStringLiteral("bpm")).toInt(), 4);
		QCOMPARE(slowRead.value(QStringLiteral("importable")).toBool(), false);
		QVERIFY(!slowRead.value(QStringLiteral("importability_error")).toString().isEmpty());
		const ControlResult tooFast = run(QStringLiteral("interchange.smf_import"),
			{{QStringLiteral("path"), slow}});
		QVERIFY(!tooFast.ok);
		QCOMPARE(tooFast.errorKind, ControlErrorKind::Refused);
		QVERIFY(tooFast.errorMessage.contains(QStringLiteral("tick 0")));
		QCOMPARE(digest(mapEvents()), digest(before));
	}

//! A delta time in a Standard MIDI File is a variable-length quantity of at
	//! most FOUR bytes (0x0FFFFFFF file ticks), and a file tick is ten LMMS
	//! ticks, so the widest gap the format can carry is 26,843,545 LMMS ticks.
	//! A wider one used to be written as the five-byte quantity appendVlq can
	//! An export to a path that holds a DANGLING symbolic link is refused without
	//! `overwrite`. QFileInfo::exists is false for a link whose target is gone, so
	//! the guard let the export through and the commit replaced the link. A link
	//! is occupied, as it is for the hand-off and the clip save: nothing is written
	//! and the link stays. Unix-only: QFile::link writes a .lnk shortcut on Windows.
	void aDanglingLinkAtTheExportPathIsOccupied()
	{
#ifndef Q_OS_UNIX
		QSKIP("symlinks are exercised on Unix only: QFile::link writes a .lnk shortcut on Windows");
#else
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 90}}).ok);
		const QString file = path(m_directory, QStringLiteral("dangling.mid"));
		const QString target = path(m_directory, QStringLiteral("never-created.mid"));
		QVERIFY2(QFile::link(target, file), "the fixture symlink was not created");
		QVERIFY2(!QFileInfo::exists(file), "the fixture link is not dangling");

		const ControlResult exported = run(QStringLiteral("interchange.smf_export"),
			{{QStringLiteral("path"), file}});
		QVERIFY2(!exported.ok, "an export replaced a dangling link without overwrite");
		QCOMPARE(exported.errorKind, ControlErrorKind::Refused);
		QVERIFY(exported.errorMessage.contains(QStringLiteral("already exists")));
		QVERIFY2(QFileInfo(file).isSymLink(), "the refused export replaced the link");
		QVERIFY2(!QFileInfo::exists(target), "the refused export wrote the link's target");
#endif
	}

	//! emit - a file no reader of the format would accept - and the scaled tick
	//! was narrowed to quint32, which wraps above 429,496,729 LMMS ticks. A map
	//! the format cannot carry is refused, and no file is left behind; one it
	//! can carry is still written.
	void aGapTooWideForTheFormatIsRefused()
	{
		constexpr int MaxDeltaTicks = 0x0FFFFFFF;
		const int widestGap = MaxDeltaTicks / interchange::SmfTicksPerLmmsTick;

		auto mapWithSecondEventAt = [](int tick) {
			TempoMap map;
			// The map must be ACTIVE for its events to be in force: an inactive
			// map exports only the tick-0 seed, so the wide gap would never
			// reach the file and this case would prove nothing.
			map.setActive(true);
			TempoMapEvent first;
			first.tick = 0;
			first.hasTempo = true;
			first.tempo = 120;
			map.addEvent(first);
			TempoMapEvent second = first;
			second.tick = tick;
			map.addEvent(second);
			return map;
		};

		// One LMMS tick too wide: the delta does not fit four bytes.
		const QString tooWide = path(m_directory, QStringLiteral("gap-too-wide.mid"));
		QString error;
		QVERIFY2(!interchange::writeConductorTrack(tooWide, mapWithSecondEventAt(widestGap + 1),
			120, TempoMapTimeSignature{4, 4}, nullptr, &error),
			"a delta time beyond the format's four bytes is refused");
		QVERIFY(!error.isEmpty());
		QVERIFY2(!QFile::exists(tooWide), "and no file is left behind");

		// The widest gap the format CAN carry is still written, and reads back.
		const QString widest = path(m_directory, QStringLiteral("gap-widest.mid"));
		QVERIFY2(interchange::writeConductorTrack(widest, mapWithSecondEventAt(widestGap),
			120, TempoMapTimeSignature{4, 4}, nullptr, &error),
			qPrintable(error));
		QFile handle(widest);
		QVERIFY(handle.open(QIODevice::ReadOnly));
		const QByteArray bytes = handle.readAll();
		int format = 0;
		int trackCount = 0;
		int division = 0;
		std::vector<MetaEvent> events;
		QVERIFY2(parseConductorFile(bytes, &format, &trackCount, &division, &events, &error),
			qPrintable(error));
		// The last event before the end-of-track is the second tempo, at the wide
		// tick exactly: 26,843,545 LMMS ticks is 268,435,450 file ticks.
		QCOMPARE(events[events.size() - 2].tick,
			static_cast<quint32>(widestGap * interchange::SmfTicksPerLmmsTick));
	}

	//! Each delta can fit a four-byte VLQ while their absolute position exceeds
	//! the reader's 32-bit file-tick range. The last event must be refused by the
	//! writer rather than producing a file its own reader cannot accept.
	void anAbsoluteFileTickBeyondTheReaderRangeIsRefused()
	{
		TempoMap map;
		map.setActive(true);
		for (tick_t tick = 0; tick <= 430'000'000; tick += 20'000'000)
		{
			TempoMapEvent event;
			event.tick = tick;
			event.hasTempo = true;
			event.tempo = 120 + static_cast<int>((tick / 20'000'000) % 2);
			QVERIFY(map.addEvent(event));
		}

		// 4.3 billion file ticks is still inside SmfMaxFileTicks.
		QString error;
		const QString withinRange = path(m_directory, QStringLiteral("absolute-tick-within.mid"));
		QVERIFY2(interchange::writeConductorTrack(withinRange, map, 120,
			TempoMapTimeSignature{4, 4}, nullptr, &error), qPrintable(error));
		QVERIFY2(interchange::readConductorTrack(withinRange).ok,
			"the largest in-range event sequence must read back");

		// The same 200-million-file-tick deltas now reach 4.4 billion.
		TempoMapEvent beyondRange;
		beyondRange.tick = 440'000'000;
		beyondRange.hasTempo = true;
		beyondRange.tempo = 120;
		QVERIFY(map.addEvent(beyondRange));
		const QString beyond = path(m_directory, QStringLiteral("absolute-tick-beyond.mid"));
		const bool wroteBeyondRange = interchange::writeConductorTrack(beyond, map, 120,
			TempoMapTimeSignature{4, 4}, nullptr, &error);
		if (wroteBeyondRange)
		{
			const interchange::SmfReadReport refused = interchange::readConductorTrack(beyond);
			QVERIFY2(!refused.ok, "the baseline writer's over-limit output must trigger the reader limit");
		}
		QVERIFY2(!wroteBeyondRange,
			"a writer output beyond the reader's absolute tick range is not readable");
		QVERIFY2(error.contains(QStringLiteral("reader can hold")), qPrintable(error));
		QVERIFY(!QFile::exists(beyond));
	}

	//! A global tempo outside the engine's own bounds never reaches the file's
	//! microseconds-per-quarter arithmetic. An EMPTY map answers the caller's
	//! global tempo at every tick, so the tick-0 seed carries whatever the caller
	//! passed - and (60000000 + 0) / 0 is an integer division by zero, which is
	//! a crash rather than a wrong byte. A tempo the engine does not hold is
	//! refused, with a reason and no file; a tempo it does hold is still written.
	void aGlobalTempoTheEngineDoesNotHoldIsRefused()
	{
		const QString file = path(m_directory, QStringLiteral("bad-global-tempo.mid"));
		for (const int tempo : {0, -120, TempoMapMaxTempo + 1})
		{
			QString error;
			QVERIFY2(!interchange::writeConductorTrack(file, TempoMap{}, tempo,
				TempoMapTimeSignature{4, 4}, nullptr, &error),
				qPrintable(QStringLiteral("global tempo %1 was accepted").arg(tempo)));
			QVERIFY2(!error.isEmpty(), "the refusal says why");
			QVERIFY2(!QFile::exists(file), "and no file is left behind");
		}

		// The engine's own bounds are the accepted ones: the slowest tempo it
		// holds still writes.
		QString error;
		QVERIFY2(interchange::writeConductorTrack(file, TempoMap{}, TempoMapMinTempo,
			TempoMapTimeSignature{4, 4}, nullptr, &error), qPrintable(error));
		QVERIFY(QFile::exists(file));
	}

	//! A global metre becomes the tick-0 seed when the map has no signature there.
	//! Refuse values the engine cannot hold instead of silently changing them in
	//! the SMF payload; ordinary signatures such as 3/4 remain valid.
	void aGlobalTimeSignatureTheEngineDoesNotHoldIsRefused()
	{
		const QString file = path(m_directory, QStringLiteral("bad-global-signature.mid"));
		for (const TempoMapTimeSignature signature : {TempoMapTimeSignature{0, 4},
				TempoMapTimeSignature{33, 4}, TempoMapTimeSignature{3, 3},
				TempoMapTimeSignature{3, 64}})
		{
			QString error;
			QVERIFY2(!interchange::writeConductorTrack(file, TempoMap{}, 120, signature,
				nullptr, &error), qPrintable(QStringLiteral("global signature %1/%2 was accepted")
					.arg(signature.numerator).arg(signature.denominator)));
			QVERIFY(!error.isEmpty());
			QVERIFY(!QFile::exists(file));
		}

		QString error;
		QVERIFY2(interchange::writeConductorTrack(file, TempoMap{}, 120,
			TempoMapTimeSignature{3, 4}, nullptr, &error), qPrintable(error));
		const interchange::SmfReadReport read = interchange::readConductorTrack(file);
		QVERIFY2(read.ok, qPrintable(read.error));
		QCOMPARE(read.events.size(), static_cast<std::size_t>(1));
		QCOMPARE(read.events.front().numerator, 3);
		QCOMPARE(read.events.front().denominator, 4);
	}

	//! A required tick-zero seed cannot make a full tempo map unreadable.
	void aTickZeroSeedDoesNotExceedTempoMapCapacity()
	{
		TempoMap map;
		map.setActive(true);
		for (int index = 0; index < TempoMap::MaxEvents; ++index)
		{
			TempoMapEvent event;
			event.tick = index + 1;
			event.hasTempo = true;
			event.tempo = 120;
			QVERIFY(map.addEvent(event));
		}
		QCOMPARE(map.size(), TempoMap::MaxEvents);

		const QString file = path(m_directory, QStringLiteral("full-map-seed.mid"));
		QString error;
		QVERIFY2(!interchange::writeConductorTrack(file, map, 120,
			TempoMapTimeSignature{4, 4}, nullptr, &error),
			"a full map plus its required tick-zero seed cannot be imported");
		QVERIFY(!error.isEmpty());
		QVERIFY(!QFile::exists(file));

		// A tick-zero event absorbs the seed, so a full map remains exportable.
		TempoMap mapWithTickZero;
		mapWithTickZero.setActive(true);
		for (int index = 0; index < TempoMap::MaxEvents; ++index)
		{
			TempoMapEvent event;
			event.tick = index;
			event.hasTempo = true;
			event.tempo = 120;
			QVERIFY(mapWithTickZero.addEvent(event));
		}
		const QString boundary = path(m_directory, QStringLiteral("full-map-tick-zero.mid"));
		QVERIFY2(interchange::writeConductorTrack(boundary, mapWithTickZero, 120,
			TempoMapTimeSignature{4, 4}, nullptr, &error), qPrintable(error));
		const interchange::SmfReadReport read = interchange::readConductorTrack(boundary);
		QVERIFY2(read.ok, qPrintable(read.error));
		TempoMap restored;
		QVERIFY2(interchange::mapFromEvents(read.events, &restored, &error), qPrintable(error));
		QCOMPARE(restored.size(), TempoMap::MaxEvents);
	}

	//! A map switched off is never consulted by the timeline: every tick answers
	//! the global tempo and metre. Its stored events were still written into the
	//! file, so a file another DAW read changed tempo at tick 200 in a session
	//! that does not - the file was a different performance of the same project.
	//! An inactive map exports the effective global values and nothing else.
	void anInactiveMapExportsOnlyWhatTheTimelineObeys()
	{
		TempoMap map;
		TempoMapEvent atZero;
		atZero.tick = 0;
		atZero.hasTempo = true;
		atZero.tempo = 140;
		atZero.hasTimeSignature = true;
		atZero.numerator = 4;
		atZero.denominator = 4;
		QVERIFY(map.addEvent(atZero));
		TempoMapEvent dormant = atZero;
		dormant.tick = 200;
		dormant.tempo = 90;
		QVERIFY(map.addEvent(dormant));
		map.setActive(false);

		// The same map ACTIVE is the control: both events are what the session
		// obeys, so both belong in the file.
		const std::vector<interchange::SmfInterchangeEvent> obeyed =
			interchange::exportEvents(map, 120, TempoMapTimeSignature{4, 4}, nullptr);
		QCOMPARE(static_cast<int>(obeyed.size()), 1);

		// The dormant map exports the seed alone: tick 0, and the GLOBAL tempo
		// and metre, because those are what every tick of it answers.
		QCOMPARE(obeyed.front().tick, static_cast<tick_t>(0));
		QCOMPARE(obeyed.front().tempo, 120);
		QCOMPARE(obeyed.front().hasTimeSignature, true);
		QCOMPARE(obeyed.front().numerator, 4);
		QCOMPARE(obeyed.front().denominator, 4);
		for (const interchange::SmfInterchangeEvent& event : obeyed)
		{
			QVERIFY2(event.tick == 0, "a dormant event reached the exported list");
		}

		// And the FILE, written while the map is still inactive, says the same
		// thing: one tempo event, the global one.
		const QString file = path(m_directory, QStringLiteral("inactive.mid"));
		QString error;
		QVERIFY2(interchange::writeConductorTrack(file, map, 120,
			TempoMapTimeSignature{4, 4}, nullptr, &error), qPrintable(error));
		const interchange::SmfReadReport read = interchange::readConductorTrack(file);
		QVERIFY2(read.ok, qPrintable(read.error));
		QCOMPARE(read.events.size(), static_cast<std::size_t>(1));
		QCOMPARE(read.events.front().tempo, 120);

		// Switched on, the same map exports what it now obeys: both events.
		map.setActive(true);
		const std::vector<interchange::SmfInterchangeEvent> active =
			interchange::exportEvents(map, 120, TempoMapTimeSignature{4, 4}, nullptr);
		QCOMPARE(static_cast<int>(active.size()), 2);
		QCOMPARE(active.back().tick, static_cast<tick_t>(200));
	}

private:
	QTemporaryDir m_directory;
};

QTEST_GUILESS_MAIN(SmfInterchangeTest)
#include "SmfInterchangeTest.moc"
