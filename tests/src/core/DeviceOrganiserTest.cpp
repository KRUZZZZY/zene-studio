/*
 * DeviceOrganiserTest.cpp - the plug-in organiser, the plug-in folders and the preferred format (split out of
 *                            DeviceCopyTest to keep both under the file-length ratchet)
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
/*! 0.5.0 L10 (moved unchanged from DeviceCopyTest.cpp): plugin.organise marks a device and plugin.organisation
 *  lists it, undoably (MX-166); plugin.paths_set replaces a format's folders (MX-163); the preferred plug-in format
 *  is kept and undone (MX-167); W6: the organiser tells an open picker that it changed. */

#include <QtTest>

#include <QDir>
#include <QJsonArray>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "PluginOrganiser.h"
#include "PluginPaths.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class DeviceOrganiserTest : public QObject
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
	void theOrganiserMarksADevice()
	{
		QCOMPARE(pluginorganiser::keyOf(QStringLiteral("ladspaeffect"), {{QStringLiteral("file"), QStringLiteral("caps")},
			{QStringLiteral("plugin"), QStringLiteral("Chorus")}}), QStringLiteral("ladspaeffect|file=caps|plugin=Chorus"));
		const QString key = QStringLiteral("amplifier");
		const ControlResult marked = run(QStringLiteral("plugin.organise"), {{QStringLiteral("key"), key},
			{QStringLiteral("favourite"), true}, {QStringLiteral("folder"), QStringLiteral("Gain")}});
		QVERIFY2(marked.ok, qPrintable(marked.errorMessage));
		QVERIFY(pluginorganiser::entry(key).favourite);
		const QJsonObject listed = run(QStringLiteral("plugin.organisation"), {{QStringLiteral("folder"), QStringLiteral("Gain")}}).result;
		QCOMPARE(listed.value(QStringLiteral("entries")).toArray().size(), 1);
		QVERIFY(listed.value(QStringLiteral("folders")).toArray().contains(QStringLiteral("Gain")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(pluginorganiser::entry(key).isDefault());
		QCOMPARE(run(QStringLiteral("plugin.organise"), {{QStringLiteral("favourite"), true}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void theFoldersAreSetAndUndone()
	{
		const QString dir = QDir::tempPath();
		const ControlResult set = run(QStringLiteral("plugin.paths_set"), {{QStringLiteral("format"), QStringLiteral("vst3")},
			{QStringLiteral("dirs"), QJsonArray{dir}}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(pluginpaths::configured(pluginpaths::Format::Vst3), QStringList{dir});
		QCOMPARE(pluginpaths::searchDirs(pluginpaths::Format::Vst3).front(), dir);  // searched first
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(pluginpaths::configured(pluginpaths::Format::Vst3).isEmpty());
		QCOMPARE(run(QStringLiteral("plugin.paths_set"), {{QStringLiteral("format"), QStringLiteral("au")},
			{QStringLiteral("dirs"), QJsonArray{}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void aPreferredFormatIsKeptAndUndone()
	{
		QVERIFY(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("clap")}}).ok);
		QCOMPARE(pluginorganiser::preferredFormat(), QStringLiteral("clap"));
		// Luna 5726: a changed format is an edit, so the transaction it records carries
		// the handler's OWN before-state and mechanism. Without a payload the registry
		// only knows "some undo step happened" (ControlRegistry.cpp recordTransactionOf).
		const QJsonObject formatTx = revtest::stateOf(QStringLiteral("plugin.prefer_format"));
		QVERIFY2(!formatTx.isEmpty(), "the format change recorded no transaction");
		QVERIFY2(formatTx.value(QStringLiteral("before")).toObject().contains(QStringLiteral("format")),
			"the transaction carries no prior format to revert to");
		QCOMPARE(formatTx.value(QStringLiteral("before")).toObject().value(QStringLiteral("format")).toString(),
			QString());
		QCOMPARE(formatTx.value(QStringLiteral("inverse")).toObject()
			.value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
		QCOMPARE(formatTx.value(QStringLiteral("reversible")).toBool(), true);
		// Writing the format it already holds is not an edit: no new transaction for it.
		const int stepsBeforeNoOp = revtest::run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size();
		QVERIFY(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("clap")}}).ok);
		QCOMPARE(revtest::run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size(), stepsBeforeNoOp);
		QCOMPARE(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("au")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(pluginorganiser::preferredFormat().isEmpty());
	}

	//! W6: the organiser tells an open effect picker that it changed - plugin.organise and plugin.prefer_format
	//! over the socket, and their undo - once per real change, and not for a write that changes nothing.
	void theOrganiserSignalsItsChanges()
	{
		QSignalSpy changed(pluginorganiser::notifier(), &pluginorganiser::Notifier::changed);
		const QString key = QStringLiteral("amplifier");
		QVERIFY(run(QStringLiteral("plugin.organise"), {{QStringLiteral("key"), key}, {QStringLiteral("hidden"), true}}).ok);
		QCOMPARE(changed.count(), 1);
		pluginorganiser::setEntry(key, pluginorganiser::entry(key));  // the same entry again: nothing to show
		QCOMPARE(changed.count(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(changed.count(), 2);
		QVERIFY(pluginorganiser::entry(key).isDefault());
		QVERIFY(run(QStringLiteral("plugin.prefer_format"), {{QStringLiteral("format"), QStringLiteral("lv2")}}).ok);
		QCOMPARE(changed.count(), 3);
		pluginorganiser::setPreferredFormat(QStringLiteral("lv2"));  // unchanged
		QCOMPARE(changed.count(), 3);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(changed.count(), 4);
		QVERIFY(pluginorganiser::preferredFormat().isEmpty());
	}
};

QTEST_MAIN(DeviceOrganiserTest)
#include "DeviceOrganiserTest.moc"
