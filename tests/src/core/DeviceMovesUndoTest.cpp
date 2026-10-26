/*
 * DeviceMovesUndoTest.cpp - undo resolves rebuilt device chains and effects
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QPointer>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlDeviceMoves.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString load(const QString& target, const QString& name)
{
	const auto devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& value : devices)
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("name")).toString() != name) { continue; }
		return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.value(QStringLiteral("id"))}}).result.value(QStringLiteral("id")).toString();
	}
	return {};
}

EffectChain* chain(const QString& target)
{
	ControlTarget resolved;
	ControlResult error;
	return resolveControlTarget(target, &resolved, &error) ? resolved.chain : nullptr;
}

QStringList names(const QString& target)
{
	QStringList result;
	if (const EffectChain* current = chain(target))
	{
		for (const Effect* effect : current->effects()) { result << QString::fromUtf8(effect->descriptor()->name); }
	}
	return result;
}

} // namespace

class DeviceMovesUndoTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void copiedDevicesAreUndoneOnARebuiltTrack_data()
	{
		QTest::addColumn<QString>("command");
		QTest::newRow("plugin.copy") << QStringLiteral("plugin.copy");
		QTest::newRow("chain.copy") << QStringLiteral("chain.copy");
	}

	void copiedDevicesAreUndoneOnARebuiltTrack()
	{
		QFETCH(QString, command);
		const QString source = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		const QString target = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		const QString plugin = load(source, QStringLiteral("amplifier"));
		QVERIFY2(!plugin.isEmpty(), "the amplifier fixture must be built");
		QVERIFY2(!load(target, QStringLiteral("bassbooster")).isEmpty(), "the bassbooster fixture must be built");
		QJsonObject args{{QStringLiteral("target"), source}, {QStringLiteral("to"), target}};
		if (command == QLatin1String("plugin.copy")) { args.insert(QStringLiteral("plugin"), plugin); }
		QVERIFY(run(command, args).ok);
		QCOMPARE(names(target), (QStringList{QStringLiteral("bassbooster"), QStringLiteral("amplifier")}));
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), target}}).ok);
		QVERIFY(chain(target) == nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(chain(target) != nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(names(target), QStringList{QStringLiteral("bassbooster")});
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(names(target), (QStringList{QStringLiteral("bassbooster"), QStringLiteral("amplifier")}));
		QVERIFY(run(QStringLiteral("control.redo")).ok);  // remove the track again
		QVERIFY(chain(target) == nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(names(target), QStringList{QStringLiteral("bassbooster")});
	}

	void aWrappedChannelCanDisappearBeforeUndo()
	{
		const int index = Engine::mixer()->createChannel();
		const QString target = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		QVERIFY(!load(target, QStringLiteral("amplifier")).isEmpty());
		QVERIFY(run(QStringLiteral("rack.wrap"), {{QStringLiteral("channel"), target}}).ok);
		Engine::mixer()->deleteChannel(index);
		QVERIFY(chain(target) == nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(chain(target) == nullptr);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(chain(target) == nullptr);
	}

	//! A chain that cannot be found (the finder answers nullptr, as for a deleted chain) removes nothing and
	//! dereferences nothing: the devices stay where they are.
	void removeDevicesRecordedToleratesAMissingChain()
	{
		const QString target = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		QVERIFY(!load(target, QStringLiteral("amplifier")).isEmpty());
		const EffectChain* original = chain(target);
		QVERIFY(original != nullptr);
		const std::vector<Effect*> devices = original->effects();
		QVERIFY(!devices.empty());
		control::removeDevicesRecorded([]() -> EffectChain* { return nullptr; }, devices);
		QCOMPARE(names(target), QStringList{QStringLiteral("amplifier")});
		control::removeDevicesRecorded(static_cast<EffectChain*>(nullptr), devices);
		QCOMPARE(names(target), QStringList{QStringLiteral("amplifier")});
	}

	void thePointerOverloadFindsARebuiltChain()
	{
		const QString target = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		QVERIFY(!load(target, QStringLiteral("amplifier")).isEmpty());
		QPointer<EffectChain> original = chain(target);
		QVERIFY(!original.isNull());
		const std::vector<Effect*> devices = original->effects();
		control::removeDevicesRecorded(original.data(), devices);
		QVERIFY(names(target).isEmpty());
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), target}}).ok);
		// Occupy the retired allocations: an undo must work when a rebuilt chain has a different address.
		Track* intervening = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(intervening != nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(original.isNull());
		QVERIFY(chain(target) != nullptr);
		Engine::projectJournal()->undo();  // the helper's step, without a registry transaction
		QCOMPARE(names(target), QStringList{QStringLiteral("amplifier")});
		Engine::projectJournal()->redo();
		QVERIFY(names(target).isEmpty());
	}
};

QTEST_MAIN(DeviceMovesUndoTest)
#include "DeviceMovesUndoTest.moc"
