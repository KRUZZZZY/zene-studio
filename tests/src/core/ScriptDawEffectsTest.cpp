/*
 * ScriptDawEffectsTest.cpp - a Lua script reads the effects on a mixer channel's chain
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
 *
 */

/*! The Lua `EffectChain` and `Effect` wrappers (ScriptDawEffects.cpp): a script addresses a channel's chain,
 *  counts and fetches its devices by position and by id, and reads one device's plug-in name, enabled flag and
 *  parameters - the same parameter list plugin.param_get reads. The device here is the rack tests' own parameter
 *  effect appended to a real channel's chain, so no plug-in module is needed. What a script holds is a wrapper, not
 *  the device: a position past the end is an invalid wrapper whose reads are empty, and a wrapper kept across the
 *  device's removal stops reading it rather than dangling. */

#include <QtTest>

#include <QStringList>

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "RackTestSupport.h"
#include "ScriptEngine.h"

using namespace lmms;

namespace
{

QString run(const QString& code, QStringList* logs)
{
	ScriptEngine* engine = ScriptEngine::instance();
	QString error;
	const auto result = engine->runString(code, &error, QStringLiteral("=(effects test)"));
	*logs = engine->takeLogMessages();
	return result == ScriptEngine::RunResult::Ok ? error : QStringLiteral("script failed: ") + error;
}

} // namespace

class ScriptDawEffectsTest : public QObject
{
	Q_OBJECT

