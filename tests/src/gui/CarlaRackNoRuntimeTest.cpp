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
 *  carla_get_native_rack_plugin() answers nullptr - and this is the DEFAULT build: CMakeLists.txt sets
 *  LMMS_HAVE_CARLA only when pkg-config finds carla-native-plugin. CarlaInstrument's constructor then
 *  dereferenced that null descriptor, so picking Carla Rack (or Carla Patchbay) killed Zene with
 *  SIGSEGV (crash report signal=SIGSEGV(11) fault_addr=0x48) before a VST3 could even be named.
 *
 *  The cases drive what a user and an agent use - plugin.list, plugin.load, and the device card's
 *  "Replace Instrument..." action with the real device picker - and the project path a saved file
 *  takes. Without the guard in the two modules each case dies with the engine; with it every case
 *  proves the typed refusal, the reason, and that nothing of the project is lost.
 *
 *  In a build WITH a Carla runtime (this box has none: no pkg-config carla-native-plugin, so the build
 *  links the stand-in) the module hosts its plug-ins and these cases cannot say anything: they report
 *  Skipped, naming the runtime, exactly as CarlaVst2HostingTest reports Skipped without one.
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
#include "DummyInstrument.h"
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

//! Why the cases cannot say anything here, or empty when they can.
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

InstrumentTrack* newTrack()
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
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

QDomElement firstInstrumentElement(QDomDocument& doc)
{
	const QDomNodeList instruments = doc.elementsByTagName(QStringLiteral("instrument"));
	return instruments.isEmpty() ? QDomElement() : instruments.item(0).toElement();
}

