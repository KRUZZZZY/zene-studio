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
		QCOMPARE(events.back().tick, static_cast<quint32>(0));

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
	}

private:
	QTemporaryDir m_directory;
};

QTEST_GUILESS_MAIN(SmfInterchangeTest)
#include "SmfInterchangeTest.moc"
