/*
 * AuComponentTest.cpp - the format-neutral rules of Audio Unit hosting (Q050-491)
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

/*! The half of AU hosting every build runs: how a component is named (four-character codes, the
 *  type:subtype:manufacturer key a project stores), how a registry scan is recorded in the plug-in scan
 *  cache and filtered by its quarantine list (the rules a plug-in file is scanned by, persisted through the
 *  cache file), which component types are hosted, and how a component that does not instantiate is named
 *  in the typed refusal. Engine-free: the macOS registry is replaced by a list of components. */

#include <QtTest>

#include <QFileInfo>
#include <QTemporaryDir>

#include "AuComponent.h"
#include "Plugin.h"
#include "PluginScanCache.h"

using namespace lmms;

namespace
{

constexpr std::uint32_t code(const char (&text)[5])
{
	return (static_cast<std::uint32_t>(static_cast<unsigned char>(text[0])) << 24)
		| (static_cast<std::uint32_t>(static_cast<unsigned char>(text[1])) << 16)
		| (static_cast<std::uint32_t>(static_cast<unsigned char>(text[2])) << 8)
		| static_cast<std::uint32_t>(static_cast<unsigned char>(text[3]));
}

// Apple's built-in components the hosted macOS job uses.
const au::ComponentId Bandpass{code("aufx"), code("bpas"), code("appl")};
const au::ComponentId Distortion{code("aufx"), code("dist"), code("appl")};
const au::ComponentId Dls{code("aumu"), code("dls "), code("appl")};
const au::ComponentId Generator{code("augn"), code("ttsp"), code("appl")};

QList<au::Component> registry()
{
	return {au::splitComponentName(Bandpass, QStringLiteral("Apple: AUBandpass"), 0x10000),
		au::splitComponentName(Distortion, QStringLiteral("Apple: AUDistortion"), 0x10000),
		au::splitComponentName(Dls, QStringLiteral("Apple: DLSMusicDevice"), 0x10000),
		au::splitComponentName(Generator, QStringLiteral("Apple: AUAudioFilePlayer"), 0x10000)};
}

QStringList namesOf(const QList<au::Component>& components)
{
	QStringList names;
	for (const au::Component& component : components) { names << component.name; }
	return names;
}

} // namespace

class AuComponentTest : public QObject
{
	Q_OBJECT

private slots:
	void fourCharacterCodesRoundTrip()
	{
		QCOMPARE(au::fourCC(code("aufx")), QStringLiteral("aufx"));
		QCOMPARE(au::fourCC(code("dls ")), QStringLiteral("dls "));  // a trailing space is part of the code
		QCOMPARE(au::fourCC(0x00000001u), QStringLiteral("00000001"));  // not printable: hex
		std::uint32_t value = 0;
		QVERIFY(au::fromFourCC(QStringLiteral("dls "), &value));
		QCOMPARE(value, code("dls "));
		QVERIFY(au::fromFourCC(QStringLiteral("00000001"), &value));
		QCOMPARE(value, 0x00000001u);
		QVERIFY(!au::fromFourCC(QStringLiteral("abc"), &value));
		QVERIFY(!au::fromFourCC(QStringLiteral("61756678"), &value));  // a printable code is never written as hex
		QVERIFY(!au::fromFourCC(QStringLiteral("ab\nc"), &value));  // a control character is not a code
	}

	void theKeyAProjectStoresNamesOneComponent()
	{
		QCOMPARE(au::componentKey(Bandpass), QStringLiteral("aufx:bpas:appl"));
		QCOMPARE(au::componentKey(Dls), QStringLiteral("aumu:dls :appl"));
		au::ComponentId parsed;
		QVERIFY(au::parseComponentKey(QStringLiteral("aumu:dls :appl"), &parsed));
		QVERIFY(parsed == Dls);
		for (const QString& bad : {QString(), QStringLiteral("aufx:bpas"), QStringLiteral("aufx:bpas:appl:x"),
				 QStringLiteral("aufx:bp:appl"), QStringLiteral("00000000:bpas:appl")})
		{
			QVERIFY2(!au::parseComponentKey(bad, &parsed), qPrintable(bad));
		}
	}

	void onlyEffectsAndInstrumentsAreHosted()
	{
		QCOMPARE(au::kindOf(code("aufx")), au::Kind::Effect);
		QCOMPARE(au::kindOf(code("aumf")), au::Kind::Effect);
		QCOMPARE(au::kindOf(code("aumu")), au::Kind::Instrument);
		for (const QString& other : {QStringLiteral("augn"), QStringLiteral("aumx"), QStringLiteral("aupn"),
				 QStringLiteral("aufc"), QStringLiteral("auou")})
		{
			std::uint32_t type = 0;
			QVERIFY(au::fromFourCC(other, &type));
			QCOMPARE(au::kindOf(type), au::Kind::Unsupported);
		}
	}

	void eachComponentHasItsOwnAbsoluteScanCachePath()
	{
		const QString path = au::scanCachePath(Bandpass);
		QCOMPARE(path, QStringLiteral("/AudioUnits/aufx/bpas/appl"));
		// The quarantine list takes absolute paths only (plugin.scan_cache_quarantine_add).
		QVERIFY(QFileInfo(path).isAbsolute());
		QVERIFY(au::scanCachePath(Distortion) != path);  // one bundle's units are hidden one by one
	}

