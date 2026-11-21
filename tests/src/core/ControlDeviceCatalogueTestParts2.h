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

void ControlDeviceCatalogueTest::clapInstrumentListsAndLoadsThroughTheSurface() {
	if (getPluginFactory()->pluginInfo("clapinstrument").isNull())
	{
		QSKIP("this build has no clapinstrument module, so it has no CLAP instrument "
			"catalogue half");
	}
	const QString fixtureDir = QStringLiteral(CLAP_TEST_INSTRUMENT_DIR);
	if (fixtureDir.isEmpty() || !QFileInfo::exists(fixtureDir))
	{
		QSKIP("this build has no CLAP instrument fixture, so there is no CLAP class to list "
			"(the fixture is built from the pinned CLAP headers; see "
			"tests/data/clap-test-plugin)");
	}

	// The fixture is what this box offers; point the product's plug-in
	// search directory at it, exactly as an installed .clap module would
	// be found. The CLAP scan walks the same directory the VST3 one does.
	ConfigManager::inst()->setVSTDir(fixtureDir);

	ControlRegistry* registry = ControlRegistry::instance();
	QJsonObject filter;
	filter.insert(QStringLiteral("format"), QStringLiteral("clap"));
	const ControlResult clap = registry->invoke(QStringLiteral("plugin.list"), filter);
	QVERIFY2(clap.ok, qPrintable(clap.errorMessage));

	QString deviceId;
	QString clapId;
	QString module;
	for (const QJsonValue& value : clap.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("name")).toString()
			!= QStringLiteral("org.lmms.test.clap-instrument"))
		{
			continue;
		}
		deviceId = device.value(QStringLiteral("id")).toString();
		clapId = device.value(QStringLiteral("clap_id")).toString();
		module = device.value(QStringLiteral("file")).toString();
		QCOMPARE(device.value(QStringLiteral("kind")).toString(),
			QStringLiteral("instrument"));
		// The id is the CATALOGUE id (dev-<n>), the one id every format's
		// entry carries and the only one plugin.load takes - not the
		// plug-in's own CLAP id, which this case used to hand to
		// plugin.load and which no client can load (the VST3 case above
		// asserts the same shape).
		QVERIFY2(deviceId.startsWith(QStringLiteral("dev-")),
			qPrintable(QStringLiteral("the clap entry's id '%1' is not a dev-<n> "
				"catalogue id, so plugin.load cannot take it")
				.arg(deviceId)));
		// The (file, clap_id) pair the host's load() keys on: the module
		// path plus the plug-in's own CLAP id, under its own key.
		QCOMPARE(clapId, device.value(QStringLiteral("name")).toString());
		QVERIFY2(device.value(QStringLiteral("loadable")).toBool(),
			"the fixture class is not offered as loadable");
	}
	QVERIFY2(!deviceId.isEmpty(),
		"plugin.list format=clap does not carry the fixture's instrument class");
	QVERIFY2(!module.isEmpty(), "the clap entry carries no module path");
	QVERIFY2(!clapId.isEmpty(), "the clap entry carries no plug-in id");

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
		QStringLiteral("clapinstrument"));

	Instrument* instrument = track->instrument();
	QVERIFY2(instrument != nullptr, "plugin.load reported success but the track has no "
		"instrument");
	QCOMPARE(QString::fromUtf8(instrument->descriptor()->name),
		QStringLiteral("clapinstrument"));
	QVERIFY2(instrument->isMidiBased(),
		"the loaded CLAP instrument is not MIDI based, so no clip could drive it");

	// The note path and the audio-output configuration, as the surface
	// reports them: the fixture is a generator with one note input port
	// and a stereo output and NO audio input.
	const ControlResult notes = registry->invoke(QStringLiteral("plugin.host_notes"));
	QVERIFY2(notes.ok, qPrintable(notes.errorMessage));
	QCOMPARE(notes.result.value(QStringLiteral("host")).toString(), QStringLiteral("clap"));
	const QJsonObject ports = notes.result.value(QStringLiteral("ports")).toObject();
	QCOMPARE(ports.value(QStringLiteral("count")).toInt(), 1);
	QCOMPARE(ports.value(QStringLiteral("preferred")).toInt(), 0);
	QVERIFY2(ports.value(QStringLiteral("dialects")).toObject()
			.value(QStringLiteral("clap")).toBool(),
		"the note port the surface reports does not declare the CLAP dialect");
	const QJsonObject audio = notes.result.value(QStringLiteral("audio")).toObject();
	QCOMPARE(audio.value(QStringLiteral("inputs")).toInt(), 0);
	QCOMPARE(audio.value(QStringLiteral("outputs")).toInt(), 2);
	QVERIFY(notes.result.value(QStringLiteral("counters")).toObject()
		.value(QStringLiteral("loads")).toInt() >= 1);

	qInfo("plugin.list format=clap: %s -> plugin.load on %s -> %s; plugin.host_notes ports=%d "
		  "audio=%d/%d",
		qPrintable(deviceId), qPrintable(control::trackIdOf(track)), qPrintable(module),
		ports.value(QStringLiteral("count")).toInt(),
		audio.value(QStringLiteral("inputs")).toInt(),
		audio.value(QStringLiteral("outputs")).toInt());
}
void ControlDeviceCatalogueTest::hostedEffectsListAndLoadThroughTheSurface() {
	const QList<QStringList> cases{
		{QStringLiteral(VST3_TEST_EFFECT_DIR), QStringLiteral("vst3"), QStringLiteral("AGain Sample Accurate"),
			QStringLiteral("vst3effect")},
		{QStringLiteral(CLAP_TEST_EFFECT_DIR), QStringLiteral("clap"), QStringLiteral("LMMS CLAP Test Gain"),
			QStringLiteral("clapeffect")}};
	int checked = 0;
	for (const QStringList& c : cases)
	{
		if (c[0].isEmpty() || !QFileInfo::exists(c[0])) { continue; }
		ConfigManager::inst()->setVSTDir(c[0]);
		const QJsonObject device = listedDevice(c[1], c[2]);
		QVERIFY2(!device.isEmpty(), qPrintable(QStringLiteral("plugin.list format=%1 does not carry '%2'")
			.arg(c[1], c[2])));
		QCOMPARE(device.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
		auto* track = new InstrumentTrack(Engine::getSong());
		const ControlResult loaded = ControlRegistry::instance()->invoke(QStringLiteral("plugin.load"),
			QJsonObject{{QStringLiteral("target"), control::trackIdOf(track)},
				{QStringLiteral("device"), device.value(QStringLiteral("id")).toString()}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(loaded.result.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
		QCOMPARE(loaded.result.value(QStringLiteral("plugin")).toString(), c[3]);
		++checked;
	}
	if (checked == 0) { QSKIP("this build has neither in-tree effect fixture"); }
}
void ControlDeviceCatalogueTest::hostedVendorsAreTheFactoriesOwn() {
	const QList<QStringList> cases{
		{QStringLiteral(VST3_TEST_INSTRUMENT_DIR), QStringLiteral("vst3"), QStringLiteral("Zene VST3 Test Instrument"),
			QStringLiteral("Zene Studio test fixture")},
		{QStringLiteral(CLAP_TEST_INSTRUMENT_DIR), QStringLiteral("clap"), QStringLiteral("org.lmms.test.clap-instrument"),
			QStringLiteral("LMMS contributors")},
		{QStringLiteral(CLAP_TEST_EFFECT_DIR), QStringLiteral("clap"), QStringLiteral("LMMS CLAP Test Gain"),
			QStringLiteral("LMMS contributors")}};
	int checked = 0;
	for (const QStringList& c : cases)
	{
		if (c[0].isEmpty() || !QFileInfo::exists(c[0])) { continue; }
		ConfigManager::inst()->setVSTDir(c[0]);
		const QString id = listedDevice(c[1], c[2]).value(QStringLiteral("id")).toString();
		ControlDeviceEntry entry;
		int index = -1;
		ControlResult error;
		QVERIFY2(controlDeviceById(id, &entry, &index, &error), qPrintable(c[2] + QStringLiteral(" is not listed")));
		QCOMPARE(gui::devicelabels::vendorOf(entry), c[3]);
		++checked;
	}
	if (checked == 0) { QSKIP("this build has no in-tree VST3 or CLAP fixture"); }
}
