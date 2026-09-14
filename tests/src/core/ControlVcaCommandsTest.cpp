/*
 * ControlVcaCommandsTest.cpp - the `vca.*` command group's own registered test
 *                               (SPEC A11-A16; the 0.3.0 ladder's OWNER-31 item
 *                               11, "phase-locked multitrack edit groups").
 *
 * What this file is for. The engine half's semantics - relative scaling, the
 * dB delta, save/load, the allocation count on the audio path - are VcaGroupTest's
 * subject (#622), and this lane extends that file for the edit set. THIS file is
 * the SURFACE half, and it holds four claims to account:
 *
 *   1. the fourteen ids exist, are registered, declare schemas, and each has a
 *      contract row whose class matches its behaviour;
 *   2. junk arguments produce TYPED refusals (invalid_args / not_found /
 *      refused) and never a crash or a silent success - the property the
 *      agent-surface sweep depends on;
 *   3. the MEASURED effect of the commands, read back through the group's own
 *      reads: the fader scales members relatively without writing their own
 *      faders, membership is single-valued, and a deleted group comes back
 *      exactly;
 *   4. SPEC A16: apply -> control.undo -> read back, for the fader, the
 *      membership, the group's existence and - the point of the feature - a
 *      phase-locked move across several tracks, which must be ONE undo step.
 *
 * Evidence lines are printed with the VCA_EDIT_EVIDENCE prefix so the numbers
 * can be pasted into the report.
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

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Mixer.h"
#include "ReversibilityTestSupport.h"
#include "Track.h"
#include "VcaGroup.h"

using namespace lmms;
using namespace revtest;

namespace
{

const QStringList kVcaIds = {
	QStringLiteral("vca.create"),
	QStringLiteral("vca.remove"),
	QStringLiteral("vca.list"),
	QStringLiteral("vca.get_state"),
	QStringLiteral("vca.rename"),
	QStringLiteral("vca.set_gain"),
	QStringLiteral("vca.set_mute"),
	QStringLiteral("vca.set_solo"),
	QStringLiteral("vca.assign"),
	QStringLiteral("vca.unassign"),
	QStringLiteral("vca.set_phase_lock"),
	QStringLiteral("vca.track_add"),
	QStringLiteral("vca.track_remove"),
	QStringLiteral("vca.edit_move"),
};

//! The two reads, which the contract calls not_mutating.
const QStringList kVcaReads = {
	QStringLiteral("vca.list"),
	QStringLiteral("vca.get_state"),
};

//! A clip of `length` ticks at `position` on `track`; the clip-<n> id.
QString addClip(const QString& track, int position, int length)
{
	const ControlResult added = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track}, {QStringLiteral("position"), position},
			{QStringLiteral("length"), length}});
	return added.ok ? added.result.value(QStringLiteral("clip")).toString() : QString();
}

void evidence(const QString& line)
{
	std::fprintf(stdout, "VCA_EDIT_EVIDENCE %s\n", qPrintable(line));
	std::fflush(stdout);
}

//! Create a group and return its vca-<n> id, or empty when the command failed.
QString createGroup(const QString& name = QStringLiteral("Drums"))
{
	const ControlResult made = run(QStringLiteral("vca.create"),
		QJsonObject{{QStringLiteral("name"), name}});
	return made.ok ? made.result.value(QStringLiteral("group")).toString() : QString();
}

//! One group's `gain_for_member` for a channel, or -1 when the group does not
//! hold it.
double memberGain(const QString& group, const QString& channel)
{
	const ControlResult state = run(QStringLiteral("vca.get_state"),
		QJsonObject{{QStringLiteral("group"), group}});
	for (const QJsonValue& value : state.result.value(QStringLiteral("members")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("channel")).toString() == channel)
		{
			return entry.value(QStringLiteral("gain_for_member")).toDouble();
		}
	}
	return -1.0;
}

//! Drop every group the mix currently holds, so each test starts from a mix it
//! owns (the registry and the engine are process-wide).
void clearGroups()
{
	Mixer* mixer = Engine::mixer();
	while (!mixer->vcaGroups().empty())
	{
		if (!mixer->deleteVcaGroup(mixer->vcaGroups().front()->id())) { break; }
	}
}

} // namespace

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
		QCOMPARE(kVcaIds.size(), 14);
		for (const QString& id : kVcaIds)
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
		for (const QString& id : kVcaIds)
		{
			const ControlCommand* cmd = registry->command(id);
			QCOMPARE(cmd->mutating, !kVcaReads.contains(id));
		}
		evidence(QStringLiteral("ids=%1 registered=1 reads=%2 mutating=%3")
			.arg(kVcaIds.size()).arg(kVcaReads.size()).arg(kVcaIds.size() - kVcaReads.size()));
	}

	//! The A16 contract table has a row for every id, the class agrees with the
	//! behaviour (a read writes nothing; every writer here has an inverse), and
	//! every row states a non-empty reason and mechanism.
	void everyIdHasAContractRow()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		int trueInverse = 0;
		int notMutating = 0;
		for (const QString& id : kVcaIds)
		{
			const control::ReversibilityEntry* row = table.lookup(id);
			QVERIFY2(row != nullptr, qPrintable(QStringLiteral("no contract row for %1").arg(id)));
			QVERIFY2(!row->reason.isEmpty(), qPrintable(id + " has an empty reason"));
			QVERIFY2(!row->mechanism.isEmpty(), qPrintable(id + " has an empty mechanism"));
			if (kVcaReads.contains(id))
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
			.arg(kVcaIds.size()).arg(trueInverse).arg(notMutating));
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
			{{QStringLiteral("group"), second}, {QStringLiteral("channel"), channelId(4)}});
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

	// -- 4. the phase-locked edit, which is the feature ---------------------

	//! Build two tracks with one clip each at the same position and length, lock
	//! them into one edit group, and move ONE clip: both must move by the same
	//! delta, the report must say so, and ONE control.undo must put both back.
	void aPhaseLockedMoveEditsEveryMember()
	{
		clearGroups();
		const QString first = addTrack(QStringLiteral("instrument"));
		const QString second = addTrack(QStringLiteral("instrument"));
		QVERIFY(!first.isEmpty());
		QVERIFY(!second.isEmpty());
		const QString anchorClip = addClip(first, 1000, 500);
		const QString otherClip = addClip(second, 1000, 500);
		QVERIFY(!anchorClip.isEmpty());
		QVERIFY(!otherClip.isEmpty());

		const QString group = createGroup(QStringLiteral("Take"));
		QVERIFY(!group.isEmpty());
		QVERIFY(run(QStringLiteral("vca.track_add"), {{QStringLiteral("group"), group},
			{QStringLiteral("track"), first}}).ok);
		QVERIFY(run(QStringLiteral("vca.track_add"), {{QStringLiteral("group"), group},
			{QStringLiteral("track"), second}}).ok);

		const ControlResult moved = run(QStringLiteral("vca.edit_move"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), anchorClip},
				{QStringLiteral("position"), 1500}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("delta")).toInt(), 500);
		QCOMPARE(moved.result.value(QStringLiteral("moved_count")).toInt(), 2);
		QCOMPARE(moved.result.value(QStringLiteral("unlocked_count")).toInt(), 0);
		QCOMPARE(moved.result.value(QStringLiteral("skipped_count")).toInt(), 0);
		QCOMPARE(clipPosition(anchorClip), 1500);
		QCOMPARE(clipPosition(otherClip), 1500);

		// ONE undo, not one per member.
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipPosition(anchorClip), 1000);
		QCOMPARE(clipPosition(otherClip), 1000);
		evidence(QStringLiteral("locked_move delta=500 moved=2 unlocked=0 anchor=1500 member=1500 "
			"after_one_undo anchor=1000 member=1000"));
	}

	//! A member with nothing in the anchor's span is reported, not silently
	//! counted: the third track's clip is far away, so it must NOT move, and the
	//! refusal/answer must say so.
	void aMemberWithNothingInTheSpanIsReportedUnlocked()
	{
		clearGroups();
		const QString first = addTrack(QStringLiteral("instrument"));
		const QString second = addTrack(QStringLiteral("instrument"));
		const QString third = addTrack(QStringLiteral("instrument"));
		QVERIFY(!first.isEmpty() && !second.isEmpty() && !third.isEmpty());
		const QString anchorClip = addClip(first, 2000, 400);
		QVERIFY(!anchorClip.isEmpty());
		QVERIFY(!addClip(second, 2000, 400).isEmpty());
		// Deliberately elsewhere: this member has nothing to lock to the anchor.
		QVERIFY(!addClip(third, 9000, 400).isEmpty());

		const QString group = createGroup(QStringLiteral("Partial"));
		for (const QString& track : {first, second, third})
		{
			QVERIFY(run(QStringLiteral("vca.track_add"),
				{{QStringLiteral("group"), group}, {QStringLiteral("track"), track}}).ok);
		}

		const ControlResult moved = run(QStringLiteral("vca.edit_move"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), anchorClip},
				{QStringLiteral("position"), 2100}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("moved_count")).toInt(), 2);
		QCOMPARE(moved.result.value(QStringLiteral("unlocked_tracks")).toArray().at(0).toString(),
			third);
		const QJsonArray thirdClips = clipsOf(third);
		QCOMPARE(thirdClips.size(), 1);
		QCOMPARE(clipPosition(thirdClips.at(0).toString()), 9000);
		evidence(QStringLiteral("locked_move_partial moved=2 unlocked=1 unlocked_track_untouched=1"));
	}

	// -- 5. the engine half: the edit set and the lock in the mixer element --

	//! The ENTITY half of this lane's change, exercised directly: a group's
	//! edit set and phase lock are part of its `<vcagroup>` element, they round
	//! trip through Mixer::saveSettings/loadSettings, and an element written
	//! BEFORE they existed loads as the locked, edit-set-less group it was.
	//!
	//! It runs on a SCRATCH Mixer rather than the engine's, because
	//! Mixer::loadSettings begins with clear() and the engine's mixer is the one
	//! the song's tracks are wired into.
	void theEditSetAndTheLockRoundTripThroughTheMixerElement()
	{
		Mixer scratch;
		while (scratch.numChannels() < 3) { scratch.createChannel(); }
		VcaGroup* group = scratch.createVcaGroup(QStringLiteral("Scratch"), 3);
		QVERIFY(group != nullptr);
		QVERIFY2(group->isPhaseLocked(), "a new group is not phase-locked by default");
		QVERIFY(group->addMember(1));
		QVERIFY2(!group->addEditTrack(-1), "a negative track id was accepted into an edit set");
		QVERIFY(group->addEditTrack(7));
		QVERIFY2(!group->addEditTrack(7), "the same track id entered an edit set twice");
		QVERIFY(group->addEditTrack(2));
		QVERIFY2(group->editTracks() == std::vector<int>({2, 7}),
			"the edit set is not ascending");
		QVERIFY(group->hasEditTrack(7));
		group->setPhaseLocked(false);
		group->vcaModel()->setValue(0.6f);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		scratch.saveSettings(doc, root);
		const QString xml = doc.toString();
		QVERIFY2(xml.contains(QStringLiteral("locked=\"0\"")), qPrintable(xml.left(900)));
		QVERIFY2(xml.contains(QStringLiteral("<edittrack track=\"7\"/>")), qPrintable(xml.left(900)));
		QVERIFY2(xml.contains(QStringLiteral("<edittrack track=\"2\"/>")), qPrintable(xml.left(900)));

		scratch.loadSettings(root);
		VcaGroup* loaded = scratch.vcaGroup(3);
		QVERIFY(loaded != nullptr);
		QCOMPARE(loaded->name(), QStringLiteral("Scratch"));
		QCOMPARE(loaded->isPhaseLocked(), false);
		QVERIFY(loaded->editTracks() == std::vector<int>({2, 7}));
		QVERIFY(loaded->contains(1));
		QVERIFY(std::abs(loaded->vcaModel()->value() - 0.6f) < 1.0e-6f);

		// A <vcagroup> written before the edit half existed carries no `locked`
		// attribute; it must load as the LOCKED group it was, with no edit set.
		QDomDocument legacy;
		QVERIFY(legacy.setContent(QStringLiteral(
			"<mixer><mixerchannel num=\"0\" name=\"master\" volume=\"1\"/>"
			"<vcagroup id=\"0\" name=\"Old\"><member channel=\"1\"/></vcagroup></mixer>")));
		Mixer scratch2;
		scratch2.loadSettings(legacy.documentElement());
		VcaGroup* old = scratch2.vcaGroup(0);
		QVERIFY2(old != nullptr, "a legacy <vcagroup> did not load at all");
		QCOMPARE(old->name(), QStringLiteral("Old"));
		QVERIFY2(old->isPhaseLocked(), "a legacy group loaded as unlocked");
		QVERIFY2(old->editTracks().empty(), "a legacy group gained an edit set from nowhere");
		evidence(QStringLiteral("edit_set_roundtrip=[2,7] locked_attr=0 legacy_locked=1 "
			"edittrack_elements=2"));
	}

	//! Every reason the lock cannot propagate is refused, typed, BEFORE anything
	//! is written: no anchor in the set, fewer than two live tracks, and the
	//! lock switched off.
	void theLockRefusesWhenThereIsNothingToLockTo()
	{
		clearGroups();
		const QString first = addTrack(QStringLiteral("instrument"));
		const QString second = addTrack(QStringLiteral("instrument"));
		const QString third = addTrack(QStringLiteral("instrument"));
		const QString clip = addClip(first, 3000, 200);
		QVERIFY(!clip.isEmpty());

		const QString group = createGroup(QStringLiteral("Refusals"));
		QVERIFY(run(QStringLiteral("vca.track_add"),
			{{QStringLiteral("group"), group}, {QStringLiteral("track"), first}}).ok);
		QVERIFY(run(QStringLiteral("vca.track_add"),
			{{QStringLiteral("group"), group}, {QStringLiteral("track"), second}}).ok);

		// (a) the anchor's track is not in the edit set.
		const ControlResult outsider = run(QStringLiteral("vca.edit_move"),
			{{QStringLiteral("group"), group},
				{QStringLiteral("clip"), addClip(third, 3000, 200)},
				{QStringLiteral("position"), 3100}});
		QVERIFY2(!outsider.ok, "an edit on a track outside the set was propagated");
		QCOMPARE(outsider.errorKind, ControlErrorKind::Refused);
		QVERIFY2(outsider.errorMessage.contains(QStringLiteral("vca.track_add")),
			qPrintable("the refusal does not name the fix: " + outsider.errorMessage));

		// (b) the lock is off.
		QVERIFY(run(QStringLiteral("vca.set_phase_lock"),
			{{QStringLiteral("group"), group}, {QStringLiteral("locked"), false}}).ok);
		const ControlResult unlocked = run(QStringLiteral("vca.edit_move"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), clip},
				{QStringLiteral("position"), 3100}});
		QVERIFY2(!unlocked.ok, "a move propagated through a group whose lock is off");
		QCOMPARE(unlocked.errorKind, ControlErrorKind::Refused);
		QVERIFY2(unlocked.errorMessage.contains(QStringLiteral("vca.set_phase_lock")),
			qPrintable("the refusal does not name the fix: " + unlocked.errorMessage));

		// (c) fewer than two live tracks: drop the second member.
		QVERIFY(run(QStringLiteral("vca.set_phase_lock"),
			{{QStringLiteral("group"), group}, {QStringLiteral("locked"), true}}).ok);
		QVERIFY(run(QStringLiteral("vca.track_remove"),
			{{QStringLiteral("group"), group}, {QStringLiteral("track"), second}}).ok);
		const ControlResult alone = run(QStringLiteral("vca.edit_move"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), clip},
				{QStringLiteral("position"), 3100}});
		QVERIFY2(!alone.ok, "a move was propagated by a group with one live member");
		QCOMPARE(alone.errorKind, ControlErrorKind::Refused);

		// Nothing above moved: a refusal writes nothing.
		QCOMPARE(clipPosition(clip), 3000);
		evidence(QStringLiteral("refused_outside_set=1 refused_lock_off=1 refused_single_member=1 "
			"anchor_untouched=1"));
	}
};

QTEST_GUILESS_MAIN(ControlVcaCommandsTest)
#include "ControlVcaCommandsTest.moc"