//! @a element as text, for an exact comparison of what a file held before and after a round trip.
QString canon(const QDomElement& element)
{
	if (element.isNull()) { return QString(); }
	QDomDocument doc;
	doc.appendChild(doc.importNode(element, true));
	return doc.toString(-1).simplified();
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
		if (const QString why = missingCarlaRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		// The catalogue offers the device: a user CAN pick it, which is what makes the refusal below the
		// product's answer rather than a hidden entry.
		QVERIFY2(!deviceNamed(QStringLiteral("carlarack")).isEmpty(),
			"plugin.list does not offer the carlarack device this build ships");
	}

	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The agent path: plugin.load refuses, typed, and says what a Carla-less build means and where a VST3
	//! goes instead of a crash.
	void pluginLoadRefusesWithoutTheRuntime()
	{
		// AC1/AC2: the load that used to take the process down with SIGSEGV.
		const QJsonObject device = deviceNamed(QStringLiteral("carlarack"));
		InstrumentTrack* track = newTrack();
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		const int before = journalledSteps();

		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("device"), device.value(QStringLiteral("id"))}});
		QVERIFY2(!loaded.ok, "plugin.load accepted carlarack although this build links no Carla runtime");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("Carla runtime")), qPrintable(loaded.errorMessage));
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("VST3")), qPrintable(loaded.errorMessage));
		// A refused command journals nothing: no undo step appears for a device that was never loaded.
		QCOMPARE(journalledSteps(), before);
		// The process is still here, which is the whole point - and the track holds the placeholder every
		// unavailable instrument module leaves behind, not a half-built Carla Rack.
		QVERIFY(track->instrument() != nullptr);
		QCOMPARE(track->instrument()->nodeName(), QStringLiteral("dummyinstrument"));
		QVERIFY2(!track->instrument()->unavailableReason().isEmpty(), "the placeholder names no reason");
		delete track;
	}

	//! The GUI path: the device card's "Replace Instrument..." opens the real picker, which lists Carla Rack;
	//! accepting it runs the same plugin.load, so the app survives instead of dying in the menu.
	void theDeviceCardRefusesWithoutTheRuntime()
	{
		const QJsonObject device = deviceNamed(QStringLiteral("carlarack"));
		InstrumentTrack* track = newTrack();
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
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
		QCOMPARE(picked, device.value(QStringLiteral("id")).toString());
		// Still running, and the same refusal reached the track as on the agent path.
		QVERIFY(track->instrument() != nullptr);
		QVERIFY2(track->instrument()->nodeName() != QStringLiteral("carlarack"),
			"the card loaded a Carla Rack over the track's instrument in a build that cannot host one");
		QCOMPARE(track->instrument()->nodeName(), QStringLiteral("dummyinstrument"));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! Carla Patchbay is the same host path with the same null descriptor: it must be refused the same way.
	void carlaPatchbayIsRefusedTheSameWay()
	{
		const QJsonObject device = deviceNamed(QStringLiteral("carlapatchbay"));
		if (device.isEmpty()) { QSKIP("this build does not ship the carlapatchbay module"); }
		InstrumentTrack* track = newTrack();
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), control::trackIdOf(track)}, {QStringLiteral("device"), device.value(QStringLiteral("id"))}});
		QVERIFY2(!loaded.ok, "plugin.load accepted carlapatchbay although this build links no Carla runtime");
		QCOMPARE(loaded.errorKind, ControlErrorKind::Refused);
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("Carla runtime")), qPrintable(loaded.errorMessage));
		QVERIFY(track->instrument() != nullptr);
		QCOMPARE(track->instrument()->nodeName(), QStringLiteral("dummyinstrument"));
		delete track;
	}

	//! The project path: a file written where Carla Rack did host a VST3 opens here as a placeholder and keeps
	//! its own XML through a save, instead of dying while it loads.
	void aSavedCarlaRackProjectKeepsItsXml()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		const QString base = m_dir.filePath(QStringLiteral("base.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(base));

		// The instrument element a machine WITH a Carla runtime writes: Carla's own state inside the
		// instrument the project names.
		QDomDocument doc;
		QVERIFY2(doc.setContent(readAll(base)), "the project this build just saved does not parse");
		QDomElement instrument = firstInstrumentElement(doc);
		QVERIFY(!instrument.isNull());
		instrument.setAttribute(QStringLiteral("name"), QStringLiteral("carlarack"));
		QDomElement carlaState = doc.createElement(QStringLiteral("CARLA-PROJECT"));
		carlaState.setAttribute(QStringLiteral("version"), QStringLiteral("5"));
		QDomElement rack = doc.createElement(QStringLiteral("RackState"));
		rack.setAttribute(QStringLiteral("plugins"), QStringLiteral("1"));
		rack.appendChild(doc.createTextNode(QStringLiteral("VST3:/usr/lib/vst3/ZeneProbe.vst3")));
		carlaState.appendChild(rack);
		instrument.appendChild(carlaState);
		const QString savedXml = canon(instrument);
		QVERIFY2(savedXml.contains(QStringLiteral("name=\"carlarack\"")), qPrintable(savedXml));
		const QString variant = m_dir.filePath(QStringLiteral("carlarack.mmp"));
		{
			QFile file(variant);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write(doc.toString().toUtf8());
		}

		// Open it here: the track comes up as a placeholder, and this call is the one that crashed.
		Engine::getSong()->loadProject(variant);
		QCoreApplication::processEvents();
		InstrumentTrack* opened = nullptr;
		for (Track* candidate : Engine::getSong()->tracks())
		{
			if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(candidate))
			{
				opened = instrumentTrack;
				break;
			}
		}
		QVERIFY2(opened != nullptr, "the project did not open");
		QVERIFY(opened->instrument() != nullptr);
		QCOMPARE(opened->instrument()->nodeName(), QStringLiteral("dummyinstrument"));

		// Save it again: what the file held for Carla Rack is still in it, character for character.
		const QString reopened = m_dir.filePath(QStringLiteral("reopened.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(reopened), "the reopened project could not be saved");
		QDomDocument after;
		QVERIFY2(after.setContent(readAll(reopened)), "the project saved after the open does not parse");
		const QDomElement kept = firstInstrumentElement(after);
		QVERIFY(!kept.isNull());
		QCOMPARE(canon(kept), savedXml);
	}

private:
	QTemporaryDir m_dir;
	QTemporaryDir m_home;
};

QTEST_MAIN(CarlaRackNoRuntimeTest)
#include "CarlaRackNoRuntimeTest.moc"
