/*
 * DevicePanelTest.cpp - the shell selection, the Inspector and the Device panel over a real engine
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

/*! The 0.5.0 shell's selection and the two panels that follow it, over a real engine with the
 *  build's own plugin modules (LMMS_TEST_PLUGIN_DIR):
 *  - ShellSelection: a clip selects its track, a channel clears both, a deleted track reads as
 *    nothing selected;
 *  - the Inspector edits through the registered commands (track.rename, track.set_mute), so one
 *    control.undo takes an edit back;
 *  - the Device panel draws a track's instrument and devices and a channel's inserts with the
 *    modulator that drives one of them, its cards' header buttons dispatch plugin.bypass and
 *    plugin.unload, and a device removed from anywhere takes its card with it at once - before
 *    the device is deleted, so no control paints a deleted parameter.
 *  The device half is skipped when this host loads no effect module. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InspectorPanel.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Load the first effect this host can load onto @a target; its fx-<n>, or empty.
QString loadAnEffect(const QString& target)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
		{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
	{
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}});
		if (loaded.ok) { return loaded.result.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

QJsonArray cards(const DevicePanel& panel) { return panel.describe().value(QStringLiteral("cards")).toArray(); }

template <class T>
T* childNamed(QWidget* parent, const QString& accessibleName)
{
	for (T* child : parent->findChildren<T*>())
	{
		if (child->accessibleName() == accessibleName && child->isVisibleTo(parent)) { return child; }
	}
	return nullptr;
}

//! Every leaf entry of @a menu names a registered command (a renamed command fails here).
bool allDeclareRegisteredCommands(QMenu* menu, QString* stray)
{
	for (QAction* action : lmms::test::leafActions(menu))
	{
		const QString id = action->property("controlCommand").toString();
		if (!ControlRegistry::instance()->hasCommand(id)) { *stray = action->text() + QStringLiteral(" -> ") + id; return false; }
	}
	return true;
}

} // namespace

using lmms::test::menuAction;

class DevicePanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
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

	void theSelectionKeepsATrackAClipOrAChannel()
	{
		ShellSelection* selection = ShellSelection::instance();
		QSignalSpy changed(selection, &ShellSelection::changed);
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Clip* clip = track->createClip(TimePos(0));
		selection->setClip(clip);
		QCOMPARE(selection->track(), track);
		QCOMPARE(selection->clip(), clip);
		selection->setTrack(track);  // the clip is on it: it stays
		QCOMPARE(selection->clip(), clip);
		QCOMPARE(changed.count(), 1);
		selection->setChannel(QStringLiteral("ch-1"));
		QCOMPARE(selection->track(), static_cast<Track*>(nullptr));
		QCOMPARE(selection->clip(), static_cast<Clip*>(nullptr));
		QCOMPARE(selection->channel(), QStringLiteral("ch-1"));
		selection->setTrack(track);
		QVERIFY(selection->channel().isEmpty());
		delete track;
		QCOMPARE(selection->track(), static_cast<Track*>(nullptr));
	}

	void theInspectorEditsThroughTheCommands()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(QStringLiteral("Lead"));
		const QString id = control::trackIdOf(track);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setTrack(track);
		inspector.rebuild();
		QCoreApplication::processEvents();  // a layout shows a widget added to a shown parent on the next turn
		QCOMPARE(inspector.describe().value(QStringLiteral("track")).toString(), id);

		auto* name = childNamed<QLineEdit>(&inspector, QStringLiteral("Track name"));
		QVERIFY(name != nullptr);
		QCOMPARE(name->text(), QStringLiteral("Lead"));
		name->setText(QStringLiteral("Bass"));
		emit name->editingFinished();
		QCOMPARE(track->name(), QStringLiteral("Bass"));

		auto* mute = childNamed<QToolButton>(&inspector, QStringLiteral("Mute the track"));
		QVERIFY(mute != nullptr);
		mute->click();
		QVERIFY(track->isMuted());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!track->isMuted());

		// Solo is track.set_solo with its `solo` argument (the Inspector once sent `soloed`, which
		// the schema refuses: every click did nothing).
		auto* solo = childNamed<QToolButton>(&inspector, QStringLiteral("Solo the track"));
		QVERIFY(solo != nullptr);
		solo->click();
		QVERIFY(track->isSolo());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!track->isSolo());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->name(), QStringLiteral("Lead"));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	/*! 0.5.0 W1: the track, clip, channel and project menus (the Inspector's More buttons, the
	 *  track header's and the mixer strip's More submenus) offer the socket-only capabilities
	 *  where they belong, every entry a registered command. */
	void theSectionMenusOfferTheW1Commands()
	{
		auto* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* sample = Track::create(Track::Type::Sample, Engine::getSong());
		Clip* midiClip = instrument->createClip(TimePos(0));
		Clip* sampleClip = sample->createClip(TimePos(0));
		const QString channel = run(QStringLiteral("mixer.add_channel")).result.value(QStringLiteral("channel")).toString();
		QString stray;

		QMenu track;
		ShellCommandMenus::populateTrack(&track, instrument);
		QVERIFY2(allDeclareRegisteredCommands(&track, &stray), qPrintable(stray));
		QVERIFY(menuAction(&track, {QStringLiteral("Record"), QStringLiteral("Arm")}) != nullptr);
		QVERIFY(menuAction(&track, {QStringLiteral("Retrospective Capture"), QStringLiteral("Last MIDI to a New Clip")}) != nullptr);
		QVERIFY(menuAction(&track, {QStringLiteral("Render"), QStringLiteral("Freeze")}) != nullptr);
		QVERIFY2(menuAction(&track, {QStringLiteral("Insert Test Signal...")}) == nullptr, "an instrument track offered a test signal clip");
		QMenu sampleTrack;
		ShellCommandMenus::populateTrack(&sampleTrack, sample);
		QVERIFY(menuAction(&sampleTrack, {QStringLiteral("Insert Test Signal...")}) != nullptr);

		QMenu midi;
		ShellCommandMenus::populateClip(&midi, midiClip);
		QVERIFY2(allDeclareRegisteredCommands(&midi, &stray), qPrintable(stray));
		QVERIFY(menuAction(&midi, {QStringLiteral("Scale and Chords"), QStringLiteral("Detect Chords")}) != nullptr);
		QVERIFY(menuAction(&midi, {QStringLiteral("Groove"), QStringLiteral("Random Seed...")}) != nullptr);
		QMenu audio;
		ShellCommandMenus::populateClip(&audio, sampleClip);
		QVERIFY2(allDeclareRegisteredCommands(&audio, &stray), qPrintable(stray));
		QVERIFY(menuAction(&audio, {QStringLiteral("Process"), QStringLiteral("Reverse")}) != nullptr);
		QVERIFY(menuAction(&audio, {QStringLiteral("Tempo and Warp"), QStringLiteral("Stretch Algorithm"), QStringLiteral("preserve_pitch")}) != nullptr);
		QVERIFY2(!menuAction(&audio, {QStringLiteral("Crossfade Into Next Clip...")})->isEnabled(), "the last clip offered a crossfade");

		QMenu strip;
		ShellCommandMenus::populateChannel(&strip, channel);
		QVERIFY2(allDeclareRegisteredCommands(&strip, &stray), qPrintable(stray));
		menuAction(&strip, {QStringLiteral("Loudness Meter"), QStringLiteral("On")})->trigger();
		QVERIFY(run(QStringLiteral("meter.get_state")).result.value(QStringLiteral("armed")).toBool());
		menuAction(&strip, {QStringLiteral("Loudness Meter"), QStringLiteral("Off")})->trigger();
		QVERIFY(!run(QStringLiteral("meter.get_state")).result.value(QStringLiteral("armed")).toBool());

		QMenu project;
		ShellCommandMenus::populateProject(&project);
		QVERIFY2(allDeclareRegisteredCommands(&project, &stray), qPrintable(stray));

		// The Inspector's clip section carries the same menu behind its More button.
		InspectorPanel inspector;
		ShellSelection::instance()->setClip(midiClip);
		inspector.rebuild();
		auto* more = childNamed<QToolButton>(&inspector, QStringLiteral("More clip actions"));
		QVERIFY(more != nullptr && more->menu() != nullptr);
		emit more->menu()->aboutToShow();
		QVERIFY(menuAction(more->menu(), {QStringLiteral("Scale and Chords"), QStringLiteral("Detect Chords")}) != nullptr);
		ShellSelection::instance()->setTrack(nullptr);
		delete instrument;
		delete sample;
	}

	void theDevicePanelDrawsTheChainAndTheModulatorBesideIt()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		const QString first = loadAnEffect(channel);
		if (first.isEmpty()) { QSKIP("this host loads no effect module"); }
		const QString second = loadAnEffect(channel);
		// The parameter list an agent reads for the device (dsp.get_state), which the card draws from.
		const QJsonArray chains = run(QStringLiteral("dsp.get_state"), {{QStringLiteral("target"), channel}}).result
			.value(QStringLiteral("chains")).toArray();
		QVERIFY(!chains.isEmpty());
		const QJsonArray parameters = chains.first().toObject().value(QStringLiteral("devices")).toArray()
			.first().toObject().value(QStringLiteral("parameters")).toArray();
		QVERIFY(!parameters.isEmpty());
		const QString modulator = run(QStringLiteral("modulator.create"), {{QStringLiteral("name"), QStringLiteral("Wobble")},
			{QStringLiteral("rate"), 2.0}}).result.value(QStringLiteral("modulator")).toString();
		QVERIFY(run(QStringLiteral("modulator.target_set"), {{QStringLiteral("modulator"), modulator},
			{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 0}, {QStringLiteral("effect"), 0},
			{QStringLiteral("parameter"), parameters.first().toObject().value(QStringLiteral("name")).toString()},
			{QStringLiteral("depth"), 0.25}}).ok);

		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(panel.describe().value(QStringLiteral("channel")).toString(), channel);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 1 : 2);
		const QJsonObject card = cards(panel).first().toObject();
		QCOMPARE(card.value(QStringLiteral("plugin")).toString(), first);
		QVERIFY(card.value(QStringLiteral("parameters")).toInt() > 0);
		QVERIFY(card.value(QStringLiteral("parameters")).toInt() <= parameters.size());
		QVERIFY(card.value(QStringLiteral("shown")).toInt() <= DeviceCard::MaxParameters);
		QCOMPARE(panel.describe().value(QStringLiteral("modulators")).toArray(), QJsonArray{modulator});

		// The power button is plugin.bypass.
		auto* deviceCard = panel.findChildren<DeviceCard*>().first();
		Effect* effect = deviceCard->spec().effect;
		auto* power = childNamed<QToolButton>(deviceCard, QStringLiteral("Switch %1 on or off").arg(deviceCard->spec().title));
		QVERIFY(power != nullptr);
		power->click();
		QVERIFY(!effect->isEnabled());

		// Removed over the socket: the card goes with it, and the panel redraws what is left.
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first}}).ok);
		QCOMPARE(cards(panel).size(), 0);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 0 : 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 1 : 2);
		ShellSelection::instance()->setTrack(nullptr);
	}

	void aTrackShowsItsInstrumentAndItsDevices()
	{
		const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		const QString effect = loadAnEffect(track);
		if (effect.isEmpty()) { QSKIP("this host loads no effect module"); }
		Track* resolved = nullptr;
		for (Track* t : Engine::getSong()->tracks()) { if (control::trackIdOf(t) == track) { resolved = t; } }
		QVERIFY(resolved != nullptr);
		DevicePanel panel;
		panel.show();
		ShellSelection::instance()->setTrack(resolved);
		panel.rebuild();
		const QJsonArray drawn = cards(panel);
		QVERIFY(!drawn.isEmpty());
		QCOMPARE(drawn.last().toObject().value(QStringLiteral("plugin")).toString(), effect);
		const bool hasInstrument = dynamic_cast<InstrumentTrack*>(resolved)->instrument() != nullptr;
		QCOMPARE(drawn.size(), hasInstrument ? 2 : 1);
		ShellSelection::instance()->setTrack(nullptr);
	}
};

QTEST_MAIN(DevicePanelTest)
#include "DevicePanelTest.moc"
