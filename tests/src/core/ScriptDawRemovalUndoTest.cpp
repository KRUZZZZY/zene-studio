/*
 * ScriptDawRemovalUndoTest.cpp - what a script's effect removal, and the
 * recorded plugin.unload, put back on undo (Luna 5332 and its rework, 51e7da51f).
 *
 * Every case runs on a REAL engine: the removal goes through the Lua binding or
 * the control registry, and undo and redo are the project journal's own. The
 * assertions read the chain the engine holds, not the script's output, and each
 * checks the journal's undo depth, so a spurious checkpoint fails the case.
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
 */

#include <QtTest>
#include <QStringList>
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "ScriptEngine.h"

using namespace lmms;

namespace
{

QString qstr(const char* text) { return QString::fromUtf8(text); }


//! Load a device into a chain and remove it again through the binding.
const char* const RemoveEffectScript = R"LUA(
local chain = zene.mixer():channel(%1):chain()
chain:loadEffect("%2")
print("loaded " .. chain:effectCount())
chain:removeEffect(0)
print("removed " .. chain:effectCount())
)LUA";

} // namespace

class ScriptDawRemovalUndoTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		// The build's own effect modules (BassBooster for the cases below),
		// found the way RackMenuTest finds them. Set before Engine::init scans.
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/*! The first effect module this build lists as loadable, or false. */
	static bool loadableDevice(QString* device, QString* deviceName)
	{
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		for (int i = 0; i < catalogue.size(); ++i)
		{
			if (catalogue.at(i).loadable)
			{
				*device = control::deviceId(i);
				*deviceName = catalogue.at(i).name;
				return true;
			}
		}
		return false;
	}


	/*! chain:removeEffect is one undo step, like the load beside it: undo puts
	 *  the device back with its settings, redo takes it out again. The removal
	 *  used to be journalless, so the undo fell on the PREVIOUS step and the
	 *  device was gone for good. */
	void aScriptsRemovedEffectIsUndoneByItsOwnStep()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(RemoveEffectScript).arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		// The script really removed it: the chain is empty now.
		QVERIFY(chain->effects().empty());

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(QString::fromUtf8(chain->effects().front()->descriptor()->name), deviceName);
		journal->redo();
		QVERIFY(chain->effects().empty());

		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! Luna 5332, at the undo cap. With the stack full, the removal's own steps
	 *  push the stack's oldest steps out. The merge used to be named by an index
	 *  taken before those pushes, so it could be a silent no-op and one undo
	 *  restored only the enabled flag. Named by serial it folds the removal's
	 *  steps into one: one undo brings the device back, re-enables the chain,
	 *  and leaves the step pushed before the script untouched. */
	void aScriptsRemovedEffectIsUndoneOnceAtTheUndoCap()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int oldCap = journal->maxUndoStates();
		constexpr int cap = 8;
		QVERIFY(journal->setMaxUndoStates(cap));

		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;

		// Fill the stack to the cap with steps that touch nothing; the last one
		// is the sentinel, the step the script's own steps must not reach.
		auto preceding = std::make_shared<int>(0);
		while (journal->undoDepth() < cap - 1)
		{
			control::addStructuralUndoStep([] {}, [] {}, 0);
		}
		control::addStructuralUndoStep([preceding] { ++*preceding; }, [] {}, 0);
		QCOMPARE(journal->undoDepth(), cap);

		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(RemoveEffectScript).arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());

		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(QString::fromUtf8(chain->effects().front()->descriptor()->name), deviceName);
		QVERIFY(chain->isEnabled());
		QCOMPARE(*preceding, 0);

		journal->redo();
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());

		QVERIFY(journal->setMaxUndoStates(oldCap));
		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! Luna 5332, with the tightest bound the journal accepts (one step). The
	 *  removal's structural step must be the step that survives: a second
	 *  checkpoint pushed after it (the emptied chain switching off) would evict
	 *  it, and one undo would restore nothing. One undo brings the device back
	 *  and the chain is enabled again; one redo removes it again. */
	void aScriptsRemovedEffectIsUndoneOnceAtCapOne()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int oldCap = journal->maxUndoStates();
		QVERIFY(journal->setMaxUndoStates(1));

		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;

		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(RemoveEffectScript).arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 1);

		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(QString::fromUtf8(chain->effects().front()->descriptor()->name), deviceName);
		QVERIFY(chain->isEnabled());

		journal->redo();
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());

		QVERIFY(journal->setMaxUndoStates(oldCap));
		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! A chain switched off, then its only device removed by a script: one undo
	 *  must put the device back AND leave the chain off (appending a device
	 *  switches a chain on, so the undo has to restore the flag it recorded), and
	 *  the undo must not push a checkpoint of its own. */
	void aScriptsRemovedLastEffectOfADisabledChainStaysDisabledOnUndo()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(
			"zene.mixer():channel(%1):chain():loadEffect(\"%2\")\n").arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);

		chain->setEnabledUnjournalled(false);
		journal->clearJournal();

		QCOMPARE(int(engine->runString(qstr(
			"zene.mixer():channel(%1):chain():removeEffect(0)\n").arg(index), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 1);

		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(QString::fromUtf8(chain->effects().front()->descriptor()->name), deviceName);
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 0);

		journal->redo();
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());

		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! The same case through plugin.unload, which shares the recorded removal:
	 *  one undo restores the device, the chain stays off, and no checkpoint is
	 *  left behind. */
	void aPluginUnloadOfTheLastEffectOfADisabledChainStaysDisabledOnUndo()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		const QString channel = control::channelIdOf(mixer->mixerChannel(index));
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(
			"zene.mixer():channel(%1):chain():loadEffect(\"%2\")\n").arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);

		// plugin.unload names the device by its engine id (fx-<id>), not its position.
		const QString fx = QStringLiteral("fx-%1").arg(chain->effects().front()->id());
		chain->setEnabledUnjournalled(false);
		journal->clearJournal();

		QCOMPARE(int(engine->runString(qstr(
			"local reply, err = zene.command('plugin.unload', {target = '%1', plugin = '%2'})\n"
			"print(reply ~= nil and 'unloaded' or ('refused ' .. tostring(err)))\n").arg(channel, fx), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(engine->takeLogMessages().contains(qstr("unloaded")));
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 1);

		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(QString::fromUtf8(chain->effects().front()->descriptor()->name), deviceName);
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 0);

		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! An enabled chain's only device removed through plugin.unload: one undo
	 *  brings the device back, the chain is on again, and no checkpoint is left. */
	void aPluginUnloadOfTheLastEffectOfAnEnabledChainRestoresItOnUndo()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		const QString channel = control::channelIdOf(mixer->mixerChannel(index));
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(
			"zene.mixer():channel(%1):chain():loadEffect(\"%2\")\n").arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(chain->isEnabled());
		const QString fx = QStringLiteral("fx-%1").arg(chain->effects().front()->id());
		journal->clearJournal();

		QCOMPARE(int(engine->runString(qstr(
			"local reply, err = zene.command('plugin.unload', {target = '%1', plugin = '%2'})\n"
			"print(reply ~= nil and 'unloaded' or ('refused ' .. tostring(err)))\n").arg(channel, fx), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(engine->takeLogMessages().contains(qstr("unloaded")));
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());

		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(QString::fromUtf8(chain->effects().front()->descriptor()->name), deviceName);
		QVERIFY(chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 0);

		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! A chain with two devices, switched off, loses one by a script: the chain
	 *  stays non-empty, so the flag does not change, and one undo puts the device
	 *  back in its place with the chain still off and no checkpoint left. */
	void aScriptsRemovedEffectFromADisabledChainWithTwoStaysDisabledOnUndo()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(
			"local chain = zene.mixer():channel(%1):chain()\n"
			"chain:loadEffect(\"%2\")\n"
			"chain:loadEffect(\"%2\")\n").arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(static_cast<int>(chain->effects().size()), 2);

		chain->setEnabledUnjournalled(false);
		journal->clearJournal();

		QCOMPARE(int(engine->runString(qstr(
			"zene.mixer():channel(%1):chain():removeEffect(0)\n").arg(index), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QVERIFY(!chain->isEnabled());

		journal->undo();
		QCOMPARE(static_cast<int>(chain->effects().size()), 2);
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 0);

		mixer->deleteChannel(index);
		engine->takeLogMessages();
	}


	/*! The non-recorded branch of detachEffect (the capture-failure path) must
	 *  push nothing: a removal that claims no undo step cannot own a checkpoint
	 *  for the chain's enabled flag. Reached directly, since capture failure
	 *  needs a device over the snapshot limit, which this build cannot load. */
	void aNonRecordedDetachOfTheLastEffectPushesNoCheckpoint()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runString(qstr(
			"zene.mixer():channel(%1):chain():loadEffect(\"%2\")\n").arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(chain->isEnabled());
		Effect* effect = chain->effects().front();

		journal->clearJournal();
		control::detachEffect(chain, effect);
		effect->deleteLater();
		QVERIFY(chain->effects().empty());
		QVERIFY(!chain->isEnabled());
		QCOMPARE(journal->undoDepth(), 0);

		mixer->deleteChannel(index);
		ScriptEngine::instance()->takeLogMessages();
	}

};

QTEST_MAIN(ScriptDawRemovalUndoTest)
#include "ScriptDawRemovalUndoTest.moc"
