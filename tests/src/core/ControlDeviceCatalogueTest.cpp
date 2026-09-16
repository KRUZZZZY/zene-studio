/*
 * ControlDeviceCatalogueTest.cpp - unit tests for the device catalogue behind
 *                                  plugin.list and the dev-<n> ids, in
 *                                  particular its LV2 half (SPEC A11-A14).
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
 */

#include <QtTest>

#include <algorithm>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ConfigManager.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "lmmsconfig.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "Song.h"

#ifdef LMMS_HAVE_LV2
#include "Lv2Manager.h"
#endif

//! The directory plugin.list must find the in-tree VST3 fixture in (feature row
//! 78, board task #668). Set by tests/CMakeLists.txt when this build has the
//! fixture (WANT_VST3_TEST_INSTRUMENT=ON); empty otherwise, and the VST3 case
//! skips.
#ifndef VST3_TEST_INSTRUMENT_DIR
#define VST3_TEST_INSTRUMENT_DIR ""
#endif

using namespace lmms;

//! The catalogue is what plugin.list returns and what every dev-<n> id indexes
//! into, so its shape and its order are part of the agent surface's contract.
//!
//! The LV2 half is exercised here in-process (through the same
//! PluginFactory + Lv2Manager the running product uses); the end-to-end load /
//! parameter / state / unload path over the control socket is
//! tests/control-socket-integration.py's job.
class ControlDeviceCatalogueTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		// Tell PluginFactory where the build-tree plugins live *before* it is
		// first instantiated (it reads LMMS_PLUGIN_DIR in its constructor).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The hosted formats are appended after the built-in modules, so gaining a
	//! host does not renumber the dev-<n> ids a client already holds.
	void builtinBlockIsNotRenumberedByHostedFormats()
	{
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		QVERIFY(!catalogue.isEmpty());

		bool leftBuiltins = false;
		for (int i = 0; i < catalogue.size(); ++i)
		{
			const bool builtin = catalogue.at(i).format == QLatin1String("builtin");
			if (!builtin) { leftBuiltins = true; continue; }
			QVERIFY2(!leftBuiltins,
				qPrintable(QStringLiteral("built-in device %1 sits after a hosted device")
					.arg(catalogue.at(i).name)));
		}
		// The dev-<n> id is the catalogue index, straight through.
		for (int i = 0; i < catalogue.size(); ++i)
		{
			QCOMPARE(control::deviceId(i), QStringLiteral("dev-%1").arg(i));
		}
	}

	//! Every hosted format is one of the formats plugin.list documents.
	void formatsAreFromTheClosedSet()
	{
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		const QStringList known = {QStringLiteral("builtin"), QStringLiteral("ladspa"),
			QStringLiteral("lv2"), QStringLiteral("vst3")};
		for (const ControlDeviceEntry& entry : catalogue)
		{
			QVERIFY2(known.contains(entry.format),
				qPrintable(QStringLiteral("unknown format '%1' on '%2'")
					.arg(entry.format, entry.name)));
			// The name is what plugin.list reports and what a dev-<n> stands
			// for; an empty one would be unaddressable.
			QVERIFY(!entry.name.isEmpty());
			// The display name is what the select dialogs show, so the LV2 half
			// must carry the plugin's own name - not fall back to the URI.
			if (entry.format == QLatin1String("lv2"))
			{
				QVERIFY2(!entry.displayName.isEmpty(),
					qPrintable(QStringLiteral("lv2 device %1 has no display name")
						.arg(entry.name)));
			}
		}
	}

	//! Every LV2 entry names the URI the host resolves and plugin.load keys on,
	//! and the URI is also the device's own id.
	void lv2EntriesCarryTheirUri()
	{
#ifdef LMMS_HAVE_LV2
		if (getPluginFactory()->pluginInfo("lv2effect").isNull())
		{
			QSKIP("this build has no lv2effect module, so it has no LV2 catalogue half");
		}
		if (Engine::getLv2Manager() == nullptr)
		{
			QSKIP("no Lv2Manager in this process, so there is no LV2 world to enumerate");
		}
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();

		int lv2 = 0;
		for (const ControlDeviceEntry& entry : catalogue)
		{
			if (entry.format != QLatin1String("lv2")) { continue; }
			++lv2;
			QVERIFY2(!entry.uri.isEmpty(),
				qPrintable(QStringLiteral("lv2 device %1 carries no URI").arg(entry.name)));
			// The URI is the id: it is what plugin.load resolves and what a
			// project file stores, so dev-<n> -> plugin.load -> the host all
			// agree on one string.
			QCOMPARE(entry.name, entry.uri);
			QVERIFY(entry.kind == QLatin1String("effect")
				|| entry.kind == QLatin1String("instrument"));
			QVERIFY(entry.loadable);
			// The URI must be one this process's own LV2 world accepted, i.e.
			// one Lv2ControlBase::check() passed - not merely a bundle on disk.
			QVERIFY2(Engine::getLv2Manager()->getPlugin(entry.uri) != nullptr,
				qPrintable(QStringLiteral("lv2 device %1 is not in the LV2 world")
					.arg(entry.uri)));
		}
		// If the box has LV2 bundles the host can use, they must show up; a
		// silently empty LV2 half is the bug this test exists to catch.
		if (!getPluginFactory()->pluginInfo("lv2instrument").isNull())
		{
			const bool worldHasPlugins = std::any_of(
				Engine::getLv2Manager()->begin(), Engine::getLv2Manager()->end(),
				[](const auto& pair) { return pair.second.isValid(); });
			if (worldHasPlugins)
			{
				QVERIFY2(lv2 > 0, "the LV2 world has valid plugins but the catalogue lists none");
			}
		}
		qInfo("catalogue: %d devices, %d of them LV2", int(catalogue.size()), lv2);
#else
		QSKIP("this build has no LV2 host (LMMS_HAVE_LV2 is off)");
#endif
	}

	//! Two calls must agree: a dev-<n> a client holds has to keep meaning the
	//! same device for the life of the binary.
	void catalogueOrderIsDeterministic()
	{
		const QList<ControlDeviceEntry> first = controlDeviceCatalogue();
		const QList<ControlDeviceEntry> second = controlDeviceCatalogue();
		QCOMPARE(first.size(), second.size());
		for (int i = 0; i < first.size(); ++i)
		{
			QCOMPARE(first.at(i).name, second.at(i).name);
			QCOMPARE(first.at(i).format, second.at(i).format);
			QCOMPARE(first.at(i).kind, second.at(i).kind);
		}
	}

	//! An unknown dev-<n> is a typed not_found naming how many devices exist,
	//! and a malformed id is invalid_args - never an empty success.
	void deviceLookupIsTyped()
	{
		ControlDeviceEntry entry;
		int index = -1;
		ControlResult error;

		QVERIFY(!controlDeviceById(QStringLiteral("dev-999999"), &entry, &index, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		QVERIFY(!error.errorMessage.isEmpty());

		QVERIFY(!controlDeviceById(QStringLiteral("amplifier"), &entry, &index, &error));
		QCOMPARE(error.errorKind, ControlErrorKind::InvalidArgs);

		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		QVERIFY(!catalogue.isEmpty());
		QVERIFY(controlDeviceById(control::deviceId(0), &entry, &index, &error));
		QCOMPARE(index, 0);
	}

	//! plugin.list's schema admits the lv2 filter (a format the catalogue now
	//! has) and still refuses a format outside the closed set.
	void pluginListAcceptsTheLv2FormatFilter()
	{
		ControlRegistry* registry = ControlRegistry::instance();

		QJsonObject filter;
		filter.insert(QStringLiteral("format"), QStringLiteral("lv2"));
		const ControlResult lv2 = registry->invoke(QStringLiteral("plugin.list"), filter);
		QVERIFY2(lv2.ok, qPrintable(lv2.errorMessage));
		// The filter can only ever return LV2 entries, and the id it reports is
		// the entry's index in the whole catalogue.
		for (const QJsonValue& value : lv2.result.value(QStringLiteral("devices")).toArray())
		{
			const QJsonObject device = value.toObject();
			QCOMPARE(device.value(QStringLiteral("format")).toString(), QStringLiteral("lv2"));
			QVERIFY(!device.value(QStringLiteral("uri")).toString().isEmpty());
			QVERIFY(device.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("dev-")));
		}

		QJsonObject bogus;
		bogus.insert(QStringLiteral("format"), QStringLiteral("not_a_format"));
		const ControlResult refused = registry->invoke(QStringLiteral("plugin.list"), bogus);
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);

		// The unfiltered listing's total is the whole catalogue, and the parts
		// add up: this is the invariant the integration test's breakdown
		// assertion rides on.
		const ControlResult all = registry->invoke(QStringLiteral("plugin.list"));
		QVERIFY(all.ok);
		const QJsonObject byFormat = all.result.value(QStringLiteral("counts_by_format")).toObject();
		int sum = 0;
		for (auto it = byFormat.begin(); it != byFormat.end(); ++it) { sum += it.value().toInt(); }
		QCOMPARE(sum, all.result.value(QStringLiteral("total")).toInt());
	}

	//! The VST3 half (feature row 78, board task #668): with the in-tree MIT
	//! fixture in the product's VST3 search directory, plugin.list must carry
	//! the class and plugin.load must load it onto an instrument track - the
	//! socket-driven half of instrument hosting, without which the host exists
	//! but is not in the release (CHARTER 3.1).
	//!
	//! Skipped when this build has no VST3 host or no fixture: the option that
	//! builds the fixture is off by default, and no third-party VST3 class can
	//! be assumed on a build machine.
	void vst3ClassesListAndLoadThroughTheSurface()
	{
		if (getPluginFactory()->pluginInfo("vst3instrument").isNull())
		{
			QSKIP("this build has no vst3instrument module, so it has no VST3 catalogue half");
		}
		const QString fixtureDir = QStringLiteral(VST3_TEST_INSTRUMENT_DIR);
		if (fixtureDir.isEmpty() || !QFileInfo::exists(fixtureDir))
		{
			QSKIP("this build has no VST3 test fixture, so there is no VST3 class to list "
				"(configure with -DWANT_VST3_TEST_INSTRUMENT=ON)");
		}

		// The fixture is what this box offers; point the product's VST3 search
		// directory at it, exactly as an installed plug-in would be found.
		ConfigManager::inst()->setVSTDir(fixtureDir);

		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject filter;
		filter.insert(QStringLiteral("format"), QStringLiteral("vst3"));
		const ControlResult vst3 = registry->invoke(QStringLiteral("plugin.list"), filter);
		QVERIFY2(vst3.ok, qPrintable(vst3.errorMessage));

		QString deviceId;
		QString bundle;
		for (const QJsonValue& value : vst3.result.value(QStringLiteral("devices")).toArray())
		{
			const QJsonObject device = value.toObject();
			if (device.value(QStringLiteral("name")).toString()
				!= QStringLiteral("Zene VST3 Test Instrument"))
			{
				continue;
			}
			deviceId = device.value(QStringLiteral("id")).toString();
			bundle = device.value(QStringLiteral("file")).toString();
			QCOMPARE(device.value(QStringLiteral("kind")).toString(),
				QStringLiteral("instrument"));
			QCOMPARE(device.value(QStringLiteral("class")).toString(),
				device.value(QStringLiteral("name")).toString());
			QVERIFY2(device.value(QStringLiteral("loadable")).toBool(),
				"the fixture class is not offered as loadable");
		}
		QVERIFY2(!deviceId.isEmpty(),
			"plugin.list format=vst3 does not carry the fixture's class");
		QVERIFY2(!bundle.isEmpty(), "the vst3 entry carries no bundle path");

		// The id is the catalogue index, so the same device is addressable from
		// the unfiltered listing a client may already hold.
		const ControlResult all = registry->invoke(QStringLiteral("plugin.list"));
		QVERIFY(all.ok);
		QJsonObject listed;
		for (const QJsonValue& value : all.result.value(QStringLiteral("devices")).toArray())
		{
			const QJsonObject device = value.toObject();
			if (device.value(QStringLiteral("id")).toString() == deviceId) { listed = device; }
		}
		QVERIFY2(!listed.isEmpty(),
			qPrintable(QStringLiteral("device %1 is not in the unfiltered listing")
				.arg(deviceId)));
		QCOMPARE(listed.value(QStringLiteral("format")).toString(), QStringLiteral("vst3"));

		// plugin.load, onto an instrument track: the whole point of the block.
		auto* track = new InstrumentTrack(Engine::getSong());
		QJsonObject args;
		args.insert(QStringLiteral("target"), control::trackIdOf(track));
		args.insert(QStringLiteral("device"), deviceId);
		const ControlResult loaded = registry->invoke(QStringLiteral("plugin.load"), args);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(loaded.result.value(QStringLiteral("kind")).toString(),
			QStringLiteral("instrument"));
		QCOMPARE(loaded.result.value(QStringLiteral("plugin")).toString(),
			QStringLiteral("vst3instrument"));

		Instrument* instrument = track->instrument();
		QVERIFY2(instrument != nullptr, "plugin.load reported success but the track has no "
			"instrument");
		QCOMPARE(QString::fromUtf8(instrument->descriptor()->name),
			QStringLiteral("vst3instrument"));
		QVERIFY2(instrument->isMidiBased(),
			"the loaded VST3 instrument is not MIDI based, so no clip could drive it");
		qInfo("plugin.list format=vst3: %s -> plugin.load on %s -> %s, %d parameter(s)",
			qPrintable(deviceId), qPrintable(control::trackIdOf(track)),
			qPrintable(bundle), instrument->parameterCount());
	}
};

QTEST_GUILESS_MAIN(ControlDeviceCatalogueTest)
#include "ControlDeviceCatalogueTest.moc"