	void theRegistryNameSplitsIntoMakerAndName()
	{
		const au::Component split = au::splitComponentName(Bandpass, QStringLiteral("Apple: AUBandpass"), 7);
		QCOMPARE(split.vendor, QStringLiteral("Apple"));
		QCOMPARE(split.name, QStringLiteral("AUBandpass"));
		QCOMPARE(split.version, 7u);
		QVERIFY(split.id == Bandpass);
		const au::Component bare = au::splitComponentName(Bandpass, QStringLiteral("NoMaker"), 0);
		QVERIFY(bare.vendor.isEmpty());
		QCOMPARE(bare.name, QStringLiteral("NoMaker"));
	}

	//! Every listed component is recorded in the scan cache; an unsupported type is recorded as not a plug-in
	//! and not listed; a quarantined one is neither recorded nor listed, and is reported.
	void aScanRecordsEveryComponentAndTheQuarantineHidesOne()
	{
		PluginScanCache cache;
		cache.addToQuarantine(au::scanCachePath(Distortion), QStringLiteral("crashed on load"));
		const au::ScanOutcome outcome = au::recordScan(registry(), &cache);

		QCOMPARE(namesOf(outcome.listed), (QStringList{QStringLiteral("AUBandpass"), QStringLiteral("DLSMusicDevice")}));
		QCOMPARE(outcome.quarantined, QStringList{au::scanCachePath(Distortion)});
		QVERIFY(cache.record(au::scanCachePath(Distortion)) == nullptr);

		const PluginScanRecord* bandpass = cache.record(au::scanCachePath(Bandpass));
		QVERIFY(bandpass != nullptr);
		QCOMPARE(bandpass->status, PluginScanRecord::Status::HasDescriptor);
		QCOMPARE(bandpass->name, QStringLiteral("aufx:bpas:appl"));
		QCOMPARE(bandpass->displayName, QStringLiteral("AUBandpass"));
		QCOMPARE(bandpass->author, QStringLiteral("Apple"));
		QCOMPARE(bandpass->type, static_cast<int>(Plugin::Type::Effect));
		QCOMPARE(cache.record(au::scanCachePath(Dls))->type, static_cast<int>(Plugin::Type::Instrument));
		const PluginScanRecord* generator = cache.record(au::scanCachePath(Generator));
		QVERIFY(generator != nullptr);
		QCOMPARE(generator->status, PluginScanRecord::Status::NotAPlugin);
		QCOMPARE(cache.fileCount(), 3);

		// Lifting the quarantine lists it again, as for a plug-in file.
		QVERIFY(cache.removeFromQuarantine(au::scanCachePath(Distortion)));
		QVERIFY(namesOf(au::recordScan(registry(), &cache).listed).contains(QStringLiteral("AUDistortion")));
		QCOMPARE(cache.fileCount(), 4);
	}

	//! The records and the quarantine entry survive the cache file, and an unchanged rescan writes nothing.
	void theRecordsAndTheQuarantinePersistInTheCacheFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.filePath(QStringLiteral("plugin-scan-cache.json"));
		{
			PluginScanCache cache(file);
			cache.addToQuarantine(au::scanCachePath(Distortion), QStringLiteral("hidden by the user"));
			au::recordScan(registry(), &cache);
			QVERIFY(cache.save());
		}
		PluginScanCache reread(file);
		QVERIFY(reread.load());
		QVERIFY(reread.isQuarantined(au::scanCachePath(Distortion)));
		QVERIFY(reread.record(au::scanCachePath(Bandpass)) != nullptr);
		QVERIFY(!reread.isDirty());
		const au::ScanOutcome again = au::recordScan(registry(), &reread);
		QVERIFY(!namesOf(again.listed).contains(QStringLiteral("AUDistortion")));
		QVERIFY2(!reread.isDirty(), "an unchanged registry rewrote the cache");
		// A new version of a component is a changed record.
		QList<au::Component> updated = registry();
		updated[0].version = 0x20000;
		au::recordScan(updated, &reread);
		QVERIFY(reread.isDirty());
		QCOMPARE(reread.record(au::scanCachePath(Bandpass))->version, 0x20000);
	}

	void aScanWithoutACacheStillLists()
	{
		QCOMPARE(au::recordScan(registry(), nullptr).listed.size(), 3);
	}

	void aFailedStatusIsNamed()
	{
		QCOMPARE(au::statusName(-10875), QStringLiteral("kAudioUnitErr_FailedInitialization"));
		QCOMPARE(au::statusName(-10868), QStringLiteral("kAudioUnitErr_FormatNotSupported"));
		QCOMPARE(au::statusName(-66748), QStringLiteral("kAudioComponentErr_NotPermitted"));
		QCOMPARE(au::statusName(static_cast<std::int32_t>(code("!dat"))), QStringLiteral("'!dat'"));
		QVERIFY(au::statusName(-12345).isEmpty());
	}

	void theRefusalNamesTheComponentTheStepAndTheStatus()
	{
		QCOMPARE(au::instantiationError(Bandpass, QStringLiteral("AUBandpass"), QStringLiteral("AudioUnitInitialize failed"), -10875),
			QStringLiteral("the Audio Unit aufx:bpas:appl (AUBandpass) could not be instantiated: AudioUnitInitialize failed "
				"(status -10875, kAudioUnitErr_FailedInitialization)"));
		const au::ComponentId missing{code("aufx"), code("zzzz"), code("zene")};
		QCOMPARE(au::instantiationError(missing, QString(), QStringLiteral("the system component registry has no such component"), 0),
			QStringLiteral("the Audio Unit aufx:zzzz:zene could not be instantiated: the system component registry has no such "
				"component"));
		QCOMPARE(au::instantiationError(missing, QString(), QStringLiteral("x"), -12345),
			QStringLiteral("the Audio Unit aufx:zzzz:zene could not be instantiated: x (status -12345)"));
	}
};

QTEST_GUILESS_MAIN(AuComponentTest)
#include "AuComponentTest.moc"
