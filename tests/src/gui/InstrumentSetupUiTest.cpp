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

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QPointer>
#include <QJsonArray>
#include <QMenu>
#include <QTextStream>
#include <QToolButton>

#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
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
#include "InstrumentLayers.h"
#include "InstrumentMidiParts.h"
#include "LayerRackPanel.h"
#include "InstrumentOutputBuses.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "ModulationLayer.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
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

//! The dev-<n> id plugin.list gives the built-in instrument module @a name, or empty.
QString builtinDevice(const QString& name)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("instrument")}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("name")).toString() == name) { return device.value(QStringLiteral("id")).toString(); }
	}
	return QString();
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

//! The track's state saved and read back into the same track - the project round trip of one track.
void saveAndReload(InstrumentTrack* track)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("trackcontainer"));
	doc.appendChild(root);
	const QDomElement saved = track->saveState(doc, root);
	track->restoreState(saved);
}

//! The track's own state as the XML an inserted layer is built from. The journalling id is left
//! out: the layer is a track of its own and must not claim the parent's journalling object.
QString layerTemplateXml(InstrumentTrack* track)
{
	QDomDocument doc;
	QDomElement wrapper = doc.createElement(QStringLiteral("wrapper"));
	doc.appendChild(wrapper);
	QDomElement state = track->saveState(doc, wrapper);
	state.removeChild(state.firstChildElement(QStringLiteral("journallingObject")));
	QString xml;
	QTextStream stream(&xml);
	state.save(stream, -1);
	return xml;
}

//! The loudest sample @a bus carries while one note plays on @a track's clip for a few periods (its layers get the
//! note too); the note is stopped and its clip removed again afterwards.
float renderedPeak(InstrumentTrack* track, AudioBusHandle* bus)
{
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(192), TimePos(0), 60), false);
	Song* song = Engine::getSong();
	song->setPlayPos(0, Song::PlayMode::Song);
	song->playSong();
	float peak = 0.0f;
	for (int period = 0; period < 8; ++period)
	{
		Engine::audioEngine()->renderNextPeriod();
		for (const SampleFrame& frame : bus->buffer()) { peak = std::max({peak, std::abs(frame.left()), std::abs(frame.right())}); }
	}
	song->stop();
	for (int period = 0; period < 64 && !Engine::audioEngine()->playHandles().empty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }
	track->removeClip(clip);
	delete clip;
	return peak;
}

//! The row of @a dialog whose @a field is @a value, or -1.
int rowWhere(const CommandListDialog& dialog, const QString& field, const QString& value)
{
	for (int row = 0; row < dialog.rowCount(); ++row)
	{
		if (dialog.row(row).value(field).toString() == value) { return row; }
	}
	return -1;
}

//! The song's modulator @a id ("modulator-<n>").
const Modulator* modulatorOf(const QString& id)
{
	return Engine::getSong()->modulationLayer().layer().modulator(id.section(QLatin1Char('-'), 1).toInt());
}

