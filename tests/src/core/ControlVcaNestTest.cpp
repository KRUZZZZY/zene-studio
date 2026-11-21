/*
 * ControlVcaNestTest.cpp - vca.set_links and vca.set_parent with a request that changes nothing
 *
 * The two commands share one change path (src/core/ControlCommandsVcaNest.cpp, change()). A call that asks
 * for what the group already has changes nothing, so it must record no undo step and must not mark the
 * project modified - otherwise control.undo steps through a step that undoes nothing, and the project looks
 * edited when it was not. A call that really changes a link must still record exactly one undo step.
 *
 * The group's surface half (ids, schemas, refusals, fader, membership) is ControlVcaCommandsTest.cpp; the
 * shared helpers are in ControlVcaTestSupport.h.
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

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVcaTestSupport.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "VcaGroup.h"

using namespace lmms;
using namespace revtest;
using namespace vcatest;

class ControlVcaNestTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! set_links with only the group, and again with the group's own four links written out: both succeed,
	//! neither records an undo step, and neither marks the project modified.
	void aNoOpSetLinksRecordsNoUndoStepAndLeavesTheProjectUnmodified()
	{
		clearGroups();
		const QString group = createGroup();
		QVERIFY2(!group.isEmpty(), "vca.create failed");
		ProjectJournal* journal = Engine::projectJournal();
		Song* song = Engine::getSong();
		journal->clearJournal();
		song->noteProjectSaved(QStringLiteral("vca-nest.mmp"));
		QVERIFY(!song->isModified());
		const int depth = journal->undoDepth();

		const ControlResult bare = run(QStringLiteral("vca.set_links"), {{QStringLiteral("group"), group}});
		QVERIFY2(bare.ok, qPrintable(bare.errorMessage));
		QCOMPARE(journal->undoDepth(), depth);
		QVERIFY(!song->isModified());

		const ControlResult repeated = run(QStringLiteral("vca.set_links"), {{QStringLiteral("group"), group},
			{QStringLiteral("gain"), true}, {QStringLiteral("mute"), true}, {QStringLiteral("solo"), true},
			{QStringLiteral("suspended"), false}});
		QVERIFY2(repeated.ok, qPrintable(repeated.errorMessage));
		QCOMPARE(journal->undoDepth(), depth);
		QVERIFY(!song->isModified());
		evidence(QStringLiteral("set_links_noop undo_steps=0 modified=0"));
	}

	//! set_parent with the group left out of a parent: the group has none already, so nothing changes.
	void aNoOpSetParentRecordsNoUndoStepAndLeavesTheProjectUnmodified()
	{
		clearGroups();
		const QString group = createGroup();
		QVERIFY2(!group.isEmpty(), "vca.create failed");
		ProjectJournal* journal = Engine::projectJournal();
		Song* song = Engine::getSong();
		journal->clearJournal();
		song->noteProjectSaved(QStringLiteral("vca-nest.mmp"));
		QVERIFY(!song->isModified());
		const int depth = journal->undoDepth();

		const ControlResult none = run(QStringLiteral("vca.set_parent"), {{QStringLiteral("group"), group}});
		QVERIFY2(none.ok, qPrintable(none.errorMessage));
		QCOMPARE(journal->undoDepth(), depth);
		QVERIFY(!song->isModified());
		evidence(QStringLiteral("set_parent_noop undo_steps=0 modified=0"));
	}

	//! A real change is still ONE undo step, marks the project modified, and control.undo puts the link back.
	void aRealChangeStillRecordsOneUndoStepAndMarksTheProjectModified()
	{
		clearGroups();
		const QString group = createGroup();
		QVERIFY2(!group.isEmpty(), "vca.create failed");
		ProjectJournal* journal = Engine::projectJournal();
		Song* song = Engine::getSong();
		journal->clearJournal();
		song->noteProjectSaved(QStringLiteral("vca-nest.mmp"));
		const int depth = journal->undoDepth();

		const ControlResult muted = run(QStringLiteral("vca.set_links"),
			{{QStringLiteral("group"), group}, {QStringLiteral("mute"), false}});
		QVERIFY2(muted.ok, qPrintable(muted.errorMessage));
		VcaGroup* vca = Engine::mixer()->vcaGroups().front();
		QVERIFY(!vca->linksMute());
		QCOMPARE(journal->undoDepth(), depth + 1);
		QVERIFY(song->isModified());

		REV_UNDO_OR_FAIL();
		QVERIFY(vca->linksMute());
		evidence(QStringLiteral("set_links_real undo_steps=1 modified=1 undo_restored=1"));
	}

	//! After a real change, a repeat of it changes nothing, so control.undo still undoes the REAL change: the
	//! repeat did not push a step on top of it.
	void aNoOpAfterARealChangeLeavesThatChangeAsTheOnlyUndoStep()
	{
		clearGroups();
		const QString group = createGroup();
		QVERIFY2(!group.isEmpty(), "vca.create failed");
		ProjectJournal* journal = Engine::projectJournal();
		Song* song = Engine::getSong();
		journal->clearJournal();
		song->noteProjectSaved(QStringLiteral("vca-nest.mmp"));
		const int depth = journal->undoDepth();

		QVERIFY(run(QStringLiteral("vca.set_links"),
			{{QStringLiteral("group"), group}, {QStringLiteral("mute"), false}}).ok);
		QCOMPARE(journal->undoDepth(), depth + 1);

		const ControlResult repeat = run(QStringLiteral("vca.set_links"),
			{{QStringLiteral("group"), group}, {QStringLiteral("mute"), false}});
		QVERIFY2(repeat.ok, qPrintable(repeat.errorMessage));
		QCOMPARE(journal->undoDepth(), depth + 1);

		VcaGroup* vca = Engine::mixer()->vcaGroups().front();
		REV_UNDO_OR_FAIL();
		QVERIFY(vca->linksMute());
		QCOMPARE(journal->undoDepth(), depth);
		evidence(QStringLiteral("repeat_after_change undo_steps=1 undo_restored=1"));
	}
};

QTEST_GUILESS_MAIN(ControlVcaNestTest)
#include "ControlVcaNestTest.moc"
