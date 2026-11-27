/*
 * CarlaRackNoRuntimeTest.cpp - Carla Rack / Carla Patchbay in a build with no Carla runtime (Q050-359)
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
 * License along with this program (see COPYING); if not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! Q050-359: a user who opens Carla Rack to load a VST3 inside it.
 *
 *  A build without a system Carla links the weak stand-in (plugins/CarlaBase/DummyCarla.cpp), whose
 *  carla_get_native_rack_plugin() / carla_get_native_patchbay_plugin() answer nullptr - and that is the
 *  DEFAULT build: CMakeLists.txt sets LMMS_HAVE_CARLA only when pkg-config finds carla-native-plugin.
 *  Two things then have to be true, and this file checks both separately:
 *
 *   1. ASKING for Carla Rack on a track that already holds an instrument REFUSES and CHANGES NOTHING.
 *      The guard in the module stopped the SIGSEGV (the constructor dereferenced the null descriptor);
 *      the seam - InstrumentTrack::loadInstrument() - now builds the candidate first and only replaces
 *      the instrument once it is the device that was asked for. The musician keeps their instrument,
 *      its settings, the track's name and their project file, and the refusal is typed and says why.
 *   2. OPENING A SAVED PROJECT that names Carla Rack uses the placeholder (Q050-018: DummyInstrument)
 *      and keeps the file's own XML through a save, so nothing of that device is lost either.
 *
 *  Everything goes through what a user and an agent use: plugin.list and plugin.load, the device card's
 *  "Replace Instrument..." with the real device picker, and project open/save.
 *
 *  In a build WITH a Carla runtime (this box has none) the module hosts its plug-ins and the refusal
 *  cases cannot say anything: they report Skipped, naming the runtime, as CarlaVst2HostingTest does from
 *  the other side. The placeholder cases do not depend on that.
 */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "DevicePicker.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "NotePlayHandle.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "ShellSelection.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

/*! Whether the carlarack module this build loads has a Carla runtime underneath it. The module exports
 *  carla_get_native_rack_plugin(), which the weak stand-in answers nullptr (DummyCarla.cpp:11) and a real
 *  runtime answers with its Rack descriptor - so the probe asks the shipping module itself rather than
 *  guessing from a file the machine may or may not have. */
bool carlaRuntimeInThisBuild()
{
	const PluginFactory::PluginInfo& info = getPluginFactory()->pluginInfo("carlarack");
	if (info.isNull() || info.library == nullptr) { return false; }
	using NativeGetter = void* (*)();
	const auto getter = reinterpret_cast<NativeGetter>(info.library->resolve("carla_get_native_rack_plugin"));
	return getter != nullptr && getter() != nullptr;
}

//! Why the refusal cases cannot say anything here, or empty when they can.
QString missingCarlaRuntime()
{
	if (getPluginFactory()->pluginInfo("carlarack").isNull())
	{
		return QStringLiteral("this build has no carlarack module to load");
	}
	if (carlaRuntimeInThisBuild())
	{
		return QStringLiteral("this build links a real Carla runtime, so Carla Rack does host plug-ins: the "
			"no-runtime refusal is not the path under test");
	}
	return QString();
}

//! plugin.list's entry for the built-in module @a name, or an empty object.
QJsonObject deviceNamed(const QString& name)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("instrument")}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("name")).toString() == name) { return entry; }
	}
	return {};
}

//! How many undo steps the engine has journalled right now (control.transactions is read-only).
int journalledSteps()
{
	return run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray().size();
}

InstrumentTrack* newTrack(const QString& name = QString())
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track != nullptr && !name.isEmpty()) { track->setName(name); }
	QCoreApplication::processEvents();
	return track;
}

//! The device card's action menu for @a track (the track selected first), filled as it opens.
QMenu* cardMenu(DevicePanel* panel, InstrumentTrack* track)
{
	ShellSelection::instance()->setTrack(track);
	panel->rebuild();
	QCoreApplication::processEvents();
	for (DeviceCard* card : panel->findChildren<DeviceCard*>())
	{
		auto* actions = card->findChild<QToolButton*>(QStringLiteral("deviceActions"));
		if (card->spec().instrumentTrack != track || actions == nullptr || actions->menu() == nullptr) { continue; }
		emit actions->menu()->aboutToShow();
		return actions->menu();
	}
	return nullptr;
}

