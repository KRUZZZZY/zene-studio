/*
 * ControlVcaEditGroupsTest.cpp - the `vca.*` command group's EDIT half (SPEC
 *                              A11-A16; the 0.3.0 ladder's OWNER-31 item 11,
 *                              "phase-locked multitrack edit groups").
 *
 * The group is proved by two registered suites, split because Gate 7 refuses a
 * NEW file over 500 lines (this one and its sibling were one file at 613): THIS
 * file holds the EDIT half: the phase-locked move across several tracks (one
 * clip moves, every member's overlapping clip moves with it, and ONE undo returns
 * all of them), a member with nothing in the anchor's span reported unlocked and
 * left untouched, the entity's own edit set and lock written into the group's
 * <vcagroup> element, and the three ways the lock refuses to propagate. The
 * SURFACE half - the ids and their schemas, the contract rows, the typed
 * refusals, the fader and membership - is ControlVcaCommandsTest.cpp. The helpers
 * both halves use, and the reason they are shared rather than duplicated, are in
 * ControlVcaTestSupport.h.
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

class ControlVcaEditGroupsTest : public QObject
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
	}	// -- 1. the phase-locked edit, which is the feature ---------------------

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

	// -- 2. the engine half: the edit set and the lock in the mixer element --

	//! The ENTITY half of the feature, exercised directly: a group's
	//! edit set is a set of stable ids in ascending order that refuses a
	//! negative id and a duplicate, the lock is on by default and switchable,
	//! and both are written into the group's own `<vcagroup>` element.
	//!
	//! WHY THERE IS NO SECOND Mixer AND NO loadSettings HERE, both measured
	//! rather than assumed: a standalone `Mixer` in a process that already has
	//! an `Engine` SEGFAULTS at `Engine::destroy()` (address 0x68, after every
	//! slot had passed - the crash this slot used to cause), and
	//! `Mixer::loadSettings` on the engine's own mixer begins with `clear()`,
	//! which DELETES channels - and the song's tracks are wired to those
	//! channels by index, so the suite would be reading freed memory from the
	//! next slot on. The LOAD direction is proved where a real load happens:
	//! `tests/control-vca-commands.py` saves a project, reopens it and asserts
	//! the edit set and the `locked` attribute came back from the FILE, and
	//! `VcaGroupTest::groupingSurvivesSaveAndReload` does the Mixer-level round
	//! trip in its own process.
	void theEditSetAndTheLockAreWrittenIntoTheMixerElement()
	{
		clearGroups();
		Mixer* mixer = Engine::mixer();
		const QString group = createGroup(QStringLiteral("Scratch"));
		QVERIFY(!group.isEmpty());
		QVERIFY2(!mixer->vcaGroups().empty(), "vca.create left the mixer without a group");
		VcaGroup* made = mixer->vcaGroups().front();
		QVERIFY2(made->isPhaseLocked(), "a new group is not phase-locked by default");
		QVERIFY2(!made->addEditTrack(-1), "a negative track id was accepted into an edit set");
		QVERIFY(made->addEditTrack(7));
		QVERIFY2(!made->addEditTrack(7), "the same track id entered an edit set twice");
		QVERIFY(made->addEditTrack(2));
		QVERIFY2(made->editTracks() == std::vector<int>({2, 7}), "the edit set is not ascending");
		QVERIFY(made->hasEditTrack(7));
		QVERIFY(made->removeEditTrack(2));
		QVERIFY2(!made->removeEditTrack(2), "a track left an edit set twice");
		QVERIFY(made->addEditTrack(2));
		made->setPhaseLocked(false);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(root);
		mixer->saveSettings(doc, root);
		const QString xml = doc.toString();
		QVERIFY2(xml.contains(QStringLiteral("locked=\"0\"")), qPrintable(xml.left(900)));
		QVERIFY2(xml.contains(QStringLiteral("<edittrack track=\"7\"/>")), qPrintable(xml.left(900)));
		QVERIFY2(xml.contains(QStringLiteral("<edittrack track=\"2\"/>")), qPrintable(xml.left(900)));
		evidence(QStringLiteral("edit_set=[2,7] locked_attr=0 edittrack_elements=2 "
			"negative_refused=1 duplicate_refused=1"));
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
	}};

QTEST_GUILESS_MAIN(ControlVcaEditGroupsTest)
#include "ControlVcaEditGroupsTest.moc"
