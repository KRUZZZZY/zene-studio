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
#include <memory>
#include <cmath>
#include <span>
#include <vector>

#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "DummyPlugin.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "lmmsconfig.h"
#include "Note.h"
#include "NotePlayHandle.h"
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

//! The directory plugin.list must find the in-tree CLAP instrument fixture in
//! (feature row 79, board task #669). Set by tests/CMakeLists.txt when this
//! build has the fixture (it is built whenever the pinned CLAP headers are
//! present); empty otherwise, and the CLAP case skips.
#ifndef CLAP_TEST_INSTRUMENT_DIR
#define CLAP_TEST_INSTRUMENT_DIR ""
#endif

//! The directories holding the in-tree VST3 and CLAP EFFECT fixtures (the SDK's gain sample and
//! clap-test-gain). Set by tests/CMakeLists.txt when this build has them and their host modules;
//! empty otherwise, and that half of the effect case skips.
#ifndef VST3_TEST_EFFECT_DIR
#define VST3_TEST_EFFECT_DIR ""
#endif
#ifndef CLAP_TEST_EFFECT_DIR
#define CLAP_TEST_EFFECT_DIR ""
#endif

using namespace lmms;

namespace
{

//! The plugin.list entry of @a format named @a name (its "name" or "display_name"), or an empty object.
QJsonObject listedDevice(const QString& format, const QString& name)
{
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"),
		QJsonObject{{QStringLiteral("format"), format}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("name")).toString() == name
			|| device.value(QStringLiteral("display_name")).toString() == name)
		{
			return device;
		}
	}
	return QJsonObject();
}

} // namespace

namespace
{

void writeLittleEndian32(QByteArray& data, int offset, quint32 value)
{
	for (int byte = 0; byte < 4; ++byte)
	{
		data[offset + byte] = static_cast<char>(value >> (byte * 8));
	}
}

void appendSample(QByteArray& patch, quint32 declaredSize, const QByteArray& payload,
	quint32 rootFrequency, char mode)
{
	QByteArray header(96, '\0');
	writeLittleEndian32(header, 8, declaredSize);
	writeLittleEndian32(header, 12, 0);
	writeLittleEndian32(header, 16, 2);
	header[20] = static_cast<char>(0x22);
	header[21] = static_cast<char>(0x56);
	writeLittleEndian32(header, 30, rootFrequency);
	header[55] = mode;
	patch.append(header);
	patch.append(payload);
}

QByteArray makePatmanPatch(bool truncateSecondSample, char firstSampleValue)
{
	QByteArray patch(239, '\0');
	patch.replace(0, 22, QByteArray("GF1PATCH110\0ID#000002", 22));
	patch[82] = 1;
	patch[151] = 1;
	patch[198] = 2;
	appendSample(patch, 2, QByteArray(2, firstSampleValue), 440000, 4);
	appendSample(patch, 2, truncateSecondSample ? QByteArray("c", 1) : QByteArray("cd", 2), 880000, 4);
	return patch;
}

std::vector<SampleFrame> renderPatmanNote(InstrumentTrack& track)
{
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	Note note(TimePos::fromFrames(frames * 8, Engine::framesPerTick()), TimePos(0), DefaultKey);
	NotePlayHandle handle(&track, 0, frames * 8, note, nullptr, -1, NotePlayHandle::Origin::MidiClip);
	std::vector<SampleFrame> output(frames, SampleFrame(0.f, 0.f));
	track.instrument()->playNote(&handle, std::span<SampleFrame>(output));
	return output;
}

} // namespace

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

	//! A truncated later sample must not replace the active patch with samples parsed before the error.
	void aTruncatedPatchKeepsThePreviouslyLoadedSound();

	void initTestCase();

	void cleanupTestCase();

	//! The hosted formats are appended after the built-in modules, so gaining a
	//! host does not renumber the dev-<n> ids a client already holds.
	void builtinBlockIsNotRenumberedByHostedFormats();

	//! Every hosted format is one of the formats plugin.list documents.
	void formatsAreFromTheClosedSet();

	//! A host descriptor is not itself a device: its real entries need a sub-plugin key.
	void builtinCatalogueDoesNotListSubPluginHosts();

	//! The LV2 host must report a clean refusal for missing or malformed keys, never dereference them.
	void lv2InstrumentRefusesMissingOrInvalidKeys();

	//! Every LV2 entry names the URI the host resolves and plugin.load keys on,
	//! and the URI is also the device's own id.
	void lv2EntriesCarryTheirUri();

	//! Two calls must agree: a dev-<n> a client holds has to keep meaning the
	//! same device for the life of the binary.
	void catalogueOrderIsDeterministic();

	//! plugin.list names every built-in device with the fields a client keys on, and the entry found by
	//! its name is the one the catalogue holds at that dev-<n>: the listing and the catalogue cannot differ.
	void builtinDevicesAreListedWithTheirCatalogueFields();

	//! An unknown dev-<n> is a typed not_found naming how many devices exist,
	//! and a malformed id is invalid_args - never an empty success.
	void deviceLookupIsTyped();

	//! plugin.list's schema admits the lv2 filter (a format the catalogue now
	//! has) and still refuses a format outside the closed set.
	void pluginListAcceptsTheLv2FormatFilter();

	//! The VST3 half (feature row 78, board task #668): with the in-tree MIT
	//! fixture in the product's VST3 search directory, plugin.list must carry
	//! the class and plugin.load must load it onto an instrument track - the
	//! socket-driven half of instrument hosting, without which the host exists
	//! but is not in the release (CHARTER 3.1).
	//!
	//! Skipped when this build has no VST3 host or no fixture: the option that
	//! builds the fixture is off by default, and no third-party VST3 class can
	//! be assumed on a build machine.
	void vst3ClassesListAndLoadThroughTheSurface();

	//! The CLAP half (feature row 79, board task #669): with the in-tree MIT
	//! instrument fixture in the product's plug-in search directory,
	//! plugin.list must carry the CLAP class and plugin.load must load it onto
	//! an instrument track - and `plugin.host_notes` must report the note
	//! ports and the audio-output configuration the host discovered, which is
	//! the half of the feature that is only real when it can be observed
	//! through the socket (CHARTER 3.1).
	//!
	//! Skipped when this build has no CLAP instrument host or no fixture.
	void clapInstrumentListsAndLoadsThroughTheSurface();

	//! The VST3 and CLAP EFFECT hosts, through the surface: with each in-tree effect fixture in the
	//! product's plug-in search directory, plugin.list carries the effect class and plugin.load puts
	//! it in a track's effect chain, hosted by vst3effect / clapeffect. Before the two modules
	//! exported lmms_plugin_main, PluginFactory recorded them as "not a plug-in": no VST3 or CLAP
	//! effect was ever offered, and a project's saved one loaded as a placeholder.
	void hostedEffectsListAndLoadThroughTheSurface();

	//! The vendor a VST3 or CLAP device's row shows (the device picker, the device card's Replace and Insert)
	//! is the one the plug-in's own factory reports - the VST3 class's vendor, which the SDK fills from
	//! PFactoryInfo when the class names none, and the CLAP descriptor's vendor - not "Not reported".
	void hostedVendorsAreTheFactoriesOwn();
};

#include "ControlDeviceCatalogueTestParts1.h"
#include "ControlDeviceCatalogueTestParts2.h"

QTEST_GUILESS_MAIN(ControlDeviceCatalogueTest)
#include "ControlDeviceCatalogueTest.moc"
