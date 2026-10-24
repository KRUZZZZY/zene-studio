/*
 * ScriptDawQueueFullTest.cpp - a full script command queue must not swallow a
 * DAW edit without telling the script.
 *
 * The finding (cluster new-p14-285): `enqueueDaw` in src/core/ScriptDawEdit.cpp
 * ignored the enqueue result, and the DAW mutators had no failure result at
 * all, so a script that queued more edits than the bounded queue (1024
 * commands) lost them in silence: `setName` and `setEnabled` returned normally
 * while their mutations were dropped. The queue counted the drops
 * (ScriptEngine::droppedCommandCount), but the script was told nothing, and a
 * lost edit that says nothing is a lie the script cannot act on.
 *
 * What this test refuses to accept as proof:
 *   - a refusal that is only a counter. The script's own console is where
 *     every other refusal in this file's half is reported (logRefusal), so
 *     that is where this one has to be too;
 *   - a test that reaches into the queue's internals. The queue is filled the
 *     way a script fills it - through the same public enqueue() - and the
 *     refusal is read the way a script reads one - through the console.
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
#include "RackTestSupport.h"
#include "ScriptBindings.h"
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

/*! Fill the bounded command queue, so the next edit a script queues is
 *  refused.
 *
 *  The fill is deterministic and needs no clock: the queue holds a fixed
 *  number of commands (ScriptCommandQueueCapacity) and the producer is the
 *  only writer, so pushing more than that leaves the queue full. The pending
 *  commands are drained first, so the queue starts empty, and the fillers are
 *  no-ops, so draining them again afterwards changes nothing. */
void fillCommandQueue()
{
	ScriptEngine* engine = ScriptEngine::instance();
	engine->processCommands();
	const ScriptCommand filler = noOpCommand();
	for (int i = 0; i < 4096; ++i)
	{
		engine->enqueue(filler);
	}
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

} // namespace

class ScriptDawQueueFullTest : public QObject
{
	Q_OBJECT

	int m_channel = -1;
	EffectChain* m_chain = nullptr;
	racktest::RackParamEffect* m_effect = nullptr;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_channel = Engine::mixer()->createChannel();
		m_chain = &Engine::mixer()->mixerChannel(m_channel)->m_fxChain;
		m_effect = new racktest::RackParamEffect(m_chain);
		m_chain->appendEffect(m_effect);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/*! A rename the queue cannot carry is refused, and the script is told:
	 *  the channel keeps its name and the console carries the refusal. */
	void aRenameOnAFullQueueIsRefusedAndTheScriptIsTold()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		QVERIFY(channel != nullptr);
		const QString originalName = channel->m_name;

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		fillCommandQueue();

		ScriptDawBindings::newMixerChannel(channel).setName(QStringLiteral("renamed-while-full"));

		const QStringList logs = engine->takeLogMessages();
		const QString refusal = refusalIn(logs);
		QVERIFY2(!refusal.isEmpty(),
			qPrintable(QStringLiteral("the dropped rename was not reported to the script: %1")
				.arg(logs.join(QStringLiteral(" | ")))));
		QVERIFY2(refusal.contains(QStringLiteral("queue is full")), qPrintable(refusal));
		// The engine never saw the edit: the name is the one the channel had.
		QCOMPARE(channel->m_name, originalName);
	}

	/*! The same for the model-write half (mute): the checkpoint and the write
	 *  it pairs with are two commands, and a queue with no room for them
	 *  refuses the change - with the script told, and the model untouched. */
	void aMuteOnAFullQueueIsRefusedAndTheScriptIsTold()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		QVERIFY(channel != nullptr);
		const bool originalMuted = channel->m_muteModel.value();

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		fillCommandQueue();

		ScriptDawBindings::newMixerChannel(channel).setMuted(!originalMuted);

		const QStringList logs = engine->takeLogMessages();
		const QString refusal = refusalIn(logs);
		QVERIFY2(!refusal.isEmpty(),
			qPrintable(QStringLiteral("the dropped mute was not reported to the script: %1")
				.arg(logs.join(QStringLiteral(" | ")))));
		QVERIFY2(refusal.contains(QStringLiteral("queue is full")), qPrintable(refusal));
		QCOMPARE(channel->m_muteModel.value(), originalMuted);
	}

	/*! A direct FloatModel binding is the one-command sibling of the mixer
	 *  fader helper; it also reports a dropped write to the script. */
	void aDirectModelWriteOnAFullQueueIsRefusedAndTheScriptIsTold()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		QVERIFY(channel != nullptr);
		const float originalValue = channel->m_volumeModel.value();
		LuaFloatModel model(&channel->m_volumeModel);

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		fillCommandQueue();

		model.setValue(originalValue == 0.0f ? 0.5f : 0.0f);

		const QStringList logs = engine->takeLogMessages();
		const QString refusal = refusalIn(logs);
		QVERIFY2(!refusal.isEmpty(),
			qPrintable(QStringLiteral("the dropped model write was not reported to the script: %1")
				.arg(logs.join(QStringLiteral(" | ")))));
		QVERIFY2(refusal.contains(QStringLiteral("queue is full")), qPrintable(refusal));
		QCOMPARE(channel->m_volumeModel.value(), originalValue);
	}

	/*! The stable-ID effect wrapper resolves its chain by flushing pending
	 *  commands first, so this mutator drains the full queue before enqueueing
	 *  the switch. The edit must still land, with no new drop or refusal. */
	void anEffectSwitchDrainsPendingCommandsBeforeQueueing()
	{
		QVERIFY(m_effect != nullptr);
		const bool originalEnabled = m_effect->isEnabled();

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		fillCommandQueue();

		const QString channelId = ScriptDawBindings::newMixerChannel(
			Engine::mixer()->mixerChannel(m_channel)).id();
		ScriptDawBindings::newEffect(channelId, m_effect).setEnabled(!originalEnabled);

		const QStringList logs = engine->takeLogMessages();
		QVERIFY2(logs.isEmpty(), qPrintable(logs.join(QStringLiteral(" | "))));
		QCOMPARE(engine->pendingCommandCount(), 1);
		QCOMPARE(engine->processCommands(), 1);
		QCOMPARE(m_effect->isEnabled(), !originalEnabled);
	}
};

QTEST_MAIN(ScriptDawQueueFullTest)
#include "ScriptDawQueueFullTest.moc"
