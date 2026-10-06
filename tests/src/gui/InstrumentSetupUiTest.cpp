/*
 * InstrumentSetupUiTest.cpp - 0.5.0 F2 interface: an instrument track's output buses, MIDI parts,
 *                             layer rack and per-voice modulation, driven like a person drives them
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

/*! The wave 2 F2 instrument features had no interface (docs/KNOWN-LIMITATIONS.md "F2, instruments").
 *  Each case reaches its surface the way a person does - the track's header / Inspector "More" menu
 *  (ShellCommandMenus::populateTrack) and the instrument's device card (fillDeviceActionMenu) - then works
 *  it only through the surface's own rows, buttons and forms, and checks the result against the model the
 *  commands change, and that control.undo takes each edit back (the surface runs commands, so the journal
 *  stays honest). */

#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QToolButton>

#include "CommandForm.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlInstrumentCommands.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "DevicePanel.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentCommandMenus.h"
#include "InstrumentMidiParts.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "RackZones.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"
#include "../core/ReversibilityTestSupport.h"

//! The directory holding the in-tree CLAP instrument fixture (its multi-output class has output buses);
//! set by tests/CMakeLists.txt when this build has the fixture.
#ifndef CLAP_TEST_INSTRUMENT_DIR
#define CLAP_TEST_INSTRUMENT_DIR ""
#endif

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;
using revtest::run;

namespace
{

InstrumentTrack* trackOf(const QString& id)
{
	ControlResult ignored;
	return control::resolveInstrumentTarget(id, &ignored);
}

//! The submenu path to an entry of the instrument's "Instrument Setup" menu.
QStringList setupPath(const QString& group, const QString& entry)
{
	return {InstrumentCommandMenus::submenuTitle(), group, entry};
}

//! The entry at @a path of the track's header / Inspector menu: it must declare @a command.
QAction* trackMenuEntry(QMenu* menu, const QString& track, const QStringList& path, const QString& command)
{
	ControlResult ignored;
	ShellCommandMenus::populateTrack(menu, control::resolveTrack(track, &ignored));
	QAction* action = menuAction(menu, path);
	return action != nullptr && action->property("controlCommand").toString() == command ? action : nullptr;
}

//! The instrument's device card's action menu (Device panel, the track selected), filled as it opens.
QMenu* cardMenu(DevicePanel* panel, const QString& track)
{
	ControlResult ignored;
	ShellSelection::instance()->setTrack(control::resolveTrack(track, &ignored));
	panel->rebuild();
	QCoreApplication::processEvents();
	for (DeviceCard* card : panel->findChildren<DeviceCard*>())
	{
		auto* actions = card->findChild<QToolButton*>(QStringLiteral("deviceActions"));
		if (card->spec().instrumentTrack == nullptr || actions == nullptr || actions->menu() == nullptr) { continue; }
		emit actions->menu()->aboutToShow();
		return actions->menu();
	}
	return nullptr;
}

//! The index of the action labelled @a label in @a spec.
int actionIndex(const ListSpec& spec, const QString& label)
{
	for (int i = 0; i < spec.actions.size(); ++i)
	{
		if (spec.actions.at(i).label == label) { return i; }
	}
	return -1;
}

//! The dev-<n> id plugin.list gives the CLAP class @a clapId, or empty.
QString clapDevice(const QString& clapId)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), QStringLiteral("clap")}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("clap_id")).toString() == clapId) { return device.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! The form the last form-asking action opened under @a dialog, filled with @a fields and run.
ControlResult fillAndRun(QWidget* dialog, const QJsonObject& fields)
{
	const QList<CommandForm*> forms = dialog->findChildren<CommandForm*>();
	if (forms.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no form opened")); }
	CommandForm* form = forms.last();
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		if (!form->setField(it.key(), it.value())) { return ControlResult::failure(ControlErrorKind::InvalidArgs, it.key()); }
	}
	form->accept();
	return form->result();
}

const RackZone* partOf(const QString& track, int index)
{
	return trackOf(track)->midiParts().zones().zone(index);
}

} // namespace

class InstrumentSetupUiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's instruments, for the layers and the CLAP host
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ShellSelection::instance()->setTrack(nullptr);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! MX-114: the multi-output CLAP fixture's buses, from the track's menu and the device card; a channel made
	//! for each unrouted bus, the bus un-routed, then routed to a channel through the row's form; undo takes it back.
	void outputBusesAreListedRoutedAndUndone()
	{
		if (getPluginFactory()->pluginInfo("clapinstrument").isNull()) { QSKIP("this build has no clapinstrument module"); }
		const QString fixtureDir = QStringLiteral(CLAP_TEST_INSTRUMENT_DIR);
		if (fixtureDir.isEmpty() || !QFileInfo::exists(fixtureDir)) { QSKIP("this build has no CLAP instrument fixture"); }
		ConfigManager::inst()->setVSTDir(fixtureDir);  // the CLAP scan walks this directory too
		const QString device = clapDevice(QStringLiteral("org.lmms.test.clap-instrument-multiout"));
		QVERIFY2(!device.isEmpty(), "plugin.list does not carry the fixture's multi-output class");
		const QString target = revtest::addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device}}).ok);
		InstrumentOutputBuses* model = trackOf(target)->instrument()->outputBuses();
		QVERIFY(model != nullptr);

		QMenu header;
		QVERIFY(trackMenuEntry(&header, target, setupPath(QStringLiteral("Output Buses"), QStringLiteral("Output Buses...")),
			QStringLiteral("plugin.output_buses")) != nullptr);
		DevicePanel panel;
		QMenu* card = cardMenu(&panel, target);
		QVERIFY2(card != nullptr, "the instrument has no device card with an action menu");
		QVERIFY(menuAction(card, setupPath(QStringLiteral("Output Buses"), QStringLiteral("Output Buses..."))) != nullptr);

		const ListSpec spec = CommandLists::outputBuses(target);
		CommandListDialog dialog(spec);
		QCOMPARE(dialog.rowCount(), 2);
		QCOMPARE(dialog.row(1).value(QStringLiteral("name")).toString(), QStringLiteral("Aux"));
		const auto channelsBefore = Engine::mixer()->numChannels();
		QVERIFY(dialog.runExtra(0).ok);  // Create a Channel for Each Bus
		QCOMPARE(Engine::mixer()->numChannels(), channelsBefore + 1);
		QVERIFY(model->routeOf(1) >= 0);
		QVERIFY(!dialog.row(1).value(QStringLiteral("channel")).toString().isEmpty());  // the list was read again

		QVERIFY(dialog.runAction(1, actionIndex(spec, QStringLiteral("Un-route"))).ok);
		QCOMPARE(model->routeOf(1), -1);
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		QCOMPARE(dialog.runAction(1, actionIndex(spec, QStringLiteral("Route to Channel..."))).errorKind, ControlErrorKind::Busy);
		auto* form = dialog.findChild<CommandForm*>();
		QVERIFY2(form != nullptr, "Route to Channel... opened no form");
		QVERIFY(form->setField(QStringLiteral("channel"), channel));
		form->accept();
		QVERIFY2(form->result().ok, qPrintable(form->result().errorMessage));
		QCOMPARE(control::channelIdOf(Engine::mixer()->mixerChannel(index)), QStringLiteral("ch-%1").arg(model->routeOf(1)));
		REV_UNDO_OR_FAIL();
		QCOMPARE(model->routeOf(1), -1);
		QVERIFY(!dialog.runAction(0, actionIndex(spec, QStringLiteral("Un-route"))).ok);  // bus 0 is the main output: refused
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! MX-114: a track's MIDI parts from its menu and its device card - one added through the list's form, edited in
	//! place (a field left blank keeps its value), removed; control.undo takes the edit and the removal back.
	void midiPartsAreAddedEditedRemovedAndUndone()
	{
		const QString track = revtest::addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable instrument"); }
		QMenu header;
		QVERIFY(trackMenuEntry(&header, track, setupPath(QStringLiteral("MIDI Parts"), QStringLiteral("MIDI Parts...")),
			QStringLiteral("plugin.midi_parts")) != nullptr);
		DevicePanel panel;
		QMenu* card = cardMenu(&panel, track);
		QVERIFY(card != nullptr && menuAction(card, setupPath(QStringLiteral("MIDI Parts"), QStringLiteral("Add a MIDI Part..."))) != nullptr);

		const ListSpec spec = CommandLists::midiParts(track);
		CommandListDialog dialog(spec);
		QCOMPARE(dialog.rowCount(), 0);
		QCOMPARE(dialog.runExtra(0).errorKind, ControlErrorKind::Busy);  // Add Part... asks
		const ControlResult added = fillAndRun(&dialog, {{QStringLiteral("midi_channel"), 3}, {QStringLiteral("low_key"), 60}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(dialog.rowCount(), 1);
		QCOMPARE(partOf(track, 0)->chain, 2);  // channel 3 is index 2

		QCOMPARE(dialog.runAction(0, actionIndex(spec, QStringLiteral("Edit..."))).errorKind, ControlErrorKind::Busy);
		const ControlResult edited = fillAndRun(&dialog, {{QStringLiteral("midi_channel"), 5}, {QStringLiteral("high_key"), 72}});
		QVERIFY2(edited.ok, qPrintable(edited.errorMessage));
		QCOMPARE(partOf(track, 0)->chain, 4);
		QCOMPARE(partOf(track, 0)->highKey, 72);
		QCOMPARE(partOf(track, 0)->lowKey, 60);  // left blank: kept
		QCOMPARE(dialog.row(0).value(QStringLiteral("midi_channel")).toInt(), 5);
		REV_UNDO_OR_FAIL();
		QCOMPARE(partOf(track, 0)->chain, 2);
		QCOMPARE(partOf(track, 0)->highKey, 127);

		QVERIFY(dialog.runAction(0, actionIndex(spec, QStringLiteral("Remove"))).ok);
		QCOMPARE(trackOf(track)->midiParts().zones().zoneCount(), 0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(trackOf(track)->midiParts().zones().zoneCount(), 1);
		ShellSelection::instance()->setTrack(nullptr);
	}
};

QTEST_MAIN(InstrumentSetupUiTest)
#include "InstrumentSetupUiTest.moc"
