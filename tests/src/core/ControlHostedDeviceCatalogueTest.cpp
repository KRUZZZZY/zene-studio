/*
 * ControlHostedDeviceCatalogueTest.cpp - the CLAP and VST3 halves of the device catalogue, through stand-in host modules
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

/*! plugin.list and plugin.load reach hosted CLAP and VST3 classes through the host modules' own descriptors: the
 *  catalogue asks the descriptor named "clapeffect" / "vst3instrument" ... for its sub-plug-in keys, turns each
 *  complete key into an entry (id, display name, format, kind, module file, loadable) and, at load time,
 *  rebuilds the key from the entry or refuses it typed. A build without the CLAP headers or the VST3 SDK has none
 *  of those modules, so this runs against stand-ins (tests/src/plugins/FakeHostModule.cpp) built for clapeffect,
 *  vst3effect and vst3instrument - and deliberately NOT clapinstrument, so a CLAP instrument entry meets a missing
 *  host. Each stand-in lists a valid class, one whose module file is gone, and two with an attribute missing. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "PluginFactory.h"

using namespace lmms;

namespace
{

//! The entries of one format and kind that \a list holds, in order.
QList<ControlDeviceEntry> entriesOf(const QList<ControlDeviceEntry>& list, const QString& format, const QString& kind)
{
	QList<ControlDeviceEntry> out;
	for (const ControlDeviceEntry& entry : list)
	{
		if (entry.format == format && entry.kind == kind) { out.append(entry); }
	}
	return out;
}

} // namespace

class ControlHostedDeviceCatalogueTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_cache;

private slots:
	void initTestCase()
	{
		QVERIFY(m_cache.isValid());
		qputenv("LMMS_PLUGIN_DIR", QByteArrayLiteral(LMMS_FAKE_HOSTS_DIR));
		qputenv("LMMS_PLUGIN_SCAN_CACHE", QFile::encodeName(m_cache.filePath(QStringLiteral("scan-cache.json"))));
		Engine::init(true);
		ControlRegistry::setReady(true);
		for (const char* host : {"clapeffect", "vst3effect", "vst3instrument"})
		{
			QVERIFY2(PluginFactory::instance()->pluginInfo(host).descriptor != nullptr,
				qPrintable(QStringLiteral("stand-in host %1 not discovered: %2").arg(QLatin1String(host), PluginFactory::instance()->scanReport())));
		}
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void clapClassesAreListedFromTheHostDescriptor()
	{
		QList<ControlDeviceEntry> list;
		controlClapDeviceEntries(&list);
		controlClapDeviceEntries(nullptr);  // a null sink is ignored

		const QList<ControlDeviceEntry> effects = entriesOf(list, QStringLiteral("clap"), QStringLiteral("effect"));
		QCOMPARE(effects.size(), 2);  // the valid class and the one whose file is gone; the two incomplete keys are not offered
		QCOMPARE(effects.at(0).name, QStringLiteral("clapeffect.valid"));
		QCOMPARE(effects.at(0).displayName, QStringLiteral("clapeffect Valid"));
		QVERIFY(QFileInfo::exists(effects.at(0).file));
		QVERIFY(effects.at(0).loadable);
		QCOMPARE(effects.at(1).name, QStringLiteral("clapeffect.gone"));
		// No clapinstrument host in this directory: no instrument entries, and effects come first.
		QVERIFY(entriesOf(list, QStringLiteral("clap"), QStringLiteral("instrument")).isEmpty());
		QCOMPARE(list.size(), 2);
	}

	void vst3ClassesAreListedEffectsFirst()
	{
		QList<ControlDeviceEntry> list;
		controlVst3DeviceEntries(&list);
		controlVst3DeviceEntries(nullptr);

		QCOMPARE(list.size(), 4);
		QCOMPARE(list.at(0).kind, QStringLiteral("effect"));
		QCOMPARE(list.at(1).kind, QStringLiteral("effect"));
		QCOMPARE(list.at(2).kind, QStringLiteral("instrument"));
		QCOMPARE(list.at(3).kind, QStringLiteral("instrument"));
		QCOMPARE(list.at(0).name, QStringLiteral("vst3effect.valid"));
		QCOMPARE(list.at(0).className, QStringLiteral("vst3effect.valid"));  // the class name is the device's id
		QCOMPARE(list.at(0).displayName, list.at(0).name);
		QCOMPARE(list.at(0).format, QStringLiteral("vst3"));
		QVERIFY(list.at(0).loadable);
		QCOMPARE(list.at(2).name, QStringLiteral("vst3instrument.valid"));
		QCOMPARE(list.at(3).name, QStringLiteral("vst3instrument.gone"));
	}

	void theCatalogueAndPluginListCarryTheHostedEntries()
	{
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		QCOMPARE(entriesOf(catalogue, QStringLiteral("clap"), QStringLiteral("effect")).size(), 2);
		QCOMPARE(entriesOf(catalogue, QStringLiteral("vst3"), QStringLiteral("instrument")).size(), 2);

		const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"),
			QJsonObject{{QStringLiteral("format"), QStringLiteral("vst3")}, {QStringLiteral("kind"), QStringLiteral("effect")}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("count")).toInt(), 2);
		const QJsonObject first = listed.result.value(QStringLiteral("devices")).toArray().first().toObject();
		QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("vst3effect.valid"));
		QCOMPARE(first.value(QStringLiteral("format")).toString(), QStringLiteral("vst3"));
		QVERIFY(first.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("dev-")));
	}

	void aClapEntryBecomesTheKeyTheHostLoadsBy()
	{
		QList<ControlDeviceEntry> list;
		controlClapDeviceEntries(&list);
		const ControlDeviceEntry valid = list.at(0);
		QString plugin;
		Plugin::Descriptor::SubPluginFeatures::Key key;
		bool useKey = false;
		ControlResult error;
		QVERIFY2(controlClapDeviceModule(valid, &plugin, &key, &useKey, &error), qPrintable(error.errorMessage));
		QCOMPARE(plugin, QStringLiteral("clapeffect"));
		QVERIFY(useKey);
		QVERIFY(key.isValid());
		QCOMPARE(key.attributes.value(QStringLiteral("file")), valid.file);
		QCOMPARE(key.attributes.value(QStringLiteral("id")), valid.name);
		QCOMPARE(key.attributes.value(QStringLiteral("name")), valid.displayName);
		QCOMPARE(key.name, valid.displayName);

		// With no display name the id stands in for it.
		ControlDeviceEntry bare = valid;
		bare.displayName.clear();
		QVERIFY(controlClapDeviceModule(bare, &plugin, &key, &useKey, &error));
		QCOMPARE(key.name, bare.name);
		QCOMPARE(key.attributes.value(QStringLiteral("name")), bare.name);
	}

	void aClapEntryThatCannotLoadIsRefusedTyped()
	{
		QList<ControlDeviceEntry> list;
		controlClapDeviceEntries(&list);
		QString plugin;
		Plugin::Descriptor::SubPluginFeatures::Key key;
		bool useKey = false;
		ControlResult error;

		// The module file was deleted after the listing.
		QVERIFY(!controlClapDeviceModule(list.at(1), &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		QVERIFY(error.errorMessage.contains(QStringLiteral("no longer there")));

		// An entry that names no module / no class.
		ControlDeviceEntry noFile = list.at(0);
		noFile.file.clear();
		QVERIFY(!controlClapDeviceModule(noFile, &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(error.errorMessage.contains(QStringLiteral("module path")));
		ControlDeviceEntry noId = list.at(0);
		noId.name.clear();
		QVERIFY(!controlClapDeviceModule(noId, &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(error.errorMessage.contains(QStringLiteral("plug-in id")));

		// A CLAP instrument entry with no clapinstrument host in this build.
		ControlDeviceEntry instrument = list.at(0);
		instrument.kind = QStringLiteral("instrument");
		QVERIFY(!controlClapDeviceModule(instrument, &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		QVERIFY(error.errorMessage.contains(QStringLiteral("clapinstrument")));
	}

	void aVst3EntryBecomesTheKeyTheHostLoadsByOrIsRefusedTyped()
	{
		QList<ControlDeviceEntry> list;
		controlVst3DeviceEntries(&list);
		QString plugin;
		Plugin::Descriptor::SubPluginFeatures::Key key;
		bool useKey = false;
		ControlResult error;

		QVERIFY2(controlVst3DeviceModule(list.at(0), &plugin, &key, &useKey, &error), qPrintable(error.errorMessage));
		QCOMPARE(plugin, QStringLiteral("vst3effect"));
		QVERIFY(useKey);
		QCOMPARE(key.attributes.value(QStringLiteral("file")), list.at(0).file);
		QCOMPARE(key.attributes.value(QStringLiteral("class")), list.at(0).className);
		QVERIFY2(controlVst3DeviceModule(list.at(2), &plugin, &key, &useKey, &error), qPrintable(error.errorMessage));
		QCOMPARE(plugin, QStringLiteral("vst3instrument"));

		QVERIFY(!controlVst3DeviceModule(list.at(1), &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		QVERIFY(error.errorMessage.contains(QStringLiteral("no longer there")));
		ControlDeviceEntry noFile = list.at(0);
		noFile.file.clear();
		QVERIFY(!controlVst3DeviceModule(noFile, &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(error.errorMessage.contains(QStringLiteral("bundle path")));
		ControlDeviceEntry noClass = list.at(0);
		noClass.className.clear();
		QVERIFY(!controlVst3DeviceModule(noClass, &plugin, &key, &useKey, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(error.errorMessage.contains(QStringLiteral("class name")));
	}
};

QTEST_MAIN(ControlHostedDeviceCatalogueTest)
#include "ControlHostedDeviceCatalogueTest.moc"
