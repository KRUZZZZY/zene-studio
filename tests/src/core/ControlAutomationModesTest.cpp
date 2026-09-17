/*
 * ControlAutomationModesTest.cpp - the automation MODES driven through the
 *                                 control surface: the mode round trip and the
 *                                 no-destruction property (feature-list rows 10
 *                                 and 63, board task #647).
 *
 * The group's registry surface (the ids, the schemas and the typed refusals)
 * lives in ControlAutomationScriptTest, which is the group's contract test.
 * This file is the feature's own proof, and it is a second binary on purpose:
 * the file-length ratchet is not moved for a new feature (the same reason
 * ClipLinkTest/ClipLinkPersistenceTest are two files).
 *
 * THE PROPERTY: with the mode in read, riding a control while the transport
 * walks over previously recorded automation must leave that automation exactly
 * as it was. Each comparison is paired with a leg that runs the IDENTICAL
 * harness where a write IS expected (write mode), so the read assertion cannot
 * pass by the harness never writing anything at all - a comparison-shaped
 * assertion that cannot fail is not a proof.
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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{
/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime - the module's
 * import descriptor names zene.exe (ERROR_MOD_NOT_FOUND, 126) and the product loads them
 * inside zene.exe; AudioPluginTest.cpp carries the full mechanism and the CI evidence.
 */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

} // namespace

class ControlAutomationModesTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		// The built-in device modules live in the build tree, and the plugin
		// factory finds them through LMMS_PLUGIN_DIR - the preamble
		// ReversibilityUndoTest and ControlDeviceCatalogueTest use, and the
		// reason tests/CMakeLists.txt puts this target on the LMMS_TEST_PLUGIN_DIR
		// list. Without it, a device cannot be loaded at all.
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		// THE DEVICE THE MODES DRIVE, built through the surface the way a client
		// builds one. A bare Engine::init song has NO track at all - the default
		// project is loaded by main(), which a test binary never runs - and
		// automation.get_state reports the parameters of the devices that EXIST
		// (measured on this build: 27 for a track carrying the triple
		// oscillator, an empty `tracks` array in a song with none). The modules
		// alone are not enough, which is what the README of this list said: the
		// fixture has to make the track.
		m_devicedTrack = revtest::addInstrumentTrack();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every mode is drivable AND observable: the read-back reports exactly the
	//! string that was set, for all five, through the one spelling function both
	//! directions use (control::automationModeName). A mode that could be set but
	//! not read back would not be an observable feature (the acceptance contract
	//! asks for both), and "off" is checked here to be a mode of its own rather
	//! than a second spelling of "read".
	void modeSetIsObservableForEveryMode()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QString trackId;
		QString paramId;
		double minValue = 0.0;
		double maxValue = 1.0;
		const bool haveParameter = findAutomatableParameter(registry, &trackId, &paramId, &minValue, &maxValue);
		if (!haveParameter && !testHostCanLoadPluginModules())
		{
			QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
				"executable, so on Windows their import descriptor names zene.exe and a test host "
				"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
				"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
		}
		QVERIFY2(haveParameter,
			"this song exposes no automatable device parameter, so the mode commands have nothing "
			"to drive: the binary needs the instrument modules (LMMS_TEST_PLUGIN_DIR, see "
			"tests/CMakeLists.txt)");

		const QVector<QString> modes = {qstr("off"), qstr("read"), qstr("touch"),
			qstr("latch"), qstr("write")};
		for (const QString& mode : modes)
		{
			const ControlResult result = registry->invoke(QStringLiteral("automation.mode_set"),
				QJsonObject{{QStringLiteral("track"), trackId},
					{QStringLiteral("parameter"), paramId},
					{QStringLiteral("mode"), mode}});
			QVERIFY2(result.ok, qPrintable(mode + ": " + result.errorMessage));
			QCOMPARE(result.result.value(QStringLiteral("mode")).toString(), mode);
			QCOMPARE(parameterEntry(registry, trackId, paramId).value(QStringLiteral("mode")).toString(),
				mode);
			QVERIFY2(!result.result.value(QStringLiteral("mode_before")).toString().isEmpty(),
				qPrintable(mode + ": the set reported no mode_before"));
			QCOMPARE(result.result.value(QStringLiteral("changed")).toBool(),
				result.result.value(QStringLiteral("mode_before")).toString() != mode);
		}

		// The five are five distinct modes, not five names for fewer: a mode
		// that reads back as another one has failed here.
		QSet<QString> seen;
		for (const QString& mode : modes)
		{
			registry->invoke(QStringLiteral("automation.mode_set"),
				QJsonObject{{QStringLiteral("track"), trackId},
					{QStringLiteral("parameter"), paramId},
					{QStringLiteral("mode"), mode}});
			seen.insert(parameterEntry(registry, trackId, paramId)
				.value(QStringLiteral("mode")).toString());
		}
		QCOMPARE(seen.size(), static_cast<int>(modes.size()));

		// Leave it as the test found it (read): the mode is runtime state, so
		// this is the whole clean-up.
		const ControlResult back = registry->invoke(QStringLiteral("automation.mode_set"),
			QJsonObject{{QStringLiteral("track"), trackId},
				{QStringLiteral("parameter"), paramId},
				{QStringLiteral("mode"), qstr("read")}});
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
	}

	//! THE no-destruction property, driven through the socket. automation.add_point
	//! records a curve; automation.mode_set puts the control in read; the control
	//! is then RIDDEN - plugin.param_set, a manual move of the same model the clip
	//! automates - while this harness advances the transport through the clip with
	//! its own calls to Song::processNextBuffer(); and the recorded automation must
	//! come back exactly as it was.
	//!
	//! The sensitivity leg at the end runs the IDENTICAL harness in write mode and
	//! REQUIRES the clip to change.
	//!
	//! The comparison is made through the wire, not through the model: the numbers
	//! in it are the ones a client sees in automation.get_state.
	void readRideThroughTheSocketCannotTouchTheRecordedAutomation()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QString trackId;
		QString paramId;
		double minValue = 0.0;
		double maxValue = 1.0;
		const bool haveParameter = findAutomatableParameter(registry, &trackId, &paramId, &minValue, &maxValue);
		if (!haveParameter && !testHostCanLoadPluginModules())
		{
			QSKIP("the song exposes no automatable device parameter: plugin modules link the zene "
				"executable, so on Windows their import descriptor names zene.exe and a test host "
				"cannot satisfy it; the product loads them inside zene.exe where that resolves by "
				"construction (CI msvc-x64: QLibrary::load -> ERROR_MOD_NOT_FOUND, 126)");
		}
		QVERIFY2(haveParameter,
			"this song exposes no automatable device parameter, so the mode commands have nothing "
			"to drive: the binary needs the instrument modules (LMMS_TEST_PLUGIN_DIR, see "
			"tests/CMakeLists.txt)");
		QVERIFY2(maxValue > minValue, "the parameter has no range to ride");

		// 1. Record automation through the surface: three points across the clip.
		const double mid = minValue + 0.5 * (maxValue - minValue);
		const double low = minValue + 0.25 * (maxValue - minValue);
		const double high = minValue + 0.75 * (maxValue - minValue);
		const QVector<QPair<int, double>> points = {{0, mid}, {96, low}, {192, high}};
		for (const QPair<int, double>& point : points)
		{
			const ControlResult added = registry->invoke(QStringLiteral("automation.add_point"),
				QJsonObject{{QStringLiteral("track"), trackId},
					{QStringLiteral("parameter"), paramId},
					{QStringLiteral("ticks"), point.first},
					{QStringLiteral("value"), point.second}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
		}

		const QString recorded = recordedAutomationSignature(registry, trackId, paramId);
		QVERIFY2(recorded != notAutomated(), qPrintable(recorded));
		// The legacy per-clip record path must be OFF, or the ride would be
		// written by IT and this proof would be measuring the wrong mechanism.
		QCOMPARE(parameterEntry(registry, trackId, paramId).value(QStringLiteral("automation"))
			.toObject().value(QStringLiteral("recording")).toBool(), false);

		// 2. The mode under test: read (the default, set explicitly so this leg
		//    tests the command and not an accident of what the default is).
		const ControlResult readMode = registry->invoke(QStringLiteral("automation.mode_set"),
			QJsonObject{{QStringLiteral("track"), trackId},
				{QStringLiteral("parameter"), paramId},
				{QStringLiteral("mode"), qstr("read")}});
		QVERIFY2(readMode.ok, qPrintable(readMode.errorMessage));
		QCOMPARE(readMode.result.value(QStringLiteral("mode")).toString(), qstr("read"));
		QCOMPARE(parameterEntry(registry, trackId, paramId).value(QStringLiteral("mode")).toString(),
			qstr("read"));

		// 3. Ride the control while the transport walks over the recorded curve.
		//    Engine::init() starts the render-only dummy device, and that thread
		//    renders the same song: this harness drives processNextBuffer() itself
		//    (two threads walking the automation at once is the race
		//    AutomationModesTest documents), so the device is stopped here.
		Song* song = Engine::getSong();
		Engine::audioEngine()->audioDev()->stopProcessing();
		rideTheControl(registry, song, trackId, paramId, high, low);

		// 4. THE PROPERTY: not one node added, removed, moved or changed.
		QCOMPARE(recordedAutomationSignature(registry, trackId, paramId), recorded);

		// 5. SENSITIVITY: the same harness in write must change it.
		const ControlResult writeMode = registry->invoke(QStringLiteral("automation.mode_set"),
			QJsonObject{{QStringLiteral("track"), trackId},
				{QStringLiteral("parameter"), paramId},
				{QStringLiteral("mode"), qstr("write")}});
		QVERIFY2(writeMode.ok, qPrintable(writeMode.errorMessage));
		rideTheControl(registry, song, trackId, paramId, low, high);
		QVERIFY2(recordedAutomationSignature(registry, trackId, paramId) != recorded,
			"a write pass through the socket wrote nothing, so the read-mode assertion above "
			"proves nothing");

		// 6. Leave the session as this test found it: back to read. The mode is
		//    runtime state; the points the write pass added are this test's own
		//    automation and stay (they are what the undo stack holds).
		const ControlResult back = registry->invoke(QStringLiteral("automation.mode_set"),
			QJsonObject{{QStringLiteral("track"), trackId},
				{QStringLiteral("parameter"), paramId},
				{QStringLiteral("mode"), qstr("read")}});
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
	}

private:
	static QString qstr(const char* text) { return QString::fromLatin1(text); }

	//! The sentinel recordedAutomationSignature() returns for a parameter with no
	//! clip at all, so a caller can tell "no automation" from "unchanged".
	static QString notAutomated() { return QStringLiteral("<not automated>"); }

	//! The first automatable device parameter of the song, with its wire id
	//! (trk-<n> and "<plugin>/<index>") and its range - addressed exactly the way
	//! automation.get_state reports it. False when the song has none: a build
	//! that cannot load its instrument modules falls back to a DummyInstrument,
	//! which exposes no parameter at all.
	static bool findAutomatableParameter(ControlRegistry* registry, QString* trackId,
		QString* paramId, double* minValue, double* maxValue)
	{
		const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"));
		if (!state.ok) { return false; }
		for (const QJsonValue& t : state.result.value(QStringLiteral("tracks")).toArray())
		{
			const QJsonObject track = t.toObject();
			for (const QJsonValue& p : track.value(QStringLiteral("parameters")).toArray())
			{
				const QJsonObject parameter = p.toObject();
				const QString id = parameter.value(QStringLiteral("id")).toString();
				if (id.isEmpty()) { continue; }
				*trackId = track.value(QStringLiteral("id")).toString();
				*paramId = id;
				*minValue = parameter.value(QStringLiteral("min")).toDouble();
				*maxValue = parameter.value(QStringLiteral("max")).toDouble();
				return true;
			}
		}
		return false;
	}

	//! One parameter's entry as automation.get_state reports it (its mode, its
	//! value, its clip and its clip's record flag), or an empty object when the
	//! track or the parameter is not in the read-back.
	static QJsonObject parameterEntry(ControlRegistry* registry, const QString& trackId,
		const QString& paramId)
	{
		const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"),
			QJsonObject{{QStringLiteral("track"), trackId}});
		if (!state.ok) { return QJsonObject(); }
		for (const QJsonValue& t : state.result.value(QStringLiteral("tracks")).toArray())
		{
			const QJsonObject track = t.toObject();
			if (track.value(QStringLiteral("id")).toString() != trackId) { continue; }
			for (const QJsonValue& p : track.value(QStringLiteral("parameters")).toArray())
			{
				const QJsonObject parameter = p.toObject();
				if (parameter.value(QStringLiteral("id")).toString() == paramId) { return parameter; }
			}
		}
		return QJsonObject();
	}

	//! A fingerprint of one parameter's RECORDED automation, read back through
	//! the socket: every number the clip holds (ticks, in/out values, raw values
	//! and tangents - not a picked subset), serialised canonically because
	//! QJsonObject sorts its keys. Two identical strings mean no node was added,
	//! removed, moved or retangented anywhere in the clip.
	static QString recordedAutomationSignature(ControlRegistry* registry, const QString& trackId,
		const QString& paramId)
	{
		const QJsonObject automation = parameterEntry(registry, trackId, paramId)
			.value(QStringLiteral("automation")).toObject();
		if (automation.isEmpty()) { return notAutomated(); }
		return QString::fromUtf8(QJsonDocument(automation.value(QStringLiteral("points")).toArray())
			.toJson(QJsonDocument::Compact));
	}

	//! Ride the control the way a client does: alternate plugin.param_set calls -
	//! a manual move of the model the clip automates - with rendered periods, so
	//! the transport walks over the recorded curve while the control is moved.
	static void rideTheControl(ControlRegistry* registry, Song* song, const QString& trackId,
		const QString& paramId, double first, double second)
	{
		const QString pluginId = paramId.section(QLatin1Char('/'), 0, 0);
		const int index = paramId.section(QLatin1Char('/'), 1).toInt();
		song->playSong();
		song->getTimeline().setTicks(0);
		for (int i = 0; i < 16; ++i)
		{
			const ControlResult rode = registry->invoke(QStringLiteral("plugin.param_set"),
				QJsonObject{{QStringLiteral("target"), trackId},
					{QStringLiteral("plugin"), pluginId},
					{QStringLiteral("index"), index},
					{QStringLiteral("value"), i % 2 == 0 ? first : second}});
			QVERIFY2(rode.ok, qPrintable(rode.errorMessage));
			song->processNextBuffer();
		}
		QVERIFY2(song->getPlayPos().getTicks() > 0, "the harness never moved the transport");
		song->stop();
	}

private:
	//! The track initTestCase put a real device on (empty when this build
	//! exposes no loadable instrument module - the slots' own QVERIFY2 says so
	//! with the reason, rather than skipping silently).
	QString m_devicedTrack;
};

QTEST_GUILESS_MAIN(ControlAutomationModesTest)
#include "ControlAutomationModesTest.moc"
