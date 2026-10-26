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

#include <cmath>
#include <limits>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVcaTestSupport.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
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

	//! The command must trust the engine mutation result before claiming a
	//! successful transaction. A detached live group makes addMember refuse
	//! after the argument resolver has accepted an `also:true` request.
	void failedAssignmentMutationLeavesNoUndoOrTransaction()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(mixer != nullptr);
		QVERIFY(journal != nullptr);

		const QString first = createGroup(QStringLiteral("First member"));
		const QString second = createGroup(QStringLiteral("Shared member"));
		QVERIFY(!first.isEmpty());
		QVERIFY(!second.isEmpty());
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), first},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), first},
			{QStringLiteral("gain"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_gain"), {{QStringLiteral("group"), second},
			{QStringLiteral("gain"), 0.25}}).ok);

		const int depthBeforeCrossGroup = journal->undoDepth();
		const int transactionsBeforeCrossGroup = run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size();
		const ControlResult crossGroup = run(QStringLiteral("vca.assign"),
			{{QStringLiteral("group"), second}, {QStringLiteral("channel"), channelId(1)}});
		QVERIFY2(!crossGroup.ok, "default cross-group assign reported success");
		QCOMPARE(crossGroup.errorKind, ControlErrorKind::Refused);
		QVERIFY(crossGroup.errorMessage.contains(first));
		QCOMPARE(journal->undoDepth(), depthBeforeCrossGroup);
		QCOMPARE(run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size(), transactionsBeforeCrossGroup);
		QCOMPARE(memberGain(second, channelId(1)), -1.0);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 0.5);

		const ControlResult shared = run(QStringLiteral("vca.assign"),
			{{QStringLiteral("group"), second}, {QStringLiteral("channel"), channelId(1)},
				{QStringLiteral("also"), true}});
		QVERIFY2(shared.ok, qPrintable(shared.errorMessage));
		QCOMPARE(run(QStringLiteral("vca.get_state"), {{QStringLiteral("group"), first}})
			.result.value(QStringLiteral("member_count")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("vca.get_state"), {{QStringLiteral("group"), second}})
			.result.value(QStringLiteral("member_count")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("vca.get_state"), {{QStringLiteral("group"), first}})
			.result.value(QStringLiteral("volume")).toDouble(), 0.5);
		QCOMPARE(run(QStringLiteral("vca.get_state"), {{QStringLiteral("group"), second}})
			.result.value(QStringLiteral("volume")).toDouble(), 0.25);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 0.125);
		REV_UNDO_OR_FAIL();
		QCOMPARE(memberGain(second, channelId(1)), -1.0);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(1)->vcaGain()), 0.5);

		const QString detachedId = createGroup(QStringLiteral("Detached target"));
		QVERIFY(!detachedId.isEmpty());
		const QString detachedWireId = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), detachedId}}).result.value(QStringLiteral("id")).toString();
		const int detachedGroupId = detachedWireId.mid(QStringLiteral("vca-").size()).toInt();
		VcaGroup* detached = mixer->vcaGroup(detachedGroupId);
		QVERIFY(detached != nullptr);
		detached->detach();
		const int depthBeforeFailure = journal->undoDepth();
		const int transactionsBeforeFailure = run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size();
		const ControlResult failed = run(QStringLiteral("vca.assign"),
			{{QStringLiteral("group"), detachedId}, {QStringLiteral("channel"), channelId(2)},
				{QStringLiteral("also"), true}});
		QVERIFY2(!failed.ok, "a refused VcaGroup::addMember mutation reported success");
		QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
		QCOMPARE(journal->undoDepth(), depthBeforeFailure);
		QCOMPARE(run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size(), transactionsBeforeFailure);
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), detachedId}}).result.value(QStringLiteral("member_count")).toInt(), 0);
		QCOMPARE(static_cast<double>(mixer->mixerChannel(2)->vcaGain()), 1.0);
		evidence(QStringLiteral("cross_group_refused=1 also_shared_gain=0.125 add_member_refusal=1 "
			"undo_depth_unchanged=1 transaction_unchanged=1"));
	}

	//! A transaction inverse must restore every exclusive solo flag and the
	//! channel mutes it displaced, even after the bounded journal evicts it.
	void replayingSoloInverseRestoresPreviousExclusiveSolo()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(mixer != nullptr);
		QVERIFY(journal != nullptr);
		const QString first = createGroup(QStringLiteral("Solo A"));
		const QString second = createGroup(QStringLiteral("Solo B"));
		QVERIFY(!first.isEmpty());
		QVERIFY(!second.isEmpty());
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), first},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), second},
			{QStringLiteral("channel"), channelId(2)}}).ok);
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			mixer->mixerChannel(ch)->m_muteModel.setValue(ch == 3);
		}
		QVector<bool> originalMutes;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			originalMutes.append(mixer->mixerChannel(ch)->m_muteModel.value());
		}
		QVERIFY(run(QStringLiteral("vca.set_solo"),
			{{QStringLiteral("group"), first}, {QStringLiteral("soloed"), true}}).ok);
		QVector<bool> beforeSecondSoloMutes;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			beforeSecondSoloMutes.append(mixer->mixerChannel(ch)->m_muteModel.value());
		}

		const int oldCap = journal->maxUndoStates();
		QVERIFY(journal->setMaxUndoStates(1));
		const int evictedBefore = journal->evictedSteps();
		const ControlResult secondSolo = run(QStringLiteral("vca.set_solo"),
			{{QStringLiteral("group"), second}, {QStringLiteral("soloed"), true}});
		QVERIFY2(secondSolo.ok, qPrintable(secondSolo.errorMessage));
		const QJsonObject inverse = stateOf(QStringLiteral("vca.set_solo"))
			.value(QStringLiteral("inverse")).toObject();
		QCOMPARE(journal->undoDepth(), 1);
		QVERIFY(run(QStringLiteral("vca.set_gain"),
			{{QStringLiteral("group"), second}, {QStringLiteral("gain"), 0.75}}).ok);
		QVERIFY(journal->evictedSteps() > evictedBefore);
		const ControlResult restored = run(inverse.value(QStringLiteral("op")).toString(),
			inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		const bool firstRestored = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), first}}).result.value(QStringLiteral("soloed")).toBool();
		const bool secondCleared = !run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), second}}).result.value(QStringLiteral("soloed")).toBool();
		QVector<bool> restoredMutes;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			restoredMutes.append(mixer->mixerChannel(ch)->m_muteModel.value());
		}
		const ControlResult unsoloed = run(QStringLiteral("vca.set_solo"),
			{{QStringLiteral("group"), first}, {QStringLiteral("soloed"), false}});
		QVERIFY2(unsoloed.ok, qPrintable(unsoloed.errorMessage));
		QVector<bool> finalMutes;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			finalMutes.append(mixer->mixerChannel(ch)->m_muteModel.value());
		}
		QVERIFY(journal->setMaxUndoStates(oldCap));

		QVERIFY2(firstRestored, "replaying B's inverse left A unsoloed");
		QVERIFY(secondCleared);
		QCOMPARE(restoredMutes, beforeSecondSoloMutes);
		QCOMPARE(finalMutes, originalMutes);
		evidence(QStringLiteral("solo_inverse_restored_prior_group=1 channel_mutes=1 "
			"unsolo_restored_original_mutes=1 journal_evicted=1"));
	}

	//! The pre-solo cache can be stale while no solo is active: a later manual
	//! mute edit changes the model but does not rewrite that transient cache.
	void soloUndoRestoresPreSoloMuteCache()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(mixer != nullptr);
		QVERIFY(journal != nullptr);
		const QString group = createGroup(QStringLiteral("Cache restore"));
		QVERIFY(!group.isEmpty());
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), group},
			{QStringLiteral("channel"), channelId(1)}}).ok);

		const bool wasJournalling = journal->isJournalling();
		journal->setJournalling(false);
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			mixer->mixerChannel(ch)->m_muteModel.setValue(ch == 3);
		}
		journal->setJournalling(wasJournalling);
		QVERIFY(run(QStringLiteral("vca.set_solo"),
			{{QStringLiteral("group"), group}, {QStringLiteral("soloed"), true}}).ok);
		QVERIFY(run(QStringLiteral("vca.set_solo"),
			{{QStringLiteral("group"), group}, {QStringLiteral("soloed"), false}}).ok);

		// A user mute edit while solo is inactive changes the models but leaves
		// the previous cache available for the command's undo checkpoint.
		journal->setJournalling(false);
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			mixer->mixerChannel(ch)->m_muteModel.setValue(ch == 2);
		}
		journal->setJournalling(wasJournalling);
		QVector<bool> cacheBefore;
		QVector<bool> changedMutes;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			cacheBefore.append(mixer->mixerChannel(ch)->m_muteBeforeSolo);
			changedMutes.append(mixer->mixerChannel(ch)->m_muteModel.value());
		}
		QVERIFY(run(QStringLiteral("vca.set_solo"),
			{{QStringLiteral("group"), group}, {QStringLiteral("soloed"), true}}).ok);
		QVector<bool> cacheAfterSolo;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			cacheAfterSolo.append(mixer->mixerChannel(ch)->m_muteBeforeSolo);
		}
		QVERIFY(cacheAfterSolo != cacheBefore);
		REV_UNDO_OR_FAIL();

		QVector<bool> cacheAfterUndo;
		QVector<bool> mutesAfterUndo;
		for (int ch = 1; ch < static_cast<int>(mixer->numChannels()); ++ch)
		{
			cacheAfterUndo.append(mixer->mixerChannel(ch)->m_muteBeforeSolo);
			mutesAfterUndo.append(mixer->mixerChannel(ch)->m_muteModel.value());
		}
		QCOMPARE(cacheAfterUndo, cacheBefore);
		QCOMPARE(mutesAfterUndo, changedMutes);
		evidence(QStringLiteral("solo_undo_restored_mute_models=1 pre_solo_cache=1"));
	}

	//! A deleted track keeps its stable id in the group's edit set so callers
	//! can see and repair the missing membership. Removing that id must not need
	//! a live Track object, and its undo restores the same missing membership.
	void staleTrackMembershipCanBeRemoved()
	{
		clearGroups();
		const QString track = addTrack();
		const QString group = createGroup(QStringLiteral("Deleted track"));
		QVERIFY(!track.isEmpty());
		QVERIFY(!group.isEmpty());
		QVERIFY(run(QStringLiteral("vca.track_add"),
			{{QStringLiteral("group"), group}, {QStringLiteral("track"), track}}).ok);

		const ControlResult deleted = run(QStringLiteral("track.remove"),
			{{QStringLiteral("track"), track}});
		QVERIFY2(deleted.ok, qPrintable(deleted.errorMessage));
		const QJsonObject stale = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result;
		QCOMPARE(stale.value(QStringLiteral("tracks")).toArray().size(), 1);
		QCOMPARE(stale.value(QStringLiteral("tracks")).toArray().at(0).toString(), track);
		QCOMPARE(stale.value(QStringLiteral("missing_tracks")).toArray().size(), 1);

		const ControlResult removed = run(QStringLiteral("vca.track_remove"),
			{{QStringLiteral("group"), group}, {QStringLiteral("track"), track}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("removed")).toString(), track);
		QCOMPARE(removed.result.value(QStringLiteral("tracks")).toArray().size(), 0);
		QCOMPARE(removed.result.value(QStringLiteral("missing_tracks")).toArray().size(), 0);

		REV_UNDO_OR_FAIL();
		const QJsonObject restored = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result;
		QCOMPARE(restored.value(QStringLiteral("tracks")).toArray().size(), 1);
		QCOMPARE(restored.value(QStringLiteral("tracks")).toArray().at(0).toString(), track);
		QCOMPARE(restored.value(QStringLiteral("missing_tracks")).toArray().size(), 1);

		// The inverse published in the transaction stream is also a replay
		// promise. A live-track command cannot restore this dead id; replay the
		// reported operation through the same registry clients use.
		const ControlResult removedAgain = run(QStringLiteral("vca.track_remove"),
			{{QStringLiteral("group"), group}, {QStringLiteral("track"), track}});
		QVERIFY2(removedAgain.ok, qPrintable(removedAgain.errorMessage));
		const QJsonArray transactions = run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray();
		QVERIFY(!transactions.isEmpty());
		const QJsonObject inverse = transactions.last().toObject()
			.value(QStringLiteral("inverse")).toObject();
		const ControlResult replayed = run(inverse.value(QStringLiteral("op")).toString(),
			inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replayed.ok, qPrintable(QStringLiteral("reported inverse failed: %1 -> %2")
			.arg(inverse.value(QStringLiteral("op")).toString(), replayed.errorMessage)));
		const QJsonObject replayRestored = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result;
		QCOMPARE(replayRestored.value(QStringLiteral("tracks")).toArray().size(), 1);
		QCOMPARE(replayRestored.value(QStringLiteral("tracks")).toArray().at(0).toString(), track);
		QCOMPARE(replayRestored.value(QStringLiteral("missing_tracks")).toArray().size(), 1);
		evidence(QStringLiteral("deleted_track_membership_removed=1 undo_restored=1 inverse_replay_restored=1"));
	}

	//! Redoing `vca.create` restores the requested group name, not an empty one.
	void redoingCreateRestoresTheRequestedName()
	{
		clearGroups();
		const QString name = QStringLiteral("Redo preserves this name");
		const QString group = createGroup(name);
		QVERIFY2(!group.isEmpty(), "vca.create failed");

		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).errorKind, ControlErrorKind::NotFound);

		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY2(redone.ok, qPrintable(redone.errorMessage));
		QVERIFY(redone.result.value(QStringLiteral("redone")).toBool());
		const ControlResult restored = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(restored.result.value(QStringLiteral("name")).toString(), name);
		clearGroups();
		evidence(QStringLiteral("create_undo=1 redo=1 name_restored=1"));
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

	//! Undoing `vca.unassign` restores a SHARED membership: a channel assigned
	//! with `also:true` lives in two groups, and the undo has to put it back
	//! with that same multiple-membership mode. Re-adding it single-valued asks
	//! VcaGroup::addMember to take a channel another group already holds, which
	//! it refuses - so the undo silently drops a membership that was there
	//! before the undo, and the channel plays at a group's gain in one bus only.
	void undoingUnassignRestoresASharedMembership()
	{
		clearGroups();
		const QString first = createGroup(QStringLiteral("Shared one"));
		const QString second = createGroup(QStringLiteral("Shared two"));
		QVERIFY(!first.isEmpty());
		QVERIFY(!second.isEmpty());

		// `also:true` is the multiple-membership mode (0.5.0 L09): the channel
		// belongs to BOTH groups, and both hold it deliberately.
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), first},
			{QStringLiteral("channel"), channelId(1)}, {QStringLiteral("also"), true}}).ok);
		QVERIFY(run(QStringLiteral("vca.assign"), {{QStringLiteral("group"), second},
			{QStringLiteral("channel"), channelId(1)}, {QStringLiteral("also"), true}}).ok);
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), second}}).result.value(QStringLiteral("member_count")).toInt(), 1);

		QVERIFY(run(QStringLiteral("vca.unassign"), {{QStringLiteral("group"), second},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), second}}).result.value(QStringLiteral("member_count")).toInt(), 0);

		// The undo path: control.undo restores the SHARED membership in both groups.
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), second}}).result.value(QStringLiteral("member_count")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), first}}).result.value(QStringLiteral("member_count")).toInt(), 1);

		// Luna 4911: the ADVERTISED inverse must be replayable as written. It names vca.assign with
		// also:true - without it, the channel is refused by `first`, which still holds it. Unassign
		// again first, so the replay has a membership to put back.
		QVERIFY(run(QStringLiteral("vca.unassign"), {{QStringLiteral("group"), second},
			{QStringLiteral("channel"), channelId(1)}}).ok);
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), second}}).result.value(QStringLiteral("member_count")).toInt(), 0);
		const QJsonObject inverse = revtest::stateOf(QStringLiteral("vca.unassign"))
			.value(QStringLiteral("inverse")).toObject();
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("vca.assign"));
		const QJsonObject inverseArgs = inverse.value(QStringLiteral("args")).toObject();
		QCOMPARE(inverseArgs.value(QStringLiteral("also")).toBool(), true);
		QVERIFY2(run(inverse.value(QStringLiteral("op")).toString(), inverseArgs).ok,
			"the advertised vca.assign inverse was refused");
		// The replay must restore the same membership: `second` again AND still `first`.
		const ControlResult restored = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), second}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(restored.result.value(QStringLiteral("member_count")).toInt(), 1);
		QCOMPARE(run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), first}}).result.value(QStringLiteral("member_count")).toInt(), 1);
		evidence(QStringLiteral("shared_unassign=1 undo_shared_restored=1"));
	}

	//! NaN is a numeric QJsonValue, so the range checks must refuse it explicitly.
	void nonFiniteGainIsRefusedWithoutChangingTheGroupFader()
	{
		clearGroups();
		const QString group = createGroup();
		QVERIFY2(!group.isEmpty(), "vca.create failed");

		const QJsonObject args{{QStringLiteral("group"), group},
			{QStringLiteral("gain"), std::numeric_limits<double>::quiet_NaN()}};
		QVERIFY(args.value(QStringLiteral("gain")).isDouble());
		QVERIFY(std::isnan(args.value(QStringLiteral("gain")).toDouble()));

		const ControlResult attempted = run(QStringLiteral("vca.set_gain"), args);
		const double gainAfter = run(QStringLiteral("vca.get_state"),
			{{QStringLiteral("group"), group}}).result.value(QStringLiteral("volume")).toDouble();
		const bool refused = !attempted.ok && attempted.errorKind == ControlErrorKind::InvalidArgs;
		const bool unchanged = std::isfinite(gainAfter) && gainAfter == 1.0;
		if (!refused)
		{
			// Keep the following cases isolated on the original implementation,
			// where NaN is accepted and stored.
			run(QStringLiteral("vca.set_gain"),
				{{QStringLiteral("group"), group}, {QStringLiteral("gain"), 1.0}});
		}
		clearGroups();

		QVERIFY2(refused, qPrintable(attempted.errorMessage));
		QVERIFY(unchanged);
	}
};

QTEST_GUILESS_MAIN(ControlVcaCommandsTest)
#include "ControlVcaCommandsTest.moc"
