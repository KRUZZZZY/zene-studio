/*
 * DeviceAbUndoTest.cpp - plugin.ab stores and their undo after a device is recreated
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
/*! 0.5.0 L10 (review cluster 5639): a plugin.ab store is one undo step, and its undo and redo still find the
 *  device after plugin.unload and the undo that rebuilt it as a new object. */

#include <QtTest>

#include <QJsonArray>
#include <QPointer>

#include "../../../src/core/ControlDeviceTools.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

EffectChain& chainOf(int index) { return Engine::mixer()->mixerChannel(index)->m_fxChain; }

//! The first effect this build can load onto @a target: its fx-<n>, or empty.
QString loadAnEffect(const QString& target)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
		{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
	{
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}});
		if (loaded.ok) { return loaded.result.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! The first float parameter of @a plugin on @a target and a value inside its range away from the current one.
bool aParameter(const QString& target, const QString& plugin, QString* name, double* other)
{
	// plugin.param_get reads ONE parameter (by 'name' or 'index'): walk the indices until there is none.
	for (int index = 0; index < 256; ++index)
	{
		const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
			{QStringLiteral("plugin"), plugin}, {QStringLiteral("index"), index}});
		if (!read.ok) { break; }
		const QJsonObject p = read.result.value(QStringLiteral("parameter")).toObject();
		if (p.value(QStringLiteral("type")).toString() == QLatin1String("boolean")) { continue; }  // a float one, not "enabled"
		const double lo = p.value(QStringLiteral("min")).toDouble();
		const double hi = p.value(QStringLiteral("max")).toDouble();
		if (hi - lo <= 0.0) { continue; }
		*name = p.value(QStringLiteral("name")).toString();
		const double now = p.value(QStringLiteral("value")).toDouble();
		*other = std::abs(now - lo) > (hi - lo) * 0.25 ? lo : hi;
		return true;
	}
	return false;
}

double parameterValue(const QString& target, const QString& plugin, const QString& name)
{
	const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
		{QStringLiteral("plugin"), plugin}, {QStringLiteral("name"), name}});
	return read.ok ? read.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble() : -1e9;
}

} // namespace

class DeviceAbUndoTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A store's undo and redo after the device was unloaded and its unload undone (a new effect object): the
	//! undo and the redo must find the recreated device, not write the slots under the old id or owner.
	void aStoredSlotSurvivesTheDeviceBeingRecreated()
	{
		const int index = Engine::mixer()->createChannel();
		const QString target = ch(index);
		QString plugin = loadAnEffect(target);
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		QString name;
		double other = 0.0;
		QVERIFY(aParameter(target, plugin, &name, &other));
		const auto ab = [target, &plugin](const QString& action, const QString& slot = QString()) {
			QJsonObject args{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
				{QStringLiteral("action"), action}};
			if (!slot.isEmpty()) { args.insert(QStringLiteral("slot"), slot); }
			return run(QStringLiteral("plugin.ab"), args);
		};
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("name"), name}, {QStringLiteral("value"), other}}).ok);
		QVERIFY(ab(QStringLiteral("store"), QStringLiteral("a")).ok);                       // the store (step 1)
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);                                     // the unload: the device is back
		QCOMPARE(chainOf(index).effects().size(), std::size_t{1});
		const QString back = control::effectIdOf(chainOf(index).effects().front());
		QVERIFY(back != plugin);
		plugin = back;  // the recreated device has a new fx id
		QVERIFY(run(QStringLiteral("control.undo")).ok);                                     // the store is undone
		const ControlResult undone = ab(QStringLiteral("get"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(!undone.result.value(QStringLiteral("has_a")).toBool());
		QVERIFY(run(QStringLiteral("control.redo")).ok);                                     // and redone
		const ControlResult redone = ab(QStringLiteral("get"));
		QVERIFY2(redone.ok, qPrintable(redone.errorMessage));
		QVERIFY2(redone.result.value(QStringLiteral("has_a")).toBool(), "the redone store lost its slot on the recreated device");
		QVERIFY(ab(QStringLiteral("recall"), QStringLiteral("a")).ok);
		QVERIFY(std::abs(parameterValue(target, back, name) - other) < 1e-3);
		chainOf(index).clear();
	}

	//! Storing the same state in the selected slot twice does not create an undo step above the real store.
	void anUnchangedStoreDoesNotShieldThePreviousStoreUndo()
	{
		const int index = Engine::mixer()->createChannel();
		const QString target = ch(index);
		const QString plugin = loadAnEffect(target);
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		QString name;
		double other = 0.0;
		QVERIFY(aParameter(target, plugin, &name, &other));
		const double original = parameterValue(target, plugin, name);
		const auto ab = [target, &plugin](const QString& action, const QString& slot = QString()) {
			QJsonObject args{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
				{QStringLiteral("action"), action}};
			if (!slot.isEmpty()) { args.insert(QStringLiteral("slot"), slot); }
			return run(QStringLiteral("plugin.ab"), args);
		};
		QVERIFY(ab(QStringLiteral("store"), QStringLiteral("a")).ok);  // selected A contains original
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("name"), name}, {QStringLiteral("value"), other}}).ok);
		QVERIFY(ab(QStringLiteral("store"), QStringLiteral("a")).ok);  // a real change to A
		QVERIFY(ab(QStringLiteral("store"), QStringLiteral("a")).ok);  // no change: still selected A, same settings
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(ab(QStringLiteral("recall"), QStringLiteral("a")).ok, "slot A should still be available after undo");
		QVERIFY(std::abs(parameterValue(target, plugin, name) - original) < 1e-3);
		chainOf(index).clear();
	}

	//! Unload undo rebuilds the effect but carries its session A/B records back onto the new instance.
	void storedSlotsSurviveUnloadUndo()
	{
		const int index = Engine::mixer()->createChannel();
		const QString target = ch(index);
		QString plugin = loadAnEffect(target);
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		QString name;
		double other = 0.0;
		QVERIFY(aParameter(target, plugin, &name, &other));
		const double original = parameterValue(target, plugin, name);
		const auto ab = [target, &plugin](const QString& action, const QString& slot = QString()) {
			QJsonObject args{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
				{QStringLiteral("action"), action}};
			if (!slot.isEmpty()) { args.insert(QStringLiteral("slot"), slot); }
			return run(QStringLiteral("plugin.ab"), args);
		};
		QVERIFY(ab(QStringLiteral("store"), QStringLiteral("a")).ok);
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("name"), name}, {QStringLiteral("value"), other}}).ok);
		QVERIFY(ab(QStringLiteral("store"), QStringLiteral("b")).ok);
		const ControlResult beforeUnload = ab(QStringLiteral("get"));
		QVERIFY(beforeUnload.ok);
		QVERIFY(beforeUnload.result.value(QStringLiteral("has_a")).toBool());
		QVERIFY(beforeUnload.result.value(QStringLiteral("has_b")).toBool());
		QCOMPARE(beforeUnload.result.value(QStringLiteral("current")).toString(), QStringLiteral("b"));
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin}}).ok);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(chainOf(index).effects().size(), std::size_t{1});
		const QString rebuilt = control::effectIdOf(chainOf(index).effects().front());
		QVERIFY(rebuilt != plugin);
		plugin = rebuilt;
		const ControlResult restored = ab(QStringLiteral("get"));
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QVERIFY(restored.result.value(QStringLiteral("has_a")).toBool());
		QVERIFY(restored.result.value(QStringLiteral("has_b")).toBool());
		QCOMPARE(restored.result.value(QStringLiteral("current")).toString(), QStringLiteral("b"));
		QVERIFY(ab(QStringLiteral("recall"), QStringLiteral("a")).ok);
		QVERIFY(std::abs(parameterValue(target, plugin, name) - original) < 1e-3);
		QVERIFY(ab(QStringLiteral("recall"), QStringLiteral("b")).ok);
		QVERIFY(std::abs(parameterValue(target, plugin, name) - other) < 1e-3);
		chainOf(index).clear();
	}

	//! The slot record an undo writes onto a recreated device is released with that device, as a store's own is:
	//! the undo must not leave a record that outlives the device it was written for.
	void aSlotRecordWrittenByAnUndoIsReleasedWithItsDevice()
	{
		const int index = Engine::mixer()->createChannel();
		const QString target = ch(index);
		const QString plugin = loadAnEffect(target);
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		const std::size_t before = control::abSlotEntryCountForTesting();
		QVERIFY(run(QStringLiteral("plugin.ab"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("action"), QStringLiteral("store")}, {QStringLiteral("slot"), QStringLiteral("a")}}).ok);
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin}}).ok);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // the unloaded device is gone
		QCOMPARE(control::abSlotEntryCountForTesting(), before);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the unload: the device is back as a new object
		QCOMPARE(chainOf(index).effects().size(), std::size_t{1});
		const QString back = control::effectIdOf(chainOf(index).effects().front());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the store's undo writes its slots onto the recreated device
		QCOMPARE(control::abSlotEntryCountForTesting(), before + std::size_t{1});
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), back}}).ok);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(control::abSlotEntryCountForTesting(), before);
	}

	//! A device's A/B slot record is released when the device is destroyed: storing both slots and unloading the
	//! device leaves no record of it behind.
	void abSlotsAreReleasedWhenAnEffectIsDestroyed()
	{
		const int channel = Engine::mixer()->createChannel();
		const QString target = ch(channel);
		const QString plugin = loadAnEffect(target);
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		const std::size_t before = control::abSlotEntryCountForTesting();
		const QJsonObject identity{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin}};
		for (const QString& slot : {QStringLiteral("a"), QStringLiteral("b")})
		{
			QJsonObject args = identity;
			args.insert(QStringLiteral("action"), QStringLiteral("store"));
			args.insert(QStringLiteral("slot"), slot);
			QVERIFY2(run(QStringLiteral("plugin.ab"), args).ok, "could not store A/B slot");
		}
		QCOMPARE(control::abSlotEntryCountForTesting(), before + std::size_t{1});
		QPointer<Effect> effect = chainOf(channel).effects().front();
		const ControlResult unloaded = run(QStringLiteral("plugin.unload"), identity);
		QVERIFY2(unloaded.ok, qPrintable(unloaded.errorMessage));
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(effect.isNull());
		QCOMPARE(control::abSlotEntryCountForTesting(), before);
	}

};

QTEST_MAIN(DeviceAbUndoTest)
#include "DeviceAbUndoTest.moc"
