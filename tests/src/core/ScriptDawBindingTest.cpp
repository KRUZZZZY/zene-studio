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
channel:setMuted(true)
channel:setSoloed(true)
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

//! The effect-chain half: address the chain, refuse a bad device load.
const char* const ChainScript = R"LUA(
local channel = zene.mixer():channel(%1)
local chain = channel:chain()
print("chain effects=" .. chain:effectCount())
local refused = chain:loadEffect("this-is-not-a-device")
print("refused isValid=" .. tostring(refused:isValid()))
print("chain effects after refusal=" .. chain:effectCount())
)LUA";

} // namespace

class ScriptDawBindingTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
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
		//    the control surface's own resolver must find the same object.
		ControlTarget target;
		ControlResult result;
		QVERIFY2(resolveControlTarget(control::channelId(before), &target, &result),
			qPrintable(result.errorMessage));
		MixerChannel* channel = mixer->mixerChannel(before);
		QCOMPARE(target.chain, &channel->m_fxChain);

		// 3. The engine's own models carry what the script wrote.
		QCOMPARE(channel->m_volumeModel.value(), 0.5f);
		QVERIFY(channel->m_muteModel.value());
		QVERIFY(channel->m_soloModel.value());
		QCOMPARE(channel->m_name, qstr("lua-driven"));

		// 4. ... and the script's read-back agrees with the engine, line by line.
		const QStringList expected = {
			qstr("created id=ch-%1 gain=0.5 muted=true soloed=true").arg(before),
			qstr("readback name=lua-driven index=%1").arg(before),
			qstr("master=true count=%1").arg(before + 1),
			qstr("pan is deliberately absent: nil"),
		};
		QCOMPARE(engine->takeLogMessages(), expected);
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

	/*! The effect-chain half of the binding: the chain a Lua script reaches is
	 *  the SAME EffectChain the control surface's plugin.* group loads into, and
	 *  a refused load changes nothing and says why through the script console. */
	void theChainBindingIsTheSocketAddressableChain()
	{
		Mixer* mixer = Engine::mixer();
		const int index = static_cast<int>(mixer->numChannels()) - 1;
		MixerChannel* channel = mixer->mixerChannel(index);
		const int effectsBefore = static_cast<int>(channel->m_fxChain.effects().size());

		// The control surface resolves ch-<index> to this chain and no other.
		ControlTarget target;
		ControlResult result;
		QVERIFY2(resolveControlTarget(control::channelId(index), &target, &result),
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
