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
 *  - 0.5.0 L10 (W3): the channel's level beside its heading, a parameter's alias on its card, the
 *    card's delta and fold buttons.
 *  - 0.5.0 theme 12: cards update in place (a device added keeps the other cards), each card says its state in
 *    words (bypassed, A/B) and has one action menu of registered commands; the device picker searches and words
 *    each row's state.
 *  - 0.5.0 theme 13: the Inspector keeps an unchanged section (and its focus) when the selection moves, says
 *    "Multiple values" for notes that differ and gives a typed value to all of them, reads velocities in %, and
 *    edits a channel's name in place (mixer.rename_channel).
 *  The device half is skipped when this host loads no effect module. */

#include <QtTest>

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QMenu>
#include <QToolButton>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InspectorPanel.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "ChannelLevelMeter.h"
#include "CommandLists.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"
#include "TransportReadouts.h"
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

//! The @a T children that show: a rebuild hides the widgets it drops at once and deletes them later.
template <class T>
QList<T*> shownChildren(QWidget* parent)
{
	QList<T*> shown = parent->findChildren<T*>();
	shown.removeIf([parent](const T* child) { return !child->isVisibleTo(parent); });
	return shown;
}

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
		dynamic_cast<InstrumentTrack*>(instrument)->loadInstrument(QStringLiteral("tripleoscillator"));
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
		// L08: the mixer view's options and the solo mode are a click away on every channel.
		menuAction(&strip, {QStringLiteral("Mixer View"), QStringLiteral("Strip Width"), QStringLiteral("wide")})->trigger();
		QCOMPARE(run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("strip_width")).toString(), QStringLiteral("wide"));
		menuAction(&strip, {QStringLiteral("Mixer View"), QStringLiteral("Strip Width"), QStringLiteral("normal")})->trigger();
		QVERIFY(menuAction(&strip, {QStringLiteral("Solo Mode"), QStringLiteral("additive")}) != nullptr);
		QVERIFY(menuAction(&strip, {QStringLiteral("Strip"), QStringLiteral("Pan Mode"), QStringLiteral("dual")}) != nullptr);

		// Review W1-1 #1/#2: Take Lanes maps the rows' `lane`, and Plug-ins in Project lists an
		// instrument (a chain's `instrument`) as well as its effects.
		const QString trackId = control::trackIdOf(instrument);
		QVERIFY(run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), trackId}}).ok);
		CommandListDialog lanes(CommandLists::takeLanes(trackId));
		QVERIFY(lanes.rowCount() >= 1);
		QVERIFY2(lanes.runAction(lanes.rowCount() - 1, 0).ok, qPrintable(lanes.statusText()));
		CommandListDialog plugins(CommandLists::pluginsInProject());
		bool listsInstrument = false;
		for (int row = 0; row < plugins.rowCount(); ++row)
		{
			listsInstrument = listsInstrument || plugins.row(row).value(QStringLiteral("kind")).toString() == QStringLiteral("instrument");
		}
		QVERIFY2(listsInstrument, "Plug-ins in Project left the instrument out");

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

	/*! ED-015/016 (0.5.0 L04): the Inspector's clip and note fields edit through the registry -
	 *  typed the way a person types into them (double-click, text, Enter). Theme 7: they read in bars and
	 *  beats by default, in ticks once transport.time_format says so, and take either when typed. */
	void theInspectorsNumericFieldsEditTheClipAndItsNotes()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		dynamic_cast<InstrumentTrack*>(track)->loadInstrument(QStringLiteral("tripleoscillator"));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		Note* note = clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		note->setSelected(true);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		const auto type = [&inspector](const QString& caption, const QString& text, int nth = 0) {
			QList<ReadoutField*> matches;
			for (ReadoutField* field : inspector.findChildren<ReadoutField*>())
			{
				if (field->accessibleName().contains(caption) && field->isVisibleTo(&inspector)) { matches.append(field); }
			}
			if (nth >= matches.size()) { return false; }
			QTest::mouseDClick(matches.at(nth), Qt::LeftButton);
			auto* editor = matches.at(nth)->findChild<QLineEdit*>();
			if (editor == nullptr) { return false; }
			editor->setText(text);
			emit editor->editingFinished();
			return true;
		};
		QVERIFY(type(QStringLiteral("Start"), QStringLiteral("2.1")));
		QCOMPARE(clip->startPosition().getTicks(), Engine::getSong()->ticksPerBar());
		QVERIFY(type(QStringLiteral("Length"), QStringLiteral("2")));
		QCOMPARE(clip->length().getTicks(), 2 * Engine::getSong()->ticksPerBar());
		QVERIFY(type(QStringLiteral("Offset"), QStringLiteral("0.1.0")));  // one beat (bars|beats shown: a plain "48" is bars)
		QCOMPARE(clip->startTimeOffset().getTicks(), 48);
		const auto shown = [&inspector](const QString& caption) {
			for (ReadoutField* field : inspector.findChildren<ReadoutField*>())
			{
				if (field->caption() == caption && field->isVisibleTo(&inspector)) { return field->valueText(); }
			}
			return QString();
		};
		inspector.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(shown(QStringLiteral("Start")), QStringLiteral("2.1.1"));  // the clip's, the first of the two Starts
		QCOMPARE(shown(QStringLiteral("Length")), QStringLiteral("2.0.0"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.time_format"),
			{{QStringLiteral("format"), QStringLiteral("ticks")}}).ok);
		QCOMPARE(shown(QStringLiteral("Length")), QStringLiteral("384 ticks"));  // shown at once, in the new format
		QVERIFY(type(QStringLiteral("Offset"), QStringLiteral("24")));  // ticks shown: a plain number is ticks
		QCOMPARE(clip->startTimeOffset().getTicks(), 24);
		ControlRegistry::instance()->invoke(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("bars_beats")}});
		QVERIFY(type(QStringLiteral("Key"), QStringLiteral("62")));
		QCOMPARE(clip->notes().front()->key(), 62);
		QVERIFY(type(QStringLiteral("Velocity"), QStringLiteral("64")));
		QCOMPARE(static_cast<int>(clip->notes().front()->getVolume()), 64);  // the 0..200 note volume scale
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	void theInspectorShowsAChannelsSendsAndStrip()
	{
		// MX-012 (0.5.0 L08): the channel's sends, with their removal, and its strip settings.
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 3) { mixer->createChannel(); }
		const QString from = control::channelIdOf(mixer->mixerChannel(2));
		const QString to = control::channelIdOf(mixer->mixerChannel(3));
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), from}, {QStringLiteral("to"), to}}).ok);
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setChannel(from);
		inspector.rebuild();
		QCoreApplication::processEvents();
		QVERIFY(childNamed<QWidget>(&inspector, QStringLiteral("Trim (dB)")) != nullptr);
		QToolButton* remove = nullptr;
		for (auto* button : inspector.findChildren<QToolButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("mixer.route_remove")
				&& button->toolTip().contains(mixer->mixerChannel(3)->m_name)) { remove = button; }
		}
		QVERIFY2(remove != nullptr, "no removal for the send");
		remove->click();
		QVERIFY(run(QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), from}, {QStringLiteral("to"), to},
			{QStringLiteral("pan"), 0.0}}).errorKind == ControlErrorKind::NotFound);  // the send is gone
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // and back, with the section rebuilt around it
		QCoreApplication::processEvents();
		ShellSelection::instance()->setTrack(nullptr);
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
		auto* deviceCard = shownChildren<DeviceCard>(&panel).first();
		Effect* effect = deviceCard->spec().effect;
		auto* power = childNamed<QToolButton>(deviceCard, QStringLiteral("Switch %1 on or off").arg(deviceCard->spec().title));
		QVERIFY(power != nullptr);
		power->click();
		QVERIFY(!effect->isEnabled());

		// Removed over the socket: its card goes with it at once - before the device is deleted - and (theme 12) only
		// its card: the other device's card stays, the same widget, through the redraw.
		const QPointer<DeviceCard> other = second.isEmpty() ? nullptr : shownChildren<DeviceCard>(&panel).last();
		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first}}).ok);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 0 : 1);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 0 : 1);
		if (!second.isEmpty()) { QVERIFY(!other.isNull() && other->isVisibleTo(&panel)); }
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(cards(panel).size(), second.isEmpty() ? 1 : 2);
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! 0.5.0 L10 (W3): a channel's level beside its heading (MX-101); a parameter's alias captions its
	//! control (MX-097); the delta button is plugin.delta (MX-080); folding hides the parameters (MX-113).
	void theLevelAliasDeltaAndFoldShowOnTheCard()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString effect = loadAnEffect(channel);
		if (effect.isEmpty()) { QSKIP("this host loads no effect module"); }
		QVERIFY(run(QStringLiteral("plugin.param_rename"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), effect},
			{QStringLiteral("name"), QStringLiteral("Wet/Dry mix")}, {QStringLiteral("alias"), QStringLiteral("Blend")}}).ok);
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		const QList<ChannelLevelMeter*> meters = shownChildren<ChannelLevelMeter>(&panel);
		QCOMPARE(meters.size(), 1);
		QCOMPARE(meters.first()->channelId(), channel);
		mixerChannel->m_rmsLeft.set(0.5f);
		meters.first()->refresh();
		QVERIFY(std::abs(meters.first()->levels().first + 6.0206f) < 0.05f);
		DeviceCard* card = shownChildren<DeviceCard>(&panel).first();
		Effect* device = card->spec().effect;
		QVERIFY(childNamed<QWidget>(card, QStringLiteral("Blend")) != nullptr);
		auto* delta = childNamed<QToolButton>(card, QStringLiteral("Hear only what %1 changes").arg(card->spec().title));
		QVERIFY(delta != nullptr);
		delta->click();
		QVERIFY(device->deltaTap().enabled());
		delta->click();
		QVERIFY(!device->deltaTap().enabled());
		auto* fold = childNamed<QToolButton>(card, QStringLiteral("Fold or open %1").arg(card->spec().title));
		QVERIFY(fold != nullptr);
		fold->click();
		QVERIFY(device->deviceOptions().folded);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		DeviceCard* redrawn = shownChildren<DeviceCard>(&panel).first();
		QVERIFY(childNamed<QWidget>(redrawn, QStringLiteral("Blend")) == nullptr);  // folded: the header only
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!device->deviceOptions().folded);
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

	//! Theme 12: a device added redraws the panel in place - the cards already there are the same widgets - and a card
	//! says its state in words: "Bypassed" once bypassed, the A/B slot heard once one is stored.
	void theCardsStayInPlaceAndSayTheirState()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		const QString first = loadAnEffect(channel);
		if (first.isEmpty()) { QSKIP("this host loads no effect module"); }
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		const QPointer<DeviceCard> card = shownChildren<DeviceCard>(&panel).first();
		QVERIFY(!loadAnEffect(channel).isEmpty());
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCOMPARE(cards(panel).size(), 2);
		QVERIFY2(!card.isNull() && card->isVisibleTo(&panel), "the first card was made again for a device added after it");
		QVERIFY(run(QStringLiteral("plugin.bypass"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first},
			{QStringLiteral("bypass"), true}}).ok);
		card->refreshActivity();
		auto* state = card->findChild<QLabel*>(QStringLiteral("deviceState"));
		QVERIFY(state != nullptr && state->text().contains(QStringLiteral("Bypassed")));
		QVERIFY(state->accessibleName().contains(QStringLiteral("Bypassed")));  // a screen reader hears it too
		QVERIFY(cards(panel).first().toObject().value(QStringLiteral("state")).toObject().value(QStringLiteral("bypassed")).toBool());
		auto* ab = card->findChild<QToolButton*>(QStringLiteral("deviceAb"));
		QVERIFY(ab != nullptr && !ab->isVisibleTo(card));  // no slot stored yet: A/B means nothing
		QVERIFY(run(QStringLiteral("plugin.ab"), {{QStringLiteral("target"), channel}, {QStringLiteral("plugin"), first},
			{QStringLiteral("action"), QStringLiteral("store")}, {QStringLiteral("slot"), QStringLiteral("b")}}).ok);
		card->refreshActivity();
		QVERIFY(ab->isVisibleTo(card));
		QCOMPARE(ab->text(), QStringLiteral("B"));  // the letter, not a colour
		QVERIFY(state->text().contains(QStringLiteral("Hearing B")));
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! Theme 12: the card's Actions button and its right-click offer one menu; its first part - bypass, editor,
	//! presets, A/B, replace, move, remove - names registered commands, and Move Right is plugin.move, one undo step.
	void theCardsActionMenuOffersEachAction()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString first = loadAnEffect(channel);
		if (first.isEmpty()) { QSKIP("this host loads no effect module"); }
		const QString second = loadAnEffect(channel);
		DevicePanel panel;
		panel.resize(1200, 300);
		panel.show();
		ShellSelection::instance()->setChannel(channel);
		panel.rebuild();
		QCoreApplication::processEvents();
		DeviceCard* card = shownChildren<DeviceCard>(&panel).first();
		auto* actions = card->findChild<QToolButton*>(QStringLiteral("deviceActions"));
		QVERIFY(actions != nullptr && actions->menu() != nullptr);
		QVERIFY(!actions->accessibleName().isEmpty());
		emit actions->menu()->aboutToShow();
		QMenu* menu = actions->menu();
		for (const QString& text : {QStringLiteral("Bypass"), QStringLiteral("Open Editor"), QStringLiteral("Replace With..."),
				QStringLiteral("Move Left"), QStringLiteral("Move Right"), QStringLiteral("Remove")})
		{
			QAction* action = menuAction(menu, {text});
			QVERIFY2(action != nullptr, qPrintable(text));
			QVERIFY2(ControlRegistry::instance()->hasCommand(action->property("controlCommand").toString()), qPrintable(text));
		}
		QVERIFY(menuAction(menu, {QStringLiteral("Presets"), QStringLiteral("Load Preset...")}) != nullptr);
		QVERIFY(menuAction(menu, {QStringLiteral("A/B"), QStringLiteral("Toggle A/B")}) != nullptr);
		QVERIFY2(!menuAction(menu, {QStringLiteral("Move Left")})->isEnabled(), "the first device offered to move left");
		if (second.isEmpty()) { ShellSelection::instance()->setTrack(nullptr); return; }
		menuAction(menu, {QStringLiteral("Move Right")})->trigger();
		QCOMPARE(control::effectIdOf(mixerChannel->m_fxChain.effects().back()), first);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(control::effectIdOf(mixerChannel->m_fxChain.effects().front()), first);
		ShellSelection::instance()->setTrack(nullptr);
	}

	//! Theme 12: the device picker lists the catalogue's effects with their type and format in words, and its search
	//! filters them live; a search that matches nothing shows nothing.
	void theDevicePickerSearchesAndWordsEachRow()
	{
		DevicePicker picker(DevicePicker::Kind::Effect, QStringLiteral("Add"));
		if (picker.shownRows() == 0) { QSKIP("this host catalogues no effect"); }
		const QString row = picker.shownRowText(0);
		QVERIFY2(row.contains(QStringLiteral("Effect")), qPrintable(row));
		const QString name = row.section(QStringLiteral(" | "), 0, 0);
		picker.setQuery(name);
		QVERIFY(picker.shownRows() >= 1);
		QVERIFY(picker.shownRowText(0).contains(name));
		picker.setQuery(QStringLiteral("zz-no-device-is-called-this"));
		QCOMPARE(picker.shownRows(), 0);
		QVERIFY(picker.selectedDevice().isEmpty());
		picker.setQuery(QString());
		QVERIFY(picker.selectedDevice().startsWith(QStringLiteral("dev-")) || picker.shownRows() > 0);
	}

	//! Theme 13: selecting another clip of the same track keeps the track section - the same widget, with the focus
	//! where it was; the notes' fields say "Multiple values" when the notes differ, and a key typed there goes to all.
	void theInspectorKeepsItsContextAndShowsMixedValues()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		dynamic_cast<InstrumentTrack*>(track)->loadInstrument(QStringLiteral("tripleoscillator"));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		auto* other = dynamic_cast<MidiClip*>(track->createClip(TimePos(4 * 192)));
		Note* low = clip->addNote(Note(TimePos(0), TimePos(0), 60, 100), false);
		Note* high = clip->addNote(Note(TimePos(48), TimePos(0), 64, 80), false);
		low->setSelected(true);
		high->setSelected(true);
		InspectorPanel inspector;
		inspector.show();
		inspector.activateWindow();
		ShellSelection::instance()->setClip(clip);
		inspector.rebuild();
		QCoreApplication::processEvents();
		auto* name = childNamed<QLineEdit>(&inspector, QStringLiteral("Track name"));
		QVERIFY(name != nullptr);
		name->setFocus();
		const auto field = [&inspector](const QString& caption) -> ReadoutField* {
			for (ReadoutField* f : inspector.findChildren<ReadoutField*>())
			{
				if (f->caption() == caption && f->isVisibleTo(&inspector)) { return f; }
			}
			return nullptr;
		};
		QVERIFY(field(QStringLiteral("Key")) != nullptr);
		QVERIFY2(field(QStringLiteral("Key"))->valueText().contains(QStringLiteral("Multiple values")), qPrintable(field(QStringLiteral("Key"))->valueText()));
		QVERIFY(field(QStringLiteral("Velocity"))->valueText().contains(QStringLiteral("Multiple values")));
		QVERIFY(field(QStringLiteral("Selected notes"))->valueText().contains(QStringLiteral("2")));
		QTest::mouseDClick(field(QStringLiteral("Key")), Qt::LeftButton);
		auto* editor = field(QStringLiteral("Key"))->findChild<QLineEdit*>();
		QVERIFY(editor != nullptr);
		editor->setText(QStringLiteral("62"));
		emit editor->editingFinished();
		QCOMPARE(low->key(), 62);  // given to every selected note, not transposed by the first one's distance
		QCOMPARE(high->key(), 62);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// The undo restored the clip's checkpoint, which makes its notes anew: the second note (velocity 80) is
		// found again.
		const auto restored = std::find_if(clip->notes().begin(), clip->notes().end(),
			[](const Note* note) { return static_cast<int>(note->getVolume()) == 80; });
		QVERIFY(restored != clip->notes().end());
		QCOMPARE((*restored)->key(), 64);
		ShellSelection::instance()->setClip(other);
		inspector.rebuild();
		QCoreApplication::processEvents();
		QCOMPARE(childNamed<QLineEdit>(&inspector, QStringLiteral("Track name")), name);  // the track section stayed
		QVERIFY(inspector.describe().value(QStringLiteral("sections")).toArray().contains(QStringLiteral("clip")));
		ShellSelection::instance()->setTrack(nullptr);
		delete track;
	}

	//! Theme 13: a channel's name is a field in the Inspector, and Enter renames it through mixer.rename_channel.
	void theChannelsNameIsEditedInPlace()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* mixerChannel = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(mixerChannel);
		const QString before = mixerChannel->m_name;
		InspectorPanel inspector;
		inspector.show();
		ShellSelection::instance()->setChannel(channel);
		inspector.rebuild();
		QCoreApplication::processEvents();
		auto* name = childNamed<QLineEdit>(&inspector, QStringLiteral("Channel name"));
		QVERIFY(name != nullptr);
		QCOMPARE(name->text(), before);
		name->setText(QStringLiteral("Vocals"));
		emit name->editingFinished();
		QCOMPARE(mixerChannel->m_name, QStringLiteral("Vocals"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(mixerChannel->m_name, before);
		ShellSelection::instance()->setTrack(nullptr);
	}
};

QTEST_MAIN(DevicePanelTest)
#include "DevicePanelTest.moc"
