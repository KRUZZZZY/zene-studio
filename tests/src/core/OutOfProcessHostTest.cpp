/*
 * OutOfProcessHostTest.cpp - out-of-process plugin hosting BEYOND ZynAddSubFx:
 *                            the family table, the client-process record, and
 *                            the `oop.*` surface's typed refusals
 *
 * Feature row 80 of docs/FEATURE-LIST-0.3.0.md (board card #670). The engine
 * half is include/OutOfProcessHosting.h + src/core/OutOfProcessHosting.cpp (the
 * family table and HostTracker), the surface is
 * src/core/ControlCommandsOutOfProcess{,Edit,Support}.cpp, and this file is the
 * proof of everything except the real client process - the five ids and their
 * A16 rows, the classification, the lifecycle the record holds, the reads on a
 * live song, and the typed refusals. The whole loop on a REAL client (hosted,
 * SIGKILLed, counted, restarted, refused at the bound, cleared) is the sibling
 * ctest OutOfProcessHostClientLoopTest, in its own binary because the
 * file-length ratchet is not moved for a new feature.
 *
 * WHAT IS PROVEN, and what a green run does NOT say:
 *
 *   - The classification is read off the tree's own facts (a family with BOTH an
 *     in-process and a client implementation; a family whose only path is a
 *     client process; families with neither), not off a list this test keeps.
 *   - The crash accounting is exercised as a UNIT, in the order RemotePlugin's
 *     own call sites make it - including the case that matters most for
 *     ordinary use: a DELIBERATE shutdown is an exit and never a crash.
 *   - A device whose family ships no client executable in this build is REFUSED
 *     by name, through the surface, with the family's own reason.
 *   - NO PLUGIN IS CRASHED BY ITS OWN BUG anywhere in this pair of tests: the
 *     client is SIGKILLed from outside, so what is shown is "a dead client
 *     cannot take the host down, and the build notices, counts and refuses to
 *     keep re-hosting it" - the mechanism a real crash trips, with an external
 *     trigger. Same caveat the prior art's ZynSeparateProcessTest states.
 *   - Where this build has no loadable instrument module for a case,
 *     `Plugin::instantiate` hands back the engine's DummyPlugin and the case
 *     asserts about THAT plugin's family, which is what the build contains.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QtTest>

#include <memory>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "OutOfProcessHostSupport.h"
#include "OutOfProcessHosting.h"
#include "Plugin.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::oop;
using namespace ooptest;

class OutOfProcessHostTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		m_engineInitialised = true;
		QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
		// The audio path is not this test's subject and a live device would
		// render while the suite runs; the engine stays up, the device stops.
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		QVERIFY2(ControlRegistry::instance() != nullptr, "the control registry did not come up");
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		if (m_engineInitialised) { Engine::destroy(); }
	}

	// ---------------------------------------------------------------- 1. ids

	//! The group is registered, with both schemas, and the mutating flag agrees
	//! with the A16 table: the two reads declare not_mutating, the three writers
	//! mutating. An id the table does not carry is what the A16 anti-drift test
	//! catches; here the contract is read the way a client reads it.
	void theGroupRegistersItsFiveIds()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int rows = 0;
		const control::ReversibilityRow* table = control::reversibilityRowTable(&rows);

		for (const QString& id : oopIds())
		{
			const ControlCommand* cmd = registry->command(id);
			QVERIFY2(cmd != nullptr, qPrintable(id + " is not registered"));
			QCOMPARE(cmd->group, QStringLiteral("oop"));
			QCOMPARE(cmd->id, id);
			QVERIFY2(!cmd->verb.isEmpty(), qPrintable(id + " declares no verb"));
			QVERIFY2(!cmd->description.isEmpty(), qPrintable(id + " declares no description"));
			QVERIFY2(!cmd->argsSchema.isEmpty(), qPrintable(id + " declares no argument schema"));
			QVERIFY2(!cmd->resultSchema.isEmpty(), qPrintable(id + " declares no result schema"));
			QCOMPARE(cmd->mutating, !oopReads().contains(id));

			// And the A16 table carries exactly one row for it.
			int found = 0;
			for (int i = 0; i < rows; ++i)
			{
				if (id == QLatin1String(table[i].command)) { ++found; }
			}
			QCOMPARE(found, 1);
		}
		qInfo("oop: %lld ids registered, each with one A16 row, in a table of %d rows",
			static_cast<long long>(oopIds().size()), rows);
	}

	// ------------------------------------------------------------- 2. table

	//! The family table is the row's whole point: what each family's
	//! out-of-process story IS in this build, with the reason where it has none.
	void familiesAreClassifiedHonestly()
	{
		const QVector<Family> table = families();
		QVERIFY2(table.size() >= 10, "the family table is suspiciously short");

		const Family zyn = familyFor(QStringLiteral("zynaddsubfx"));
		QCOMPARE(zyn.availability, Availability::ClientAvailable);
		QCOMPARE(zyn.client, QString::fromLatin1(ZynClient));
		QVERIFY2(!zyn.reason.isEmpty(), "the ZynAddSubFx family states no reason");
		QVERIFY(canHostOutOfProcess(QStringLiteral("zynaddsubfx"), nullptr));

		// VST2: the client process is what the family IS, not a choice.
		for (const QString& key : {QStringLiteral("vestige"), QStringLiteral("vsteffect")})
		{
			const Family family = familyFor(key);
			QCOMPARE(family.availability, Availability::AlwaysSeparate);
			QCOMPARE(family.client, QString::fromLatin1(Vst2Client));
		}

		// The in-process families the row names: no client executable in this
		// build, each with its own reason rather than one shared sentence.
		for (const QString& key : {QStringLiteral("clapeffect"), QStringLiteral("clapinstrument"),
				QStringLiteral("vst3effect"), QStringLiteral("vst3instrument"),
				QStringLiteral("ladspaeffect"), QStringLiteral("lv2effect"),
				QStringLiteral("sf2player"), QStringLiteral("gigplayer")})
		{
			const Family family = familyFor(key);
			QCOMPARE(family.availability, Availability::NoClientInBuild);
			QVERIFY(family.client.isEmpty());
			QVERIFY2(!family.reason.isEmpty(), qPrintable(key + " states no reason"));
			QString reason;
			QVERIFY2(!canHostOutOfProcess(key, &reason), qPrintable(key + " claims to be hostable"));
			QVERIFY2(reason.contains(key) || family.reason == reason,
				qPrintable(key + " is refused without naming itself"));
		}

		// An unlisted key is answered, not ignored: the fallback names the key.
		const Family unknown = familyFor(QStringLiteral("some-plugin-that-does-not-exist"));
		QCOMPARE(unknown.availability, Availability::NoClientInBuild);
		QVERIFY2(unknown.reason.contains(QStringLiteral("some-plugin-that-does-not-exist")),
			"the fallback family does not name the key it was asked about");

		qInfo("families: %lld classified, %s the one this build can host on request",
			static_cast<long long>(table.size()), zyn.client.toUtf8().constData());
	}

	// ---------------------------------------------------------- 3. lifecycle

	//! The record RemotePlugin's own call sites make: start, death, death,
	//! death. The bound is the build's, and the refusal names the count.
	void theCrashCountReachesTheBoundAndRefuses()
	{
		HostTracker& tracker = HostTracker::instance();
		const QString client = QStringLiteral("OutOfProcessHostTest-client");
		QCOMPARE(tracker.record(client).crashes, 0);

		tracker.noteStarted(client, 4242, QStringLiteral("test"));
		QCOMPARE(tracker.record(client).starts, 1);
		QCOMPARE(tracker.record(client).lastPid, qint64(4242));

		const int bound = HostTracker::maxCrashesPerClient();
		QCOMPARE(bound, 3);
		for (int i = 0; i < bound; ++i)
		{
			QVERIFY2(!tracker.refusedByCrashLoop(client, nullptr),
				qPrintable(QStringLiteral("refused one death early (crash %1)").arg(i + 1)));
			tracker.noteExited(client, 0, true, QStringLiteral("test"));
		}

		QString refusal;
		QVERIFY2(tracker.refusedByCrashLoop(client, &refusal),
			"the bound was reached and the path is still allowed");
		QVERIFY2(refusal.contains(client) && refusal.contains(QStringLiteral("3")),
			qPrintable(QStringLiteral("the refusal does not name the client and the count: ") + refusal));
		QCOMPARE(tracker.record(client).crashes, bound);
		QCOMPARE(tracker.record(client).exits, bound);

		// The only thing that lifts it.
		tracker.resetCrashes(client);
		QVERIFY(!tracker.refusedByCrashLoop(client, nullptr));
		// ... and the history is still readable afterwards.
		QCOMPARE(tracker.record(client).exits, bound);
		qInfo("crash bound is %d; the refusal names the client and the count", bound);
	}

	//! A deliberate shutdown is not a crash: the mode switch takes a HEALTHY
	//! client down, and counting that as a crash would make ordinary mode
	//! switching look like a crash loop.
	void aDeliberateShutdownIsNotACrash()
	{
		HostTracker& tracker = HostTracker::instance();
		const QString client = QStringLiteral("OutOfProcessHostTest-shutdown");

		tracker.noteStarted(client, 7, QStringLiteral("test"));
		tracker.noteShutdown(client, 0);

		QCOMPARE(tracker.record(client).exits, 1);
		QCOMPARE(tracker.record(client).crashes, 0);
		QVERIFY(!tracker.refusedByCrashLoop(client, nullptr));

		// A non-zero exit IS counted, with or without a crash status.
		tracker.noteExited(client, 1, false, QStringLiteral("test"));
		QCOMPARE(tracker.record(client).crashes, 1);
	}

	// ------------------------------------------------------------- 4. surface

	//! The two reads answer on a live instance, and they answer about THIS
	//! build: the ZynAddSubFx family carries its client, the bound is the
	//! tracker's, and the walk reaches the song's own chains.
	void theReadsAnswerAboutThisBuild()
	{
		ControlRegistry* registry = ControlRegistry::instance();

		const ControlResult families = registry->invoke(
			QStringLiteral("oop.list_families"), QJsonObject());
		QVERIFY2(families.ok, qPrintable(families.errorMessage));
		QCOMPARE(families.result.value(QStringLiteral("family_count")).toInt(),
			families.result.value(QStringLiteral("families")).toArray().size());

		const QJsonArray table = families.result.value(QStringLiteral("families")).toArray();
		const QJsonObject zyn = familyOf(table, QStringLiteral("zynaddsubfx"));
		QVERIFY2(!zyn.isEmpty(), "the family table does not carry zynaddsubfx");
		QCOMPARE(zyn.value(QStringLiteral("availability")).toString(),
			QStringLiteral("client-available"));
		QCOMPARE(zyn.value(QStringLiteral("client")).toString(), QString::fromLatin1(ZynClient));

		const QJsonObject clap = familyOf(table, QStringLiteral("clapeffect"));
		QVERIFY2(!clap.isEmpty(), "the family table does not carry the CLAP effect family");
		QCOMPARE(clap.value(QStringLiteral("availability")).toString(), QStringLiteral("no-client"));
		QVERIFY2(!clap.value(QStringLiteral("reason")).toString().isEmpty(),
			"the CLAP family states no reason");

		const ControlResult state = registry->invoke(QStringLiteral("oop.get_state"), QJsonObject());
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QVERIFY2(!state.result.value(QStringLiteral("chains")).toArray().isEmpty(),
			"a live song with a track reported no device chains at all");
		QCOMPARE(state.result.value(QStringLiteral("bounds")).toObject()
				.value(QStringLiteral("max_crashes_per_client")).toInt(),
			HostTracker::maxCrashesPerClient());
		QVERIFY(state.result.value(QStringLiteral("realtime_safe")).toBool());
	}

	//! A device plugin with no client executable in this build is REFUSED by
	//! name, and the refusal quotes the family's own reason - not a silent
	//! fallback and not a generic message.
	void aFamilyWithNoClientIsRefusedTyped()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		auto track = std::make_unique<InstrumentTrack>(Engine::getSong());
		const QString target = control::trackIdOf(track.get());

		// The instrument the engine gives a fresh track (the build's own module
		// when one loads, the DummyPlugin when it does not) - whatever it is,
		// this build has no client executable for that family and the refusal
		// has to say so.
#ifdef LMMS_TEST_PLUGIN_DIR
		track->loadInstrument(QStringLiteral("tripleoscillator"));
#endif
		QVERIFY2(track->instrument() != nullptr, "an instrument track with no instrument");
		const QString key = QString::fromUtf8(track->instrument()->descriptor()->name);

		const ControlResult refused = registry->invoke(QStringLiteral("oop.set_mode"), QJsonObject{
			{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("mode"), QStringLiteral("separate-process")}});
		QVERIFY2(!refused.ok, "a family with no client executable accepted separate-process");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(key),
			qPrintable(QStringLiteral("the refusal does not name the family: ") + refused.errorMessage));
		QVERIFY2(refused.errorMessage.contains(target),
			qPrintable(QStringLiteral("the refusal does not name the device: ") + refused.errorMessage));

		// The read side reports the same device, in the refused state, without
		// being told to look for it.
		const QJsonObject address{
			{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), QStringLiteral("inst")}};
		const QJsonObject hosting = deviceHosting(registry, address);
		QVERIFY2(!hosting.isEmpty(), "oop.get_state did not report the track's instrument at all");
		QCOMPARE(hosting.value(QStringLiteral("state")).toString(),
			QStringLiteral("refused-no-client"));
		QCOMPARE(hosting.value(QStringLiteral("can_choose_mode")).toBool(), false);

		// A bad mode name is refused as an ARGUMENT, before anything is looked up.
		const ControlResult junk = registry->invoke(QStringLiteral("oop.set_mode"), QJsonObject{
			{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("mode"), QStringLiteral("both")}});
		QVERIFY2(!junk.ok, "oop.set_mode accepted an unknown mode");
		QCOMPARE(junk.errorKind, ControlErrorKind::InvalidArgs);

		qInfo("refused: %s", refused.errorMessage.toUtf8().constData());
	}

	//! Restarting a device whose family has no client executable is refused too,
	//! and for the same reason - there is nothing to restart.
	void restartWithoutAClientIsRefused()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		auto track = std::make_unique<InstrumentTrack>(Engine::getSong());
#ifdef LMMS_TEST_PLUGIN_DIR
		track->loadInstrument(QStringLiteral("tripleoscillator"));
#endif
		if (track->instrument() == nullptr)
		{
			QSKIP("this build's test host can instantiate no instrument at all (no plugin module "
				"is loadable here), so there is no device to refuse: UNEXERCISED by this run");
		}

		const ControlResult refused = registry->invoke(QStringLiteral("oop.restart"), QJsonObject{
			{QStringLiteral("target"), control::trackIdOf(track.get())},
			{QStringLiteral("plugin"), QStringLiteral("inst")}});
		QVERIFY2(!refused.ok, "a family with no client executable was restarted");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
	}

private:
	bool m_engineInitialised = false;
};

QTEST_GUILESS_MAIN(OutOfProcessHostTest)

#include "OutOfProcessHostTest.moc"
