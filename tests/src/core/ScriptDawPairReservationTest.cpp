/*
 * ScriptDawPairReservationTest.cpp - a change that needs TWO queue slots must
 * be refused whole.
 *
 * The finding (cluster new-p14-285, round 2): a model edit travels as two
 * commands - the undo checkpoint and the value write. A queue with room for
 * one of them but not for the other would take the checkpoint and drop the
 * write, and the script would be left with an EMPTY undo step: one that
 * restores nothing, while the mutator reported a refusal. The pair must
 * reserve its room as a whole.
 *
 * What this test refuses to accept as proof:
 *   - a queue that is simply full. A full queue refuses the FIRST command of
 *     the pair, so it proves nothing about the second one. The case here is
 *     the narrow one: exactly one slot free, so the pair does not fit and
 *     the checkpoint must not be taken on its own.
 *   - a clock. The queue's room is measured by filling it and emptying it
 *     again, and the state is read back from the engine, not waited for.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <QtTest>

#include <QStringList>

#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "RackTestSupport.h"
#include "ScriptDawBindings.h"
#include "ScriptEngine.h"

using namespace lmms;

namespace
{

/*! A command that changes nothing when the apply side runs it: a checkpoint
 *  with no journalled object behind it. It fills the bounded queue exactly the
 *  way a script's own edits fill it, without touching the engine. */
ScriptCommand noOpCommand()
{
	ScriptCommand command;
	command.type = ScriptCommand::Type::AddCheckPoint;
	command.object0 = nullptr;
	return command;
}

//! The refusal the script was given, if it was given one.
QString refusalIn(const QStringList& logs)
{
	for (const QString& line : logs)
	{
		if (line.contains(QStringLiteral("refused"))) { return line; }
	}
	return QString();
}

/*! Leave the queue exactly ONE slot short of full: the state where a change
 *  that needs two slots cannot land whole.
 *
 *  The room is measured, never assumed: the queue is filled to the brim (the
 *  overflow is dropped and counted), emptied, and filled again - which leaves
 *  the same amount of room behind, because the queue is full both times. The
 *  count of what fitted is the room, and the queue is then filled to one slot
 *  short of it. */
int leaveOneFreeSlot()
{
	ScriptEngine* engine = ScriptEngine::instance();
	const ScriptCommand filler = noOpCommand();
	for (std::size_t i = 0; i < 4 * ScriptCommandQueueCapacity; ++i)
	{
		engine->enqueue(filler);
	}
	engine->processCommands();
	for (std::size_t i = 0; i < 4 * ScriptCommandQueueCapacity; ++i)
	{
		engine->enqueue(filler);
	}
	const int room = engine->pendingCommandCount();
	engine->processCommands();
	for (int i = 0; i < room - 1; ++i)
	{
		engine->enqueue(filler);
	}
	return engine->pendingCommandCount();
}

} // namespace

class ScriptDawPairReservationTest : public QObject
{
	Q_OBJECT

	int m_channel = -1;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_channel = Engine::mixer()->createChannel();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/*! A model edit with one slot left: the change is refused whole. The
	 *  model does not move, the script is told, and NO undo step is left
	 *  behind - an empty undo step restores nothing, and a refusal that
	 *  leaves one is a refusal that lies. */
	void aModelWriteWithOneSlotLeftLeavesNoStrandedUndoCheckpoint()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		QVERIFY(channel != nullptr);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const bool originalMuted = channel->m_muteModel.value();
		const int undoDepth = journal->undoDepth();

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		const int pending = leaveOneFreeSlot();
		QVERIFY(pending > 0);

		ScriptDawBindings::newMixerChannel(channel).setMuted(!originalMuted);

		const QStringList logs = engine->takeLogMessages();
		const QString refusal = refusalIn(logs);
		QVERIFY2(!refusal.isEmpty(),
			qPrintable(QStringLiteral("the refused write was not reported to the script: %1")
				.arg(logs.join(QStringLiteral(" | ")))));
		QVERIFY2(refusal.contains(QStringLiteral("queue is full")), qPrintable(refusal));
		// The model did not move.
		QCOMPARE(channel->m_muteModel.value(), originalMuted);

		// And nothing of the change was left behind: the queue carried the
		// fillers and nothing else, so applying it queues no undo step.
		QCOMPARE(engine->processCommands(), pending);
		QCOMPARE(journal->undoDepth(), undoDepth);
	}

	/*! The same path with room for the pair: the change lands whole - the
	 *  model moves AND the undo step that makes it undoable is there. The
	 *  reservation must not cost the change its undo step. */
	void aModelWriteWithRoomForThePairIsQueuedWhole()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		QVERIFY(channel != nullptr);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const bool originalMuted = channel->m_muteModel.value();
		const int undoDepth = journal->undoDepth();

		ScriptEngine* engine = ScriptEngine::instance();
		engine->processCommands();
		engine->takeLogMessages();

		ScriptDawBindings::newMixerChannel(channel).setMuted(!originalMuted);

		QVERIFY2(refusalIn(engine->takeLogMessages()).isEmpty(),
			"a change with room for the pair was refused");
		// Both commands of the pair land: the checkpoint and the write.
		QCOMPARE(engine->processCommands(), 2);
		QCOMPARE(channel->m_muteModel.value(), !originalMuted);
		QVERIFY(journal->undoDepth() > undoDepth);
		// And the change is undoable: ONE undo puts the model back.
		journal->undo();
		QCOMPARE(channel->m_muteModel.value(), originalMuted);
	}
};

QTEST_MAIN(ScriptDawPairReservationTest)
#include "ScriptDawPairReservationTest.moc"
