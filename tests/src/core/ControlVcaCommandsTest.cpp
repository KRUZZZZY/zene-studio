/*
 * ControlVcaCommandsTest.cpp - the `vca.*` command group's SURFACE half (SPEC
 *                              A11-A16; the 0.3.0 ladder's OWNER-31 item 11,
 *                              "phase-locked multitrack edit groups").
 *
 * The group is proved by two registered suites, split because Gate 7 refuses a
 * NEW file over 500 lines (this one and its sibling were one file at 613): THIS
 * file holds the surface claims - the fourteen ids and their schemas, the A16
 * contract rows and their classes, the typed refusals every junk argument must
 * produce (the property the agent-surface sweep depends on), the fader's
 * measured effect (members scaled, no member's own fader written) and
 * membership's single-valuedness, each with the one-undo inverse. The EDIT half -
 * the phase-locked move across several tracks, the unlocked/skipped reporting,
 * the lock's refusals and the entity's own edit set in the mixer element - is
 * ControlVcaEditGroupsTest.cpp. The helpers both halves use, and the reason they
 * are shared rather than duplicated, are in ControlVcaTestSupport.h.
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
#include <QStringList>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVcaTestSupport.h"
#include "Engine.h"
#include "Mixer.h"
#include "Track.h"

using namespace lmms;
using namespace revtest;
using namespace vcatest;

class ControlVcaCommandsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		// The group commands need channels to assign; the default mix owns
		// master plus whatever the engine made, which is not a contract this
		// test should depend on.
		while (Engine::mixer() != nullptr && Engine::mixer()->numChannels() < 4)
		{
			Engine::mixer()->createChannel();
		}
		QVERIFY2(Engine::mixer() != nullptr && Engine::mixer()->numChannels() >= 4,
			"the test mix could not be given four channels");
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}
	// -- 1. the ids, the schemas, the contract rows -------------------------

	//! Every id of the group is registered, declares its group/verb, and
	//! declares BOTH schemas (an agent that cannot read the argument schema
	//! cannot call the command; one that cannot read the result schema cannot
	//! check it).
	void theGroupRegistersItsFourteenIds()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QCOMPARE(vcaIds().size(), 14);
		for (const QString& id : vcaIds())
		{
			const ControlCommand* cmd = registry->command(id);
			QVERIFY2(cmd != nullptr, qPrintable(id + " is not registered"));
			QCOMPARE(cmd->group, QStringLiteral("vca"));
			QCOMPARE(cmd->id, id);
			QVERIFY2(!cmd->verb.isEmpty(), qPrintable(id + " declares no verb"));
			QVERIFY2(!cmd->description.isEmpty(), qPrintable(id + " declares no description"));
			QVERIFY2(!cmd->argsSchema.isEmpty(), qPrintable(id + " declares no argument schema"));
			QVERIFY2(!cmd->resultSchema.isEmpty(), qPrintable(id + " declares no result schema"));
		}
		// `vca.list` and `vca.get_state` are the only reads; the other twelve
		// write.
		for (const QString& id : vcaIds())
		{
			const ControlCommand* cmd = registry->command(id);
			QCOMPARE(cmd->mutating, !vcaReads().contains(id));
		}
		evidence(QStringLiteral("ids=%1 registered=1 reads=%2 mutating=%3")
			.arg(vcaIds().size()).arg(vcaReads().size()).arg(vcaIds().size() - vcaReads().size()));
	}

	//! The A16 contract table has a row for every id, the class agrees with the
	//! behaviour (a read writes nothing; every writer here has an inverse), and
	//! every row states a non-empty reason and mechanism.
	void everyIdHasAContractRow()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		int trueInverse = 0;
		int notMutating = 0;
		for (const QString& id : vcaIds())
		{
			const control::ReversibilityEntry* row = table.lookup(id);
			QVERIFY2(row != nullptr, qPrintable(QStringLiteral("no contract row for %1").arg(id)));
			QVERIFY2(!row->reason.isEmpty(), qPrintable(id + " has an empty reason"));
			QVERIFY2(!row->mechanism.isEmpty(), qPrintable(id + " has an empty mechanism"));
			if (vcaReads().contains(id))
			{
				QCOMPARE(row->cls, control::ReversibilityClass::NotMutating);
				++notMutating;
				continue;
			}
			QCOMPARE(row->cls, control::ReversibilityClass::TrueInverse);
			QVERIFY2(row->reversible, qPrintable(id + " claims an inverse but is not reversible"));
			++trueInverse;
		}
		QCOMPARE(trueInverse, 12);
		QCOMPARE(notMutating, 2);
		evidence(QStringLiteral("contract_rows=%1 true_inverse=%2 not_mutating=%3")
			.arg(vcaIds().size()).arg(trueInverse).arg(notMutating));
	}

	// -- 2. junk arguments are TYPED refusals ------------------------------

	//! The property the agent-surface sweep depends on: every handler survives
	//! an empty, malformed or absent argument with a typed error and touches
	//! nothing. `vca.get_state` on a malformed id is invalid_args, on a
	//! well-formed unknown id not_found - never a silent success.
	void junkArgumentsAreTypedRefusals()
	{
		const QVector<QPair<QString, QJsonObject>> junk = {
			{QStringLiteral("vca.get_state"), QJsonObject{}},
			{QStringLiteral("vca.get_state"), {{QStringLiteral("group"), QString()}}},
			{QStringLiteral("vca.get_state"), {{QStringLiteral("group"), 0}}},
			{QStringLiteral("vca.get_state"), {{QStringLiteral("group"), QStringLiteral("vca-9999")}}},
			{QStringLiteral("vca.get_state"), {{QStringLiteral("group"), QStringLiteral("ch-0")}}},
			{QStringLiteral("vca.remove"), {{QStringLiteral("group"), QStringLiteral("nonsense")}}},
			{QStringLiteral("vca.rename"), {{QStringLiteral("group"), QStringLiteral("vca-9999")},
				{QStringLiteral("name"), QStringLiteral("x")}}},
			{QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), QStringLiteral("vca-9999")},
				{QStringLiteral("gain"), 1.0}}},
			{QStringLiteral("vca.assign"), {{QStringLiteral("group"), QStringLiteral("vca-9999")},
				{QStringLiteral("channel"), QStringLiteral("ch-1")}}},
			{QStringLiteral("vca.track_add"), {{QStringLiteral("group"), QStringLiteral("vca-9999")},
				{QStringLiteral("track"), QStringLiteral("trk-0")}}},
			{QStringLiteral("vca.edit_move"), {{QStringLiteral("group"), QStringLiteral("vca-9999")},
				{QStringLiteral("clip"), QStringLiteral("clip-0")},
				{QStringLiteral("position"), 0}}},
		};
		for (const auto& pair : junk)
		{
			const ControlResult result = run(pair.first, pair.second);
			QVERIFY2(!result.ok,
				qPrintable(QStringLiteral("%1 accepted junk %2")
					.arg(pair.first, QString::fromUtf8(QJsonDocument(pair.second)
						.toJson(QJsonDocument::Compact)))));
			QVERIFY2(result.errorKind == ControlErrorKind::InvalidArgs
					|| result.errorKind == ControlErrorKind::NotFound,
				qPrintable(QStringLiteral("%1 refused junk with %2, not invalid_args/not_found: %3")
					.arg(pair.first, controlErrorKindName(result.errorKind), result.errorMessage)));
			QVERIFY2(!result.errorMessage.isEmpty(),
				qPrintable(pair.first + " refused junk with no message"));
		}
		// Nothing above created anything.
		QCOMPARE(Engine::mixer()->vcaGroups().size(), static_cast<std::size_t>(0));
		evidence(QStringLiteral("junk_cases=%1 all_typed=1 groups_created=0").arg(junk.size()));
	}

	// -- 3. the measured effect --------------------------------------------

	//! The fader SCALES: the members' published gain follows the group, and not
	//! one member's own fader model is written - which is the whole reason the
	//! move is exactly reversible (the register's "easy to get wrong").
	void theFaderScalesMembersWithoutWritingTheirOwnFaders()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		const int channels = static_cast<int>(mixer->numChannels());
		QVERIFY2(channels >= 3, "the test mix has fewer than three channels");

		const QString group = createGroup();
		QVERIFY2(!group.isEmpty(), "vca.create failed");
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), group},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), group},
			{QStringLiteral("channel"), channelId(2)}}).ok);

		const float memberBefore = mixer->mixerChannel(1)->m_volumeModel.value();
		const ControlResult moved = run(QStringLiteral("vca.set_gain"),
			{{QStringLiteral("group"), group}, {QStringLiteral("gain"), 0.5}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("gain")).toDouble(), 0.5);

		QCOMPARE(memberGain(group, channelId(1)), 0.5);
		QCOMPARE(memberGain(group, channelId(2)), 0.5);
		QVERIFY2(mixer->mixerChannel(1)->m_volumeModel.value() == memberBefore,
			"the group wrote a member's own fader model");
		// A channel that is not in the group publishes unity.
		QCOMPARE(static_cast<double>(mixer->mixerChannel(3)->vcaGain()), 1.0);

		// SPEC A16: the group fader's own checkpoint is the inverse.
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result.value(QStringLiteral("volume")).toDouble(),
			1.0);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 1.0);
		QVERIFY(mixer->mixerChannel(1)->m_volumeModel.value() == memberBefore);
		evidence(QStringLiteral("fader=0.5 member_gain=0.5 member_fader_written=0 undo=1"));
	}

	//! A channel is in at most one group, master is in none, and a channel that
	//! is not a member cannot be unassigned - each as its own typed refusal,
	//! because a silent success would hide a wrong id.
	void membershipIsSingleValuedAndTyped()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		const QString first = createGroup(QStringLiteral("First"));
		const QString second = createGroup(QStringLiteral("Second"));
		QVERIFY(!first.isEmpty());
		QVERIFY(!second.isEmpty());
		QVERIFY(first != second);

		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), first},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		const ControlResult twice = run(QStringLiteral("vca.assign"),
			{{QStringLiteral("group"), first}, {QStringLiteral("channel"), channelId(1)}});
		QVERIFY2(!twice.ok, "assigning a channel to the group it is already in succeeded");
		QCOMPARE(twice.errorKind, ControlErrorKind::Refused);

		const ControlResult elsewhere = run(QStringLiteral("vca.assign"),
			{{QStringLiteral("group"), second}, {QStringLiteral("channel"), channelId(1)}});
		QVERIFY2(!elsewhere.ok, "a channel was allowed into two groups at once");
		QCOMPARE(elsewhere.errorKind, ControlErrorKind::Refused);
		QVERIFY2(elsewhere.errorMessage.contains(first),
			qPrintable("the refusal does not name the holding group: " + elsewhere.errorMessage));

		const ControlResult master = run(QStringLiteral("vca.assign"),
			{{QStringLiteral("group"), first}, {QStringLiteral("channel"), channelId(0)}});
		QVERIFY2(!master.ok, "ch-0 (master) was accepted as a group member");
		QCOMPARE(master.errorKind, ControlErrorKind::Refused);

		const ControlResult ghost = run(QStringLiteral("vca.unassign"),
			{{QStringLiteral("group"), second}, {QStringLiteral("channel"), channelId(2)}});
		QVERIFY2(!ghost.ok, "unassigning a channel that is not a member succeeded");
		QCOMPARE(ghost.errorKind, ControlErrorKind::Refused);

		// The membership round trip is ONE undo step each way.
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), first}}).result.value(QStringLiteral("member_count")).toInt(),
			0);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 1.0);
		evidence(QStringLiteral("double_assign_refused=1 cross_group_refused=1 master_refused=1 "
			"ghost_unassign_refused=1 undo_membership=1"));
	}

	//! `vca.remove` is EXACTLY reversible: the id, name, fader, mute, solo,
	//! phase lock, member channels and edit tracks all come back, and the
	//! members publish the group's gain again.
	void removeIsExactlyReversible()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		const QString track = addTrack(QStringLiteral("instrument"));
		QVERIFY2(!track.isEmpty(), "track.add failed");

		const QString group = createGroup(QStringLiteral("Bus"));
		QVERIFY(!group.isEmpty());
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), group},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		QVERIFY(run(QStringLiteral("vca.track_add"), {{QStringLiteral("group"), group},
			{QStringLiteral("track"), track}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"),
			{{QStringLiteral("group"), group}, {QStringLiteral("gain"), 0.75}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_mute"),
			{{QStringLiteral("group"), group}, {QStringLiteral("muted"), true}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_phase_lock"),
			{{QStringLiteral("group"), group}, {QStringLiteral("locked"), false}}).ok);

		const QJsonObject before = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result;
		const ControlResult removed = run(QStringLiteral("vca.remove"),
			{{QStringLiteral("group"), group}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 1.0);

		REV_UNDO_OR_FAIL();
		const QJsonObject after = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result;
		QCOMPARE(after.value(QStringLiteral("name")).toString(), QStringLiteral("Bus"));
		QCOMPARE(after.value(QStringLiteral("volume")).toDouble(), 0.75);
		QCOMPARE(after.value(QStringLiteral("muted")).toBool(), true);
		QCOMPARE(after.value(QStringLiteral("phase_locked")).toBool(), false);
		QCOMPARE(after.value(QStringLiteral("member_count")).toInt(), 1);
		QCOMPARE(after.value(QStringLiteral("track_count")).toInt(), 1);
		QCOMPARE(after.value(QStringLiteral("tracks")).toArray().at(0).toString(), track);
		// The published gain is re-derived, so the audio path is restored too:
		// the group is muted, which is a published gain of zero.
		QCOMPARE(before.size(), after.size());
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 0.0);
		evidence(QStringLiteral("removed=1 undo_restored_fields=%1 published_gain=0")
			.arg(after.size()));
	}
};

QTEST_GUILESS_MAIN(ControlVcaCommandsTest)
#include "ControlVcaCommandsTest.moc"