//! Runs @a handle on the modal dialog the next action opens, once it is up (it is shown inside exec()).
void onceModal(const std::function<void(QWidget*)>& handle, int triesLeft = 100)
{
	QTimer::singleShot(10, [handle, triesLeft] {
		QWidget* modal = QApplication::activeModalWidget();
		if (modal == nullptr && triesLeft > 0) { onceModal(handle, triesLeft - 1); return; }
		handle(modal);
	});
}

QString readAll(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

//! @a element as text, for an exact comparison of what a file held before and after a round trip.
QString canon(const QDomElement& element)
{
	if (element.isNull()) { return QString(); }
	QDomDocument doc;
	doc.appendChild(doc.importNode(element, true));
	return doc.toString(-1).simplified();
}

//! The first <instrument> element of @a doc, or a null element.
QDomElement firstInstrumentElement(QDomDocument& doc)
{
	const QDomNodeList instruments = doc.elementsByTagName(QStringLiteral("instrument"));
	return instruments.isEmpty() ? QDomElement() : instruments.item(0).toElement();
}

//! The first <track> element of @a doc that holds an <instrument>, or a null element.
QDomElement firstInstrumentTrackElement(QDomDocument& doc)
{
	const QDomNodeList tracks = doc.elementsByTagName(QStringLiteral("track"));
	for (int i = 0; i < tracks.size(); ++i)
	{
		const QDomElement track = tracks.item(i).toElement();
		if (!track.firstChildElement(QStringLiteral("instrument")).isNull()) { return track; }
	}
	return QDomElement();
}

} // namespace

class CarlaRackNoRuntimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid() && m_home.isValid());
		// A private home: no user configuration and no plug-in scan of whatever the machine has installed.
		for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
		{
			qputenv(name, m_home.path().toLocal8Bit());
		}
		// The build's own modules, the weak Carla stand-in among them: the arrangement a release without a
		// system Carla ships (the stand-in sits beside the modules and resolves carlarack's own dependency).
		qputenv("LMMS_PLUGIN_DIR", QStringLiteral(LMMS_TEST_PLUGIN_DIR).toLocal8Bit());
		qputenv("LMMS_STEM_OFFLINE", "1");
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		// The catalogue offers the device: a user CAN pick it, which is what makes the refusal the product's
		// answer rather than a hidden entry.
		QVERIFY2(!deviceNamed(QStringLiteral("carlarack")).isEmpty(),
			"plugin.list does not offer the carlarack device this build ships");
	}

	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/*! AC1/AC2, agent path: plugin.load of Carla Rack on a track that holds a configured instrument is
	 *  REFUSED, typed, and the musician's instrument, its settings, the track's name and the saved project
	 *  are exactly what they were - and no undo step appears for a device that never loaded. */
	void pluginLoadRefusalKeepsTheInstrument()
	{
		if (const QString why = missingCarlaRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = newTrack(QStringLiteral("Bass"));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		const int index = 0;
		const QJsonObject before = run(QStringLiteral("plugin.param_get"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), index}}).result.value(QStringLiteral("parameter")).toObject();
		QVERIFY2(!before.isEmpty(), "tripleoscillator exposes no parameter to configure");
		const double configured = before.value(QStringLiteral("min")).toDouble() != before.value(QStringLiteral("value")).toDouble()
			? before.value(QStringLiteral("min")).toDouble() : before.value(QStringLiteral("max")).toDouble();
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), index}, {QStringLiteral("value"), configured}}).ok);

		const QString nameBefore = track->name();
		const int stepsBefore = journalledSteps();
		const QString savedBefore = savedTrackXml(QStringLiteral("before"));

		const QJsonObject device = deviceNamed(QStringLiteral("carlarack"));
		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("device"), device.value(QStringLiteral("id"))}});
		QVERIFY2(!loaded.ok, "plugin.load accepted carlarack although this build links no Carla runtime");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("Carla runtime")), qPrintable(loaded.errorMessage));
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("VST3")), qPrintable(loaded.errorMessage));

		// The musician's instrument is still there, with its settings, its track name and its undo history.
		QVERIFY(track->instrument() != nullptr);
		QCOMPARE(track->instrument()->nodeName(), QStringLiteral("tripleoscillator"));
		QCOMPARE(track->name(), nameBefore);
		QCOMPARE(journalledSteps(), stepsBefore);
		const QJsonObject after = run(QStringLiteral("plugin.param_get"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), index}}).result.value(QStringLiteral("parameter")).toObject();
		QCOMPARE(after.value(QStringLiteral("value")).toDouble(), configured);
		// ... and the save taken afterwards is the save taken before: the refusal is not in the project.
		QCOMPARE(savedTrackXml(QStringLiteral("after")), savedBefore);
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	/*! AC1/AC2, GUI path: the device card's "Replace Instrument..." opens the real picker, which lists Carla
	 *  Rack; accepting it runs the same plugin.load, so the card is refused too and the track keeps its
	 *  configured instrument instead of the app dying in the menu. */
	void theDeviceCardRefusalKeepsTheInstrument()
	{
		if (const QString why = missingCarlaRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		InstrumentTrack* track = newTrack(QStringLiteral("Bass"));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), 0}, {QStringLiteral("value"), 0.75}}).ok);
		const QString nameBefore = track->name();
		const int stepsBefore = journalledSteps();
		const QString savedBefore = savedTrackXml(QStringLiteral("gui-before"));

		DevicePanel panel;
		QMenu* menu = cardMenu(&panel, track);
		QVERIFY2(menu != nullptr, "the instrument has no device card with an action menu");
		QAction* replace = lmms::test::menuAction(menu, {QStringLiteral("Replace Instrument...")});
		QVERIFY2(replace != nullptr, "the device card has no Replace Instrument... action");
		QString row;
		QString picked;
		onceModal([&row, &picked](QWidget* modal) {
			auto* picker = qobject_cast<DevicePicker*>(modal);
			if (picker == nullptr) { if (modal != nullptr) { modal->close(); } return; }
			picker->setQuery(QStringLiteral("Carla Rack"));
			row = picker->shownRowText(0);
			picked = picker->selectedDevice();
			if (picked.isEmpty()) { picker->reject(); return; }
			static_cast<QDialog*>(picker)->accept();
		});
		replace->trigger();
		QCoreApplication::processEvents();
		QVERIFY2(row.contains(QStringLiteral("Carla Rack")), qPrintable(row));
		QCOMPARE(picked, deviceNamed(QStringLiteral("carlarack")).value(QStringLiteral("id")).toString());

		// The drop of the card went through plugin.load and was refused: nothing of the track moved.
		QVERIFY(track->instrument() != nullptr);
		QCOMPARE(track->instrument()->nodeName(), QStringLiteral("tripleoscillator"));
		QCOMPARE(track->name(), nameBefore);
		QCOMPARE(journalledSteps(), stepsBefore);
		QCOMPARE(run(QStringLiteral("plugin.param_get"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), 0}}).result.value(QStringLiteral("parameter")).toObject()
			.value(QStringLiteral("value")).toDouble(), 0.75);
		QCOMPARE(savedTrackXml(QStringLiteral("gui-after")), savedBefore);
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! The Patchbay is the same host path with its own null descriptor: a refusal there changes nothing either.
	void carlaPatchbayRefusalKeepsTheInstrument()
	{
		if (const QString why = missingCarlaRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		const QJsonObject device = deviceNamed(QStringLiteral("carlapatchbay"));
		if (device.isEmpty()) { QSKIP("this build does not ship the carlapatchbay module"); }
		InstrumentTrack* track = newTrack(QStringLiteral("Patch"));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		const QString nameBefore = track->name();
		const int stepsBefore = journalledSteps();
		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("device"), device.value(QStringLiteral("id"))}});
		QVERIFY2(!loaded.ok, "plugin.load accepted carlapatchbay although this build links no Carla runtime");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("Carla runtime")), qPrintable(loaded.errorMessage));
		QVERIFY(track->instrument() != nullptr);
		QCOMPARE(track->instrument()->nodeName(), QStringLiteral("tripleoscillator"));
		QCOMPARE(track->name(), nameBefore);
		QCOMPARE(journalledSteps(), stepsBefore);
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	/*! A project written where Carla Rack did host a VST3 opens here as the Q050-018 placeholder and keeps its
	 *  own XML through a save; asking for Carla Rack again on that track is refused and leaves the placeholder
	 *  and the file's XML alone. */
	void aSavedCarlaRackProjectKeepsItsPlaceholder()
	{
		const QString variant = m_dir.filePath(QStringLiteral("carlarack.mmp"));
		const QString savedXml = writeCarlaRackProject(variant);
		QVERIFY2(!savedXml.isEmpty(), "the fixture project was not written");

		Engine::getSong()->loadProject(variant);
		QCoreApplication::processEvents();
		InstrumentTrack* opened = firstInstrumentTrack();
		QVERIFY2(opened != nullptr, "the project did not open");
		QVERIFY(opened->instrument() != nullptr);
		QCOMPARE(opened->instrument()->nodeName(), QStringLiteral("dummyinstrument"));
		QVERIFY2(!opened->instrument()->unavailableReason().isEmpty(), "the placeholder names no reason");

		// Save it again: what the file held for Carla Rack is still in it, character for character.
		const QString reopened = m_dir.filePath(QStringLiteral("reopened.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(reopened), "the reopened project could not be saved");
		QDomDocument after;
		QVERIFY2(after.setContent(readAll(reopened)), "the project saved after the open does not parse");
		QCOMPARE(canon(firstInstrumentElement(after)), savedXml);

		// Asking for Carla Rack on that track is refused, and the placeholder keeps the file's XML.
		if (missingCarlaRuntime().isEmpty())
		{
			const int stepsBefore = journalledSteps();
			const ControlResult loaded = run(QStringLiteral("plugin.load"),
				{{QStringLiteral("target"), control::trackIdOf(opened)},
					{QStringLiteral("device"), deviceNamed(QStringLiteral("carlarack")).value(QStringLiteral("id"))}});
			QVERIFY2(!loaded.ok, "plugin.load accepted carlarack on a track holding its own placeholder");
			QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
			QCOMPARE(opened->instrument()->nodeName(), QStringLiteral("dummyinstrument"));
			QCOMPARE(journalledSteps(), stepsBefore);
			const QString again = m_dir.filePath(QStringLiteral("reopened-again.mmp"));
			QVERIFY(Engine::getSong()->saveProjectFile(again));
			QDomDocument third;
			QVERIFY(third.setContent(readAll(again)));
			QCOMPARE(canon(firstInstrumentElement(third)), savedXml);
		}
	}

private:
	//! Saves the open song to m_dir/@a label.mmp and returns its instrument track's <track> element, canonical.
	QString savedTrackXml(const QString& label)
	{
		const QString path = m_dir.filePath(label + QStringLiteral(".mmp"));
		if (!Engine::getSong()->saveProjectFile(path)) { return QString(); }
		QDomDocument doc;
		if (!doc.setContent(readAll(path))) { return QString(); }
		return canon(firstInstrumentTrackElement(doc));
	}

	//! @a path: this session's project with its instrument element rewritten to the Carla Rack a machine WITH a
	//! runtime would have saved. Returns that element, canonical.
	QString writeCarlaRackProject(const QString& path)
	{
		InstrumentTrack* track = newTrack(QStringLiteral("Carla Rack"));
		if (track == nullptr || track->loadInstrument(QStringLiteral("tripleoscillator")) == nullptr) { return QString(); }
		const QString base = m_dir.filePath(QStringLiteral("base.mmp"));
		if (!Engine::getSong()->saveProjectFile(base)) { return QString(); }
		QDomDocument doc;
		if (!doc.setContent(readAll(base))) { return QString(); }
		QDomElement instrument = firstInstrumentElement(doc);
		if (instrument.isNull()) { return QString(); }
		instrument.setAttribute(QStringLiteral("name"), QStringLiteral("carlarack"));
		QDomElement carlaState = doc.createElement(QStringLiteral("CARLA-PROJECT"));
		carlaState.setAttribute(QStringLiteral("version"), QStringLiteral("5"));
		QDomElement rack = doc.createElement(QStringLiteral("RackState"));
		rack.setAttribute(QStringLiteral("plugins"), QStringLiteral("1"));
		rack.appendChild(doc.createTextNode(QStringLiteral("VST3:/usr/lib/vst3/ZeneProbe.vst3")));
		carlaState.appendChild(rack);
		instrument.appendChild(carlaState);
		const QString saved = canon(instrument);
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly)) { return QString(); }
		file.write(doc.toString().toUtf8());
		return saved;
	}

	InstrumentTrack* firstInstrumentTrack()
	{
		for (Track* candidate : Engine::getSong()->tracks())
		{
			if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(candidate)) { return instrumentTrack; }
		}
		return nullptr;
	}

	QTemporaryDir m_dir;
	QTemporaryDir m_home;
};

QTEST_MAIN(CarlaRackNoRuntimeTest)
#include "CarlaRackNoRuntimeTest.moc"