//! The top-level window whose title ends with @a suffix, or nullptr.
QWidget* windowEndingWith(const QString& suffix)
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->isVisible() && widget->windowTitle().endsWith(suffix)) { return widget; }
	}
	return nullptr;
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
		Engine::audioEngine()->audioDev()->stopProcessing();  // the layer case renders periods itself
		NotePlayHandleManager::init();  // a test binary has no main() to do it (CLAUDE-READ-FIRST section 4)
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

	//! MX-112: the layer rack panel, from the track's menu and the device card - a layer added, muted, soloed (the
	//! track's own instrument falls silent), its level set, its instrument opened, then removed (its window closes
	//! with it); each a journalled step, the mix saved with the track and its zones listed.
	void layerRackMixesOpensAndRemovesALayer()
	{
		// A built-in synth on the track and in the layer, so a played note is heard (the first loadable instrument
		// can be a silent CLAP test fixture).
		const QString device = builtinDevice(QStringLiteral("tripleoscillator"));
		if (device.isEmpty()) { QSKIP("this build has no tripleoscillator module"); }
		const QString track = revtest::addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), device}}).ok);
		QMenu header;
		QVERIFY(trackMenuEntry(&header, track, setupPath(QStringLiteral("Layers"), QStringLiteral("Layer Rack...")),
			QStringLiteral("rack.layers")) != nullptr);
		DevicePanel devices;
		QMenu* card = cardMenu(&devices, track);
		QVERIFY(card != nullptr && menuAction(card, setupPath(QStringLiteral("Layers"), QStringLiteral("Add a Layer..."))) != nullptr);

		LayerRackPanel panel(track);
		QCOMPARE(panel.rowCount(), 1);  // layer-0, the track's own instrument
		QVERIFY2(panel.addLayer(device).ok, qPrintable(panel.statusText()));  // what Add Layer... runs after its picker
		QCOMPARE(panel.rowCount(), 2);
		InstrumentLayers& layers = trackOf(track)->layers();
		AudioBusHandle* own = trackOf(track)->audioBusHandle();
		AudioBusHandle* bus = layers.layer(1)->audioBusHandle();

		QTest::mouseClick(panel.muteButton(1), Qt::LeftButton);
		QVERIFY(layers.mix(1).muted);
		QCOMPARE(bus->gain(), 0.0f);
		QVERIFY(panel.muteButton(1)->isChecked());
		REV_UNDO_OR_FAIL();
		QCOMPARE(bus->gain(), 1.0f);
		QTest::mouseClick(panel.soloButton(1), Qt::LeftButton);
		QCOMPARE(own->gain(), 0.0f);  // a layer soloed: the track's own instrument is not
		QCOMPARE(bus->gain(), 1.0f);
		QTest::mouseClick(panel.soloButton(1), Qt::LeftButton);
		QCOMPARE(own->gain(), 1.0f);
		panel.levelBox(1)->setValue(50.0);
		emit panel.levelBox(1)->editingFinished();
		QCOMPARE(layers.mix(1).level, 0.5f);
		QCOMPARE(bus->gain(), 0.5f);
		// Heard, not only stored: the layer's own bus sounds, and is silent while the layer is muted.
		QVERIFY2(renderedPeak(trackOf(track), bus) > 0.0f, "the layer played nothing");
		QTest::mouseClick(panel.muteButton(1), Qt::LeftButton);
		QCOMPARE(renderedPeak(trackOf(track), bus), 0.0f);
		QTest::mouseClick(panel.muteButton(1), Qt::LeftButton);
		saveAndReload(trackOf(track));
		QCOMPARE(trackOf(track)->layers().mix(1).level, 0.5f);  // saved with the track
		panel.refresh();

		QTest::mouseClick(panel.openButton(1), Qt::LeftButton);
		const QPointer<QWidget> opened = windowEndingWith(QStringLiteral("layer-1"));
		QVERIFY2(!opened.isNull(), "Open showed no window for layer-1's instrument");
		QTest::mouseClick(panel.removeButton(1), Qt::LeftButton);
		QCOMPARE(trackOf(track)->layers().layerCount(), 0);
		QCOMPARE(panel.rowCount(), 1);
		QVERIFY2(opened.isNull(), "the layer's instrument window outlived the layer");
		REV_UNDO_OR_FAIL();
		QCOMPARE(trackOf(track)->layers().layerCount(), 1);
		QCOMPARE(trackOf(track)->layers().mix(1).level, 0.5f);  // the undo brings the mix back with the layer

		const ListSpec zones = CommandLists::layerZones(track);
		CommandListDialog dialog(zones);
		QCOMPARE(dialog.rowCount(), 1);  // the whole-keyboard zone the layer was given
		QVERIFY(dialog.runAction(0, actionIndex(zones, QStringLiteral("Remove"))).ok);
		QCOMPARE(trackOf(track)->layers().zones().zoneCount(), 0);
		for (QToolButton* button : panel.findChildren<QToolButton*>())
		{
			QVERIFY2(ControlRegistry::instance()->hasCommand(button->property("controlCommand").toString()),
				qPrintable(QStringLiteral("an undeclared button: ") + button->text()));
		}
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! A held live note follows the same layer when the undo of a layer removal inserts the removed
	//! predecessor back: the note-off must reach the layer that is sounding, not the inserted one.
	void liveNoteOffFollowsLayerAfterUndoInsert()
	{
		const QString target = revtest::addTrack();
		QVERIFY(!target.isEmpty());
		InstrumentTrack* parent = trackOf(target);
		QVERIFY(parent != nullptr);
		const QString layerTemplate = layerTemplateXml(parent);
		QCOMPARE(parent->layers().insertLayer(1, layerTemplate), 1);
		QCOMPARE(parent->layers().insertLayer(2, layerTemplate), 2);
		RackZone secondLayerZone;
		secondLayerZone.chain = 2;
		QCOMPARE(parent->layers().zones().addZone(secondLayerZone), 0);

		InstrumentTrack* soundingLayer = parent->layers().layer(2);
		QVERIFY(soundingLayer != nullptr);
		parent->processInEvent(MidiEvent(MidiNoteOn, 0, 60, 100, nullptr, MidiEvent::Source::Internal));
		NotePlayHandle* heldNote = soundingLayer->playingNote(60);
		QVERIFY(heldNote != nullptr);
		QVERIFY(!heldNote->isReleased());

		const QString removedLayer = parent->layers().layerXml(1);
		QVERIFY(parent->layers().removeLayer(1));
		QCOMPARE(parent->layers().layer(1), soundingLayer);
		QCOMPARE(parent->layers().insertLayer(1, removedLayer), 1);
		QCOMPARE(parent->layers().layer(2), soundingLayer);

		parent->processInEvent(MidiEvent(MidiNoteOff, 0, 60, 0, nullptr, MidiEvent::Source::Internal));
		QVERIFY2(heldNote->isReleased(), "the held note's release missed its original layer after undo");
	}

	//! MX-120: from the track's menu and its device card, a modulator made per-voice on the track's notes, bound to a
	//! parameter of the track's instrument through the list's form, its depth changed and the assignment removed in the
	//! assignments list (each undone), then made not per-voice again.
	void perVoiceModulationIsAssignedEditedAndRemoved()
	{
		const QString device = builtinDevice(QStringLiteral("tripleoscillator"));
		if (device.isEmpty()) { QSKIP("this build has no tripleoscillator module"); }
		const QString track = revtest::addTrack();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), device}}).ok);
		QMenu header;
		QVERIFY(trackMenuEntry(&header, track, setupPath(QStringLiteral("Per-Voice Modulation"), QStringLiteral("Modulators...")),
			QStringLiteral("modulator.get_state")) != nullptr);
		DevicePanel devices;
		QMenu* card = cardMenu(&devices, track);
		QVERIFY(card != nullptr && menuAction(card, setupPath(QStringLiteral("Per-Voice Modulation"),
			QStringLiteral("Modulation Assignments..."))) != nullptr);
		const QString modulator = run(QStringLiteral("modulator.create"), {{QStringLiteral("name"), QStringLiteral("per note")},
			{QStringLiteral("shape"), QStringLiteral("sine")}, {QStringLiteral("rate"), 2.0}}).result.value(QStringLiteral("modulator")).toString();
		QVERIFY(!modulator.isEmpty());
		const QString parameter = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), track},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("index"), 0}})
			.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("name")).toString();
		QVERIFY(!parameter.isEmpty());

		const ListSpec voices = CommandLists::voiceModulators(track);
		CommandListDialog modulators(voices);
		const int row = rowWhere(modulators, QStringLiteral("parent_id"), modulator);
		QVERIFY(row >= 0);
		QVERIFY(modulators.runAction(row, actionIndex(voices, QStringLiteral("Per-Voice on This Track"))).ok);
		QVERIFY(modulatorOf(modulator)->source.perVoice);
		QCOMPARE(control::trackId(modulatorOf(modulator)->source.triggerTrack), track);
		QVERIFY(modulators.row(row).value(QStringLiteral("per_voice")).toBool());  // the list was read again
		QCOMPARE(modulators.runAction(row, actionIndex(voices, QStringLiteral("Assign an Instrument Parameter..."))).errorKind,
			ControlErrorKind::Busy);
		const ControlResult bound = fillAndRun(&modulators, {{QStringLiteral("parameter"), parameter}, {QStringLiteral("depth"), 0.25}});
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		QCOMPARE(modulatorOf(modulator)->routeCount(), 1);

		const ListSpec spec = CommandLists::modulationAssignments(track);
		CommandListDialog assignments(spec);
		const int assigned = rowWhere(assignments, QStringLiteral("parent_id"), modulator);
		QVERIFY(assigned >= 0);
		QCOMPARE(assignments.row(assigned).value(QStringLiteral("parameter")).toString(), parameter);
		QVERIFY(!assignments.row(assigned).value(QStringLiteral("voice_mode")).toString().isEmpty());
		QCOMPARE(assignments.runAction(assigned, actionIndex(spec, QStringLiteral("Depth..."))).errorKind, ControlErrorKind::Busy);
		const ControlResult deeper = fillAndRun(&assignments, {{QStringLiteral("depth"), 0.5}});
		QVERIFY2(deeper.ok, qPrintable(deeper.errorMessage));
		QCOMPARE(modulatorOf(modulator)->routes.at(0).depth, 0.5f);
		REV_UNDO_OR_FAIL();
		QCOMPARE(modulatorOf(modulator)->routes.at(0).depth, 0.25f);
		QVERIFY(assignments.runAction(assigned, actionIndex(spec, QStringLiteral("Remove"))).ok);
		QCOMPARE(modulatorOf(modulator)->routeCount(), 0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(modulatorOf(modulator)->routeCount(), 1);

		modulators.refresh();
		QVERIFY(modulators.runAction(rowWhere(modulators, QStringLiteral("parent_id"), modulator),
			actionIndex(voices, QStringLiteral("Not Per-Voice"))).ok);
		QVERIFY(!modulatorOf(modulator)->source.perVoice);
		ShellSelection::instance()->setTrack(nullptr);
	}
};

QTEST_MAIN(InstrumentSetupUiTest)
#include "InstrumentSetupUiTest.moc"