	int m_channel = -1;
	racktest::RackParamEffect* m_effect = nullptr;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_channel = Engine::mixer()->createChannel();
		m_effect = new racktest::RackParamEffect(&Engine::mixer()->mixerChannel(m_channel)->m_fxChain);
		Engine::mixer()->mixerChannel(m_channel)->m_fxChain.appendEffect(m_effect);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aScriptReadsADeviceByPositionAndById()
	{
		QStringList logs;
		const QString error = run(QStringLiteral(R"LUA(
local chain = zene.mixer():channel(%1):chain()
print("valid=" .. tostring(chain:isValid()) .. " count=" .. chain:effectCount())
local fx = chain:effect(0)
print("fx valid=" .. tostring(fx:isValid()) .. " index=" .. fx:index() .. " id=" .. fx:id())
print("plugin=" .. fx:pluginName() .. " display=" .. fx:displayName() .. " enabled=" .. tostring(fx:enabled()))
local byId = chain:effectById(fx:id())
print("byId index=" .. byId:index() .. " plugin=" .. byId:pluginName())
)LUA").arg(m_channel), &logs);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(logs.size(), 4);
		QCOMPARE(logs.at(0), QStringLiteral("valid=true count=1"));
		QVERIFY(logs.at(1).startsWith(QStringLiteral("fx valid=true index=0 id=fx-")));
		QCOMPARE(logs.at(2), QStringLiteral("plugin=rackparamtest display=Rack macro test effect enabled=true"));
		QCOMPARE(logs.at(3), QStringLiteral("byId index=0 plugin=rackparamtest"));
	}

	void enabledFollowsTheDevicesOwnSwitch()
	{
		m_effect->setEnabled(false);
		QStringList logs;
		const QString error = run(QStringLiteral("print(tostring(zene.mixer():channel(%1):chain():effect(0):enabled()))").arg(m_channel), &logs);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(logs, QStringList{QStringLiteral("false")});
		m_effect->setEnabled(true);
	}

	void theParametersAreTheOnesPluginParamGetReads()
	{
		// The list plugin.param_get reads, in its order: the device's own two after the common effect models.
		const QList<AutomatableModel*> models = controlEffectParameters(m_effect);
		QVERIFY(models.size() >= 2);
		int gain = -1;
		for (int i = 0; i < models.size(); ++i)
		{
			if (models.at(i)->displayName() == QLatin1String("Gain")) { gain = i; }
		}
		QVERIFY2(gain >= 0, "the Gain parameter is not in the list");

		QStringList logs;
		const QString error = run(QStringLiteral(R"LUA(
local fx = zene.mixer():channel(%1):chain():effect(0)
print("count=" .. fx:parameterCount())
print("name=" .. fx:parameterName(%2))
print("outside=[" .. fx:parameterName(%3) .. "][" .. fx:parameterName(-1) .. "]")
local gain = fx:parameter(%2)
print("gain valid=" .. tostring(gain:isValid()) .. " value=" .. gain:value() .. " min=" .. gain:minValue() .. " max=" .. gain:maxValue())
gain:setValue(75)
print("after=" .. gain:value())
print("outside valid=" .. tostring(fx:parameter(%3):isValid()))
)LUA").arg(m_channel).arg(gain).arg(models.size()), &logs);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(logs.size(), 6);
		QCOMPARE(logs.at(0), QStringLiteral("count=%1").arg(models.size()));
		QCOMPARE(logs.at(1), QStringLiteral("name=Gain"));
		QCOMPARE(logs.at(2), QStringLiteral("outside=[][]"));
		QCOMPARE(logs.at(3), QStringLiteral("gain valid=true value=50.0 min=0.0 max=100.0"));
		QCOMPARE(logs.at(4), QStringLiteral("after=75.0"));
		QCOMPARE(logs.at(5), QStringLiteral("outside valid=false"));
		// The engine's own model carries what the script wrote.
		QCOMPARE(models.at(gain)->value<float>(), 75.f);
	}

	void aPositionPastTheEndIsAnInvalidWrapperWhoseReadsAreEmpty()
	{
		QStringList logs;
		const QString error = run(QStringLiteral(R"LUA(
local chain = zene.mixer():channel(%1):chain()
local none = chain:effect(7)
local neg = chain:effect(-1)
print("valid=" .. tostring(none:isValid()) .. "/" .. tostring(neg:isValid()))
print("index=" .. none:index() .. " id=[" .. none:id() .. "] plugin=[" .. none:pluginName() .. "] display=[" .. none:displayName() .. "]")
print("enabled=" .. tostring(none:enabled()) .. " params=" .. none:parameterCount() .. " pname=[" .. none:parameterName(0) .. "]")
print("badId=" .. tostring(chain:effectById("fx-999999"):isValid()) .. " junk=" .. tostring(chain:effectById("junk"):isValid()))
)LUA").arg(m_channel), &logs);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(logs.size(), 4);
		QCOMPARE(logs.at(0), QStringLiteral("valid=false/false"));
		QCOMPARE(logs.at(1), QStringLiteral("index=-1 id=[] plugin=[] display=[]"));
		QCOMPARE(logs.at(2), QStringLiteral("enabled=false params=0 pname=[]"));
		QCOMPARE(logs.at(3), QStringLiteral("badId=false junk=false"));
	}

	//! A script may remove a device and then read the wrapper it kept: that wrapper reads nothing, it does not dangle.
	void aWrapperKeptAcrossTheDevicesRemovalReadsNothing()
	{
		const int channel = Engine::mixer()->createChannel();
		auto* effect = new racktest::RackParamEffect(&Engine::mixer()->mixerChannel(channel)->m_fxChain);
		Engine::mixer()->mixerChannel(channel)->m_fxChain.appendEffect(effect);
		QStringList logs;
		const QString error = run(QStringLiteral(R"LUA(
local chain = zene.mixer():channel(%1):chain()
local fx = chain:effect(0)
print("before valid=" .. tostring(fx:isValid()) .. " plugin=" .. fx:pluginName())
chain:removeEffect(0)
print("after valid=" .. tostring(fx:isValid()) .. " id=[" .. fx:id() .. "] plugin=[" .. fx:pluginName() .. "] params=" .. fx:parameterCount() .. " count=" .. chain:effectCount())
)LUA").arg(channel), &logs);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(logs.size(), 2);
		QCOMPARE(logs.at(0), QStringLiteral("before valid=true plugin=rackparamtest"));
		QCOMPARE(logs.at(1), QStringLiteral("after valid=false id=[] plugin=[] params=0 count=0"));
		QVERIFY(Engine::mixer()->mixerChannel(channel)->m_fxChain.effects().empty());
	}

	/*! A script may remove the CHANNEL that owns a device and then read the
	 *  device view it kept: the view names the device by the channel's stable
	 *  id and the device's own id, so it reports itself invalid instead of
	 *  reading a chain that is gone (cluster 1663). The chain and the device
	 *  died with the channel, so a view that still held their pointers would
	 *  read freed memory.
	 *
	 *  Two devices on the chain: the view of the SECOND one is the one the
	 *  script keeps, so the check does not depend on which slot the freed
	 *  chain's own bookkeeping still holds. */
	void aDeviceViewOfARemovedChannelReadsNothing()
	{
		const int channel = Engine::mixer()->createChannel();
		auto* chain = &Engine::mixer()->mixerChannel(channel)->m_fxChain;
		chain->appendEffect(new racktest::RackParamEffect(chain));
		chain->appendEffect(new racktest::RackParamEffect(chain));
		QCOMPARE(static_cast<int>(chain->effects().size()), 2);

		QStringList logs;
		const QString error = run(QStringLiteral(R"LUA(
local mixer = zene.mixer()
local channel = mixer:channel(%1)
local id = channel:id()
local fx = channel:chain():effect(1)
print("before valid=" .. tostring(fx:isValid()) .. " plugin=" .. fx:pluginName())
zene.command("mixer.remove_channel", {channel = id})
print("after valid=" .. tostring(fx:isValid()) .. " index=" .. fx:index())
)LUA").arg(channel), &logs);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(logs.size(), 2);
		QCOMPARE(logs.at(0), QStringLiteral("before valid=true plugin=rackparamtest"));
		QCOMPARE(logs.at(1), QStringLiteral("after valid=false index=-1"));
	}
};

QTEST_MAIN(ScriptDawEffectsTest)
#include "ScriptDawEffectsTest.moc"
