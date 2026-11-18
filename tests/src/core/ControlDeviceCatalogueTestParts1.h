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

// Complete fixture method definitions; included once after the fixture declaration.

void ControlDeviceCatalogueTest::aTruncatedPatchKeepsThePreviouslyLoadedSound() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	QFile validFile(directory.filePath(QStringLiteral("valid.pat")));
	QVERIFY(validFile.open(QIODevice::WriteOnly));
	const QByteArray validPatch = makePatmanPatch(false, 64);
	QCOMPARE(validFile.write(validPatch), static_cast<qint64>(validPatch.size()));
	validFile.close();
	QFile truncatedFile(directory.filePath(QStringLiteral("truncated.pat")));
	QVERIFY(truncatedFile.open(QIODevice::WriteOnly));
	const QByteArray truncatedPatch = makePatmanPatch(true, -64);
	QCOMPARE(truncatedFile.write(truncatedPatch), static_cast<qint64>(truncatedPatch.size()));
	truncatedFile.close();

	QVERIFY2(!getPluginFactory()->pluginInfo("patman").isNull(), "Patman plugin is unavailable");
	auto* track = new InstrumentTrack(Engine::getSong());
	Instrument* baseInstrument = track->loadInstrument(QStringLiteral("patman"));
	QVERIFY(baseInstrument != nullptr);
	QVERIFY(QMetaObject::invokeMethod(baseInstrument, "setFile", Qt::DirectConnection,
		Q_ARG(QString, validFile.fileName()), Q_ARG(bool, false)));
	NotePlayHandleManager::init();
	const std::vector<SampleFrame> previousSound = renderPatmanNote(*track);
	QVERIFY(previousSound[4][0] > 0.1f);
	QVERIFY(QMetaObject::invokeMethod(baseInstrument, "setFile", Qt::DirectConnection,
		Q_ARG(QString, truncatedFile.fileName()), Q_ARG(bool, false)));
	const std::vector<SampleFrame> currentSound = renderPatmanNote(*track);
	QVERIFY2(std::abs(currentSound[4][0] - previousSound[4][0]) < 1e-6f,
		"the truncated patch replaced the previously loaded sample");
}
void ControlDeviceCatalogueTest::initTestCase() {
	// Tell PluginFactory where the build-tree plugins live *before* it is
	// first instantiated (it reads LMMS_PLUGIN_DIR in its constructor).
	qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
	Engine::init(true);
	ControlRegistry::setReady(true);
}
void ControlDeviceCatalogueTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void ControlDeviceCatalogueTest::builtinBlockIsNotRenumberedByHostedFormats() {
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
void ControlDeviceCatalogueTest::formatsAreFromTheClosedSet() {
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	const QStringList known = {QStringLiteral("builtin"), QStringLiteral("ladspa"),
		QStringLiteral("lv2"), QStringLiteral("vst3"), QStringLiteral("clap")};
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
void ControlDeviceCatalogueTest::builtinCatalogueDoesNotListSubPluginHosts() {
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	for (const Plugin::Type type : {Plugin::Type::Effect, Plugin::Type::Instrument})
	{
		for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
		{
			if (descriptor == nullptr || descriptor->subPluginFeatures == nullptr) { continue; }
			for (const ControlDeviceEntry& entry : catalogue)
			{
				if (entry.format == QLatin1String("builtin"))
				{
					QVERIFY2(entry.name != QLatin1String(descriptor->name),
						qPrintable(QStringLiteral("host container '%1' was listed as a plain device")
							.arg(QString::fromUtf8(descriptor->name))));
				}
			}
		}
	}
}
void ControlDeviceCatalogueTest::lv2InstrumentRefusesMissingOrInvalidKeys() {
#ifdef LMMS_HAVE_LV2
	const PluginFactory::PluginInfo& info = getPluginFactory()->pluginInfo("lv2instrument");
	if (info.isNull()) { QSKIP("this build has no lv2instrument module"); }

	QTest::ignoreMessage(QtCriticalMsg,
		"Cannot instantiate LV2 instrument: missing sub-plugin key");
	std::unique_ptr<Plugin> missingKey(Plugin::instantiateWithKey(
		QStringLiteral("lv2instrument"), nullptr, nullptr));
	QVERIFY(dynamic_cast<DummyPlugin*>(missingKey.get()) != nullptr);

	using Key = Plugin::Descriptor::SubPluginFeatures::Key;
	const Key invalidKey(info.descriptor, QStringLiteral("missing URI"), {});
	QTest::ignoreMessage(QtCriticalMsg,
		"Cannot instantiate LV2 instrument: sub-plugin key has no URI");
	std::unique_ptr<Plugin> missingUri(Plugin::instantiateWithKey(
		QStringLiteral("lv2instrument"), nullptr, &invalidKey));
	QVERIFY(dynamic_cast<DummyPlugin*>(missingUri.get()) != nullptr);
#else
	QSKIP("this build has no LV2 host");
#endif
}
void ControlDeviceCatalogueTest::lv2EntriesCarryTheirUri() {
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
void ControlDeviceCatalogueTest::catalogueOrderIsDeterministic() {
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
void ControlDeviceCatalogueTest::builtinDevicesAreListedWithTheirCatalogueFields() {
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"),
		QJsonObject{{QStringLiteral("format"), QStringLiteral("builtin")}});
	QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
	const QJsonArray devices = listed.result.value(QStringLiteral("devices")).toArray();
	QVERIFY2(!devices.isEmpty(), "the build lists no built-in device at all");
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	for (const QJsonValue& value : devices)
	{
		const QJsonObject device = value.toObject();
		QCOMPARE(device.value(QStringLiteral("format")).toString(), QStringLiteral("builtin"));
		const QString name = device.value(QStringLiteral("name")).toString();
		QVERIFY(!name.isEmpty());
		const QJsonObject again = listedDevice(QStringLiteral("builtin"), name);
		QVERIFY2(!again.isEmpty(), qPrintable(QStringLiteral("'%1' listed but not found by its name").arg(name)));
		QCOMPARE(again.value(QStringLiteral("id")).toString(), device.value(QStringLiteral("id")).toString());
		bool inCatalogue = false;
		for (int i = 0; i < catalogue.size(); ++i)
		{
			if (control::deviceId(i) != device.value(QStringLiteral("id")).toString()) { continue; }
			inCatalogue = true;
			QCOMPARE(catalogue.at(i).name, name);
			QCOMPARE(catalogue.at(i).loadable, device.value(QStringLiteral("loadable")).toBool());
		}
		QVERIFY2(inCatalogue, "a listed device id names no catalogue entry");
	}
	// A name nothing carries finds nothing.
	QVERIFY(listedDevice(QStringLiteral("builtin"), QStringLiteral("no-such-device")).isEmpty());
	// A format outside the closed set is refused by the schema, not answered with an empty list.
	const ControlResult none = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"),
		QJsonObject{{QStringLiteral("format"), QStringLiteral("no-such-format")}});
	QVERIFY(!none.ok);
	QCOMPARE(none.errorKind, ControlErrorKind::InvalidArgs);
}
void ControlDeviceCatalogueTest::deviceLookupIsTyped() {
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
void ControlDeviceCatalogueTest::pluginListAcceptsTheLv2FormatFilter() {
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
void ControlDeviceCatalogueTest::vst3ClassesListAndLoadThroughTheSurface() {
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
