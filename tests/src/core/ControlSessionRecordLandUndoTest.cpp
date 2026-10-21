/*
 * ControlSessionRecordLandUndoTest.cpp - the landing pass is ONE undo step
 *
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

/*! WHY THIS FILE EXISTS, and why it is its own binary.
 *
 *  The undo half of session.arrangement_record_land: the pass creates
 *  arrangement clips, so it must be ONE undo step - one control.undo takes the
 *  whole pass back - whatever it lands and however tight the undo stack's own
 *  bound is. The pairing, the clips themselves and the refusals are proven
 *  beside this file, in tests/src/core/ControlSessionRecordLandTest.cpp; what
 *  is proven HERE is the step the pass leaves on the stack and what one undo
 *  restores.
 *
 *  IT IS A SEPARATE BINARY, not a case added to that one, for a mechanical
 *  reason: ctest numbers its tests in the order they are registered, and the
 *  repo's negative control (the harness that reverts a branch's product change
 *  and runs the branch's own tests to prove they fail without it) reads that
 *  list with a pattern that only sees THREE-DIGIT test numbers - ctest prints
 *  a two-digit number as "Test  #27:", with the number padded, so the
 *  ControlSessionRecordLandTest binary (number 27) is invisible to it. This
 *  binary is registered at the END of LMMS_TESTS in tests/CMakeLists.txt, so
 *  its number is three digits and the mechanical proof can see the regression
 *  this file carries.
 *
 *  The undo stack's own bound is the seam: it is set and read through the
 *  engine's accounting (ProjectJournal), never through a clock, so every
 *  number below is exact.
 */

#include <QtTest>

#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "SessionArrangementRecorder.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

SessionArrangementRecorder& recorder() { return Engine::getSong()->sessionScheduler().arrangementRecorder(); }

} // namespace

class ControlSessionRecordLandUndoTest : public QObject
{
	Q_OBJECT

	Song* m_song = nullptr;
	Track* m_first = nullptr;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_song = Engine::getSong();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! One song track, a 2x2 session grid with a MIDI cell on (0,0), and an armed, empty ring.
	void init()
	{
		m_song->clearProject();
		m_first = Track::create(Track::Type::Instrument, m_song);
		QVERIFY(run(QStringLiteral("session.set_grid"),
			{{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0}}).ok);
		SessionArrangementRecorder::Event ignored;
		while (recorder().pop(ignored)) {}
		recorder().setArmed(true);
	}

	void cleanup()
	{
		recorder().setArmed(false);
		// The bound is the engine's default again whatever the case below did to it.
		if (ProjectJournal* journal = Engine::projectJournal(); journal != nullptr)
		{
			journal->setMaxUndoStates(ProjectJournal::MAX_UNDO_STATES);
		}
	}

	/*! The pass is ONE undo step, however many clips it lands on one track.
	 *
	 *  The undo stack's own bound is the seam: with the bound at ONE step the
	 *  stack keeps a single checkpoint, so a pass that checkpoints once per
	 *  clip keeps only the LAST one - and one control.undo then restores the
	 *  track to the state before the last clip, leaving the earlier landed
	 *  clip behind. The bound is set through the journal's own API (the same
	 *  bound control.set_undo_depth drives), so no cap-change transaction is
	 *  recorded and the undo under test is the land's own step.
	 */
	void oneUndoTakesEveryLandedClipBackEvenAtTheUndoCap()
	{
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QVERIFY(journal->setMaxUndoStates(1));

		QVERIFY(recorder().recordStart(0, 0, 0));
		QVERIFY(recorder().recordStop(0, 0, 96));
		QVERIFY(recorder().recordStart(0, 0, 192));
		QVERIFY(recorder().recordStop(0, 0, 288));

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 2);
		QCOMPARE(m_first->getClips().size(), size_t{2});

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(undone.result.value(QStringLiteral("undone")).toBool(), "nothing was undone");
		// ONE undo takes BOTH clips back - the pass is one step, not one per clip.
		QCOMPARE(m_first->getClips().size(), size_t{0});
	}

	//! Cap-bound: two launches on one track land two clips; the pass is ONE checkpoint, so it evicts ONE
	//! older step of the user's own history - not one per clip.
	void aPassAtTheUndoCapsBoundIsStillOneUndo()
	{
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int cap = journal->maxUndoStates();
		for (int guard = 0; guard < cap + 8 && journal->undoDepth() < cap; ++guard)
		{
			m_first->addJournalCheckPoint();
		}
		QCOMPARE(journal->undoDepth(), cap);
		const int evictedBefore = journal->evictedSteps();

		QVERIFY(recorder().recordStart(0, 0, 0));
		QVERIFY(recorder().recordStop(0, 0, 96));
		QVERIFY(recorder().recordStart(0, 0, 192));
		QVERIFY(recorder().recordStop(0, 0, 288));

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 2);
		QCOMPARE(m_first->getClips().size(), size_t{2});
		// One pass, one checkpoint: exactly one eviction at the cap,
		// not one per clip.
		QCOMPARE(journal->evictedSteps() - evictedBefore, 1);

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2(undone.result.value(QStringLiteral("undone")).toBool(), "nothing was undone");
		QCOMPARE(m_first->getClips().size(), size_t{0});
	}
};

QTEST_MAIN(ControlSessionRecordLandUndoTest)
#include "ControlSessionRecordLandUndoTest.moc"
