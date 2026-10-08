/*
 * ScriptDawBindingTest.cpp - the Lua API's DAW-control binding, proved on a
 * REAL engine (0.3.0 feature row 50: "drives a real mixer/channel change from
 * Lua and reads it back").
 *
 * What this test refuses to accept as proof:
 *   - a wrapper that reports what the script wrote out of its own buffer. Every
 *     assertion below reads the ENGINE (Mixer::mixerChannel, the channel's own
 *     models, the chain's effect list), and separately checks that the script's
 *     own read-back agrees with it;
 *   - a second, private notion of a channel. The id the Lua binding returns is
 *     resolved back through the control surface's own resolveControlTarget(), so
 *     a binding that invented its own ch-<n> would fail here;
 *   - a version string that is only prose. zene.apiSurface() is compared against
 *     the version this test translation unit was compiled with, both from the
 *     same CMake variable (docs/LUA-COMPATIBILITY-POLICY.md, section 1).
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
#include "ControlStructuralSupport.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "ScriptApiVersion.h"
#include "ScriptEngine.h"

using namespace lmms;

namespace
{

QString qstr(const char* text) { return QString::fromUtf8(text); }

//! Create a channel, drive gain/mute/solo and the name, read all of it back.
const char* const ChannelScript = R"LUA(
local mixer = zene.mixer()
local channel = mixer:addChannel()
channel:setGain(0.5)
channel:setSoloed(true)
channel:setMuted(true)
channel:setName("lua-driven")
print("created id=" .. channel:id()
    .. " gain=" .. channel:gain()
    .. " muted=" .. tostring(channel:muted())
    .. " soloed=" .. tostring(channel:soloed()))
local again = mixer:channelById(channel:id())
print("readback name=" .. again:name() .. " index=" .. again:index())
print("master=" .. tostring(mixer:master():isMaster()) .. " count=" .. mixer:channelCount())
print("pan is deliberately absent: " .. tostring(again.pan))
)LUA";

//! Move the fader of one channel and report what the engine says afterwards.
const char* const GainScript = R"LUA(
local channel = zene.mixer():channel(%1)
channel:setGain(0.25)
print("gain now " .. channel:gain())
)LUA";

//! The model accessors must cross the bridge as the REGISTERED model classes:
//! a raw AutomatableModel* is not a registered class, so the call raises.
const char* const ModelAccessorScript = R"LUA(
local channel = zene.mixer():channel(%1)
local gain = channel:gainModel()
print("gain isValid=" .. tostring(gain:isValid())
    .. " value=" .. gain:value()
    .. " min=" .. gain:minValue()
    .. " max=" .. gain:maxValue()
    .. " name=" .. gain:name()
    .. " type=" .. gain:type())
gain:setValue(0.75)
print("gain after setValue=" .. gain:value())
local mute = channel:muteModel()
local solo = channel:soloModel()
print("mute isValid=" .. tostring(mute:isValid()) .. " value=" .. tostring(mute:value()))
print("solo isValid=" .. tostring(solo:isValid()) .. " name=" .. solo:name())
)LUA";

//! The effect-chain half: address the chain, refuse a bad device load.
const char* const ChainScript = R"LUA(
local channel = zene.mixer():channel(%1)
local chain = channel:chain()
print("chain effects=" .. chain:effectCount())
local refused = chain:loadEffect("this-is-not-a-device")
print("refused isValid=" .. tostring(refused:isValid()))
print("chain effects after refusal=" .. chain:effectCount())
)LUA";

//! Every id mixer:ids() lists must be resolvable, and must be the id the
//! channel itself publishes - the vocabulary channelById() understands.
const char* const IdScript = R"LUA(
local mixer = zene.mixer()
local listed = mixer:ids()
local unresolved = 0
for i, id in ipairs(listed) do
  if not mixer:channelById(id):isValid() then unresolved = unresolved + 1 end
end
print("listed " .. #listed .. " unresolved " .. unresolved)
)LUA";

} // namespace

class ScriptDawBindingTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		// The build's own effect modules (BassBooster for the undo test below),
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

	/*! The headline proof: a Lua script creates a mixer channel, drives its
	 *  gain, mute, solo and name, and the ENGINE - not the script - shows every
	 *  one of those changes, under the id the control surface resolves. */
	void luaDrivesARealMixerChannelAndReadsItBack()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		const int before = static_cast<int>(mixer->numChannels());

		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(ChannelScript), &error, qstr("=(daw binding)"))),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		// 1. The engine grew a channel: the mixer was actually changed.
		QCOMPARE(static_cast<int>(mixer->numChannels()), before + 1);

		// 2. The channel the script addressed is the one the socket addresses:
		//    the control surface's own resolver must find the same object. By the
		//    id the ENGINE gave it - a ch-<n> id is allocated at construction and
		//    is not the channel's index (SPEC-stable-ids.md slice 2).
		MixerChannel* channel = mixer->mixerChannel(before);
		const QString channelId = control::channelIdOf(channel);
		ControlTarget target;
		ControlResult result;
		QVERIFY2(resolveControlTarget(channelId, &target, &result),
			qPrintable(result.errorMessage));
		QCOMPARE(target.chain, &channel->m_fxChain);

		// 3. The engine's own models carry what the script wrote.
		QCOMPARE(channel->m_volumeModel.value(), 0.5f);
		QVERIFY(channel->m_muteModel.value());
		QVERIFY(channel->m_soloModel.value());
		QCOMPARE(channel->m_name, qstr("lua-driven"));

		// 4. ... and the script's read-back agrees with the engine, line by line.
		const QStringList expected = {
			qstr("created id=%1 gain=0.5 muted=true soloed=true").arg(channelId),
			qstr("readback name=lua-driven index=%1").arg(before),
			qstr("master=true count=%1").arg(before + 1),
			qstr("pan is deliberately absent: nil"),
		};
		QCOMPARE(engine->takeLogMessages(), expected);
	}

	//! An overlong UTF-8 name must be refused intact, not split by the queue payload.
	void anOverlongChannelNameIsRejectedWithoutChangingTheEngine()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		const int index = static_cast<int>(mixer->numChannels());
		mixer->createChannel();
		MixerChannel* channel = mixer->mixerChannel(index);
		QVERIFY(channel != nullptr);
		const QString originalName = channel->m_name;

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		const QString script = qstr("local c = zene.mixer():channel(%1)\n"
			"c:setName(string.rep(string.char(195, 169), 128))\n").arg(index);
		QString error;
		QCOMPARE(int(engine->runString(script, &error)), int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		QCOMPARE(channel->m_name, originalName);
		const QStringList logs = engine->takeLogMessages();
		QCOMPARE(logs.size(), 1);
		QVERIFY(logs.front().contains(qstr("255-byte")));
	}

	//! The same payload limit applies to the device reference used by loadEffect.
	void anOverlongDeviceReferenceIsRejectedWithoutChangingTheChain()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(mixer != nullptr);
		const int index = static_cast<int>(mixer->numChannels());
		mixer->createChannel();
		EffectChain& chain = mixer->mixerChannel(index)->m_fxChain;
		const int effectsBefore = static_cast<int>(chain.effects().size());

		ScriptEngine* engine = ScriptEngine::instance();
		engine->takeLogMessages();
		const QString script = qstr("local chain = zene.mixer():channel(%1):chain()\n"
			"local effect = chain:loadEffect(string.rep('x', 256))\n"
			"print('long reference valid=' .. tostring(effect:isValid()))\n").arg(index);
		QString error;
		QCOMPARE(int(engine->runString(script, &error)), int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		QCOMPARE(static_cast<int>(chain.effects().size()), effectsBefore);
		const QStringList logs = engine->takeLogMessages();
		QVERIFY(logs.contains(qstr("long reference valid=false")));
		QCOMPARE(logs.filter(qstr("255-byte")).size(), 1);
	}

	/*! Writes are journalled through the channel's own models, so the GUI's
	 *  Ctrl+Z and control.undo meet the same history the manual path leaves:
	 *  one undo restores the fader the script moved, and the channel survives. */
	void aScriptsGainWriteIsUndoableThroughTheProjectJournal()
	{
		// This slot creates its own channel, so the journal's newest entries
		// belong to it whatever the slots above did.
		ScriptEngine* engine = ScriptEngine::instance();
		Mixer* mixer = Engine::mixer();
		const int index = static_cast<int>(mixer->numChannels());
		QString error;
		QCOMPARE(int(engine->runString(qstr("local c = zene.mixer():addChannel()\n"
			"c:setGain(0.5)\nprint(\"setup \" .. c:id())\n"), &error)),
			int(ScriptEngine::RunResult::Ok));
		QCOMPARE(static_cast<int>(mixer->numChannels()), index + 1);

		QCOMPARE(int(engine->runString(qstr(GainScript).arg(index), &error)),
			int(ScriptEngine::RunResult::Ok));
		QCOMPARE(mixer->mixerChannel(index)->m_volumeModel.value(), 0.25f);

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QVERIFY(journal->canUndo());
		journal->undo();
		QVERIFY2(static_cast<int>(mixer->numChannels()) > index,
			"the undo removed the channel instead of restoring the fader");
		QCOMPARE(mixer->mixerChannel(index)->m_volumeModel.value(), 0.5f);
	}

	/*! The model accessors are the instrument-track bindings' shape: a pooled,
	 *  REGISTERED wrapper (FloatModel / BoolModel), never a raw
	 *  AutomatableModel*. A raw model pointer is not a registered class, so
	 *  `channel:gainModel()` raised "The class is not registered in LuaBridge"
	 *  and nothing a script did with the result was reachable. */
	void modelAccessorsReturnRegisteredLuaWrappers()
	{
		Mixer* mixer = Engine::mixer();
		const int index = static_cast<int>(mixer->numChannels()) - 1;
		MixerChannel* channel = mixer->mixerChannel(index);
		const float gainBefore = channel->m_volumeModel.value();

		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(ModelAccessorScript).arg(index), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		const QStringList logs = engine->takeLogMessages();
		const QString gainLine = logs.value(0);
		QVERIFY2(gainLine.startsWith(qstr("gain isValid=true value=")),
			qPrintable(gainLine));
		// The wrapper read the ENGINE's live value, not a stale or invented one.
		QCOMPARE(gainLine.section(" value=", 1, 1).section(" ", 0, 0).toFloat(), gainBefore);
		QVERIFY2(logs.contains(qstr("gain after setValue=0.75")),
			qPrintable(logs.join(QStringLiteral(" | "))));
		QVERIFY2(logs.contains(qstr("mute isValid=true value=")
			+ (channel->m_muteModel.value() ? qstr("true") : qstr("false"))),
			qPrintable(logs.join(QStringLiteral(" | "))));
		QVERIFY2(logs.contains(qstr("solo isValid=true name=") + channel->m_soloModel.displayName()),
			qPrintable(logs.join(QStringLiteral(" | "))));

		// The wrapper's write reached the engine's own model, not just Lua.
		QCOMPARE(channel->m_volumeModel.value(), 0.75f);
	}

	/*! The effect-chain half of the binding: the chain a Lua script reaches is
	 *  the SAME EffectChain the control surface's plugin.* group loads into, and
	 *  a refused load changes nothing and says why through the script console. */
	void theChainBindingIsTheSocketAddressableChain()
	{
		Mixer* mixer = Engine::mixer();
		const int index = static_cast<int>(mixer->numChannels()) - 1;
		MixerChannel* channel = mixer->mixerChannel(index);
		const int effectsBefore = static_cast<int>(channel->m_fxChain.effects().size());

		// The control surface resolves the channel's OWN id to this chain and no
		// other - not ch-<index>, which is a position (SPEC-stable-ids.md slice 2).
		ControlTarget target;
		ControlResult result;
		QVERIFY2(resolveControlTarget(control::channelIdOf(channel), &target, &result),
			qPrintable(result.errorMessage));
		QCOMPARE(target.chain, &channel->m_fxChain);

		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(ChainScript).arg(index), &error)),
			int(ScriptEngine::RunResult::Ok));

		// The refused load changed nothing in the chain the engine owns.
		QCOMPARE(static_cast<int>(channel->m_fxChain.effects().size()), effectsBefore);

		const QStringList logs = engine->takeLogMessages();
		QVERIFY(logs.contains(qstr("chain effects=%1").arg(effectsBefore)));
		QVERIFY(logs.contains(qstr("chain effects after refusal=%1").arg(effectsBefore)));
		QVERIFY(logs.contains(qstr("refused isValid=false")));
		// The refusal is not silent: the apply side logged why, on the same
		// console a script's own print() reaches.
		QVERIFY(logs.filter(qstr("refused")).size() >= 2);
	}

	/*! A script's loadEffect is one undo step that finds its chain again by
	 *  the channel's id: undo takes the device out and redo puts it back, and
	 *  once the channel is deleted the step does nothing. It used to keep the
	 *  EffectChain* and the Effect*, which the deletion freed. */
	void aScriptsLoadedEffectIsUndoneOnTheChannelItNames()
	{
		QString device;
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		for (int i = 0; i < catalogue.size() && device.isEmpty(); ++i)
		{
			if (catalogue.at(i).name == qstr("amplifier") && catalogue.at(i).loadable) { device = control::deviceId(i); }
		}
		if (device.isEmpty()) { QSKIP("this build has no amplifier module"); }
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr("zene.mixer():channel(%1):chain():loadEffect(\"%2\")\n").arg(index).arg(device),
			&error)), int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		const EffectChain& chain = mixer->mixerChannel(index)->m_fxChain;
		QCOMPARE(static_cast<int>(chain.effects().size()), 1);
		ProjectJournal* journal = Engine::projectJournal();
		journal->undo();
		QVERIFY(chain.effects().empty());
		journal->redo();
		QCOMPARE(static_cast<int>(chain.effects().size()), 1);
		mixer->deleteChannel(index);
		journal->undo();  // its channel is gone: nothing to take out, and nothing freed is touched
		QCOMPARE(static_cast<int>(mixer->numChannels()), index);
		engine->takeLogMessages();
	}

	/*! mixer:ids() must publish the CHANNEL ids channel:id() publishes, because
	 *  those are the ids channelById() resolves. It used to name positions
	 *  (channelId(index)), so enumerating ids() and feeding the entries back into
	 *  channelById() addressed nothing: the master is ch-1 but ids() listed ch-0,
	 *  and a ch-<n> number is an allocated id, never an index. */
	void mixerIdsListsTheChannelsOwnIdsAndEveryOneResolves()
	{
		Mixer* mixer = Engine::mixer();
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(IdScript), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		// The mixer really has that many channels, and every id ids() listed is
		// one a channel really carries.
		const int count = static_cast<int>(mixer->numChannels());
		for (int i = 0; i < count; ++i)
		{
			MixerChannel* channel = mixer->mixerChannel(i);
			const QString id = control::channelIdOf(channel);
			ControlTarget target;
			ControlResult result;
			QVERIFY2(resolveControlTarget(id, &target, &result),
				qPrintable(result.errorMessage));
			QCOMPARE(target.chain, &channel->m_fxChain);
		}

		// And the binding agrees: it listed every channel, and channelById()
		// resolved every entry back to a live channel. A positional list listed
		// "ch-0" for the master, which resolves to nothing at all.
		QCOMPARE(engine->takeLogMessages(), QStringList{
			qstr("listed %1 unresolved 0").arg(count)});
	}

	/*! The version policy's Lua face: the surface this build reports is the
	 *  version this translation unit was compiled with, and the DAW-control
	 *  namespace entry is visible in it. */
	void apiSurfaceReportsTheBuiltVersionAndTheDawBinding()
	{
		ScriptEngine* engine = ScriptEngine::instance();
		QString error;
		QCOMPARE(int(engine->runString(qstr(
			"local s = zene.apiSurface()\n"
			"print(\"version \" .. s.version .. \" full \" .. s.full_version)\n"
			"print(\"major \" .. s.major .. \" minor \" .. s.minor)\n"
			"print(\"stability \" .. s.stability)\n"
			"print(\"mixer \" .. s.has_mixer_binding .. \" functions \" .. s.function_count)\n"),
			&error)), int(ScriptEngine::RunResult::Ok));

		const QStringList logs = engine->takeLogMessages();
		QCOMPARE(logs.size(), 4);
		QCOMPARE(logs.at(0), qstr("version %1 full %2")
			.arg(ScriptApi::version(), ScriptApi::fullVersion()));
		QCOMPARE(logs.at(1), qstr("major %1 minor %2")
			.arg(ScriptApi::major()).arg(ScriptApi::minor()));
		QCOMPARE(logs.at(2), qstr("stability %1").arg(ScriptApi::stability()));
		QVERIFY(logs.at(3).startsWith(qstr("mixer 1 functions ")));
	}
};

QTEST_MAIN(ScriptDawBindingTest)
#include "ScriptDawBindingTest.moc"
