/*
 * ExternalDeviceCardTest.cpp - Track > MIDI Out > External Device... runs the device.external_* commands (Q050-252.1)
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

/*! Q050-252.1: the card's controls (port, channel, bank MSB / LSB, program, preset name with Save / Recall / Delete)
 *  are driven the way a person drives them - a combo choice, a spin box value, a button click - and leave the device
 *  the commands leave; the bank and program reach the output; each edit is one control.undo, after which the card
 *  shows the previous settings. The menu entry opens the card and declares device.external_set. */

#include <QtTest>

#include <QAction>
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ExternalDeviceCard.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiPort.h"
#include "MenuTestSupport.h"
#include "ShellCommandMenus.h"
#include "ShellMenuParts.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

class RecordingMidiClient : public MidiClient
{
public:
	QStringList writable{QStringLiteral("Synth:out"), QStringLiteral("Other:out")};
	QStringList sent;

	void processOutEvent(const MidiEvent& e, const TimePos&, const MidiPort*) override
	{
		sent.append(e.type() == MidiControlChange ? QStringLiteral("cc%1=%2").arg(e.controllerNumber()).arg(e.controllerValue())
				: QStringLiteral("pc%1").arg(e.program() + 1));
	}
	QStringList writablePorts() const override { return writable; }
};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

template<class W> W* part(const ExternalDeviceCard& card, const char* name)
{
	auto* widget = card.findChild<W*>(QString::fromLatin1(name));
	return widget;
}

} // namespace

class ExternalDeviceCardTest : public QObject
{
	Q_OBJECT

private:
	RecordingMidiClient m_client;

	InstrumentTrack* newTrack()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		track->midiPort()->rebindClientForTest(&m_client);
		m_client.sent.clear();
		return track;
	}

	QString sentSince()
	{
		const QString out = m_client.sent.join(QLatin1Char(' '));
		m_client.sent.clear();
		return out;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_client.writable = QStringList{QStringLiteral("Synth:out"), QStringLiteral("Other:out")};
	}

	//! Driving the controls leaves the device the commands leave, and the patch reaches the output.
	void theControlsConfigureTheDevice()
	{
		InstrumentTrack* track = newTrack();
		InstrumentTrack* viaCommand = newTrack();
		ExternalDeviceCard card(control::trackIdOf(track));
		auto* port = part<QComboBox>(card, "externalDevicePort");
		auto* channel = part<QSpinBox>(card, "externalDeviceChannel");
		auto* msb = part<QSpinBox>(card, "externalDeviceBankMsb");
		auto* lsb = part<QSpinBox>(card, "externalDeviceBankLsb");
		auto* program = part<QSpinBox>(card, "externalDeviceProgram");
		QVERIFY(port && channel && msb && lsb && program);
		QCOMPARE(port->count(), 3);  // (output off), Synth:out, Other:out
		QCOMPARE(port->currentIndex(), 0);

		port->setCurrentIndex(port->findData(QStringLiteral("Synth:out")));  // the choice of a person
		QVERIFY2(card.message().isEmpty(), qPrintable(card.message()));
		QVERIFY(track->midiPort()->isWritable());
		channel->setValue(5);
		msb->setValue(2);
		QCOMPARE(sentSince(), QStringLiteral("cc0=2 pc1"));  // the bank on its own goes with the program it takes effect on
		lsb->setValue(3);
		program->setValue(17);
		QCOMPARE(sentSince(), QStringLiteral("cc0=2 cc32=3 pc1 cc0=2 cc32=3 pc17"));

		QVERIFY(run(QStringLiteral("device.external_set"), {{QStringLiteral("track"), control::trackIdOf(viaCommand)},
			{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("channel"), 5}, {QStringLiteral("bank_msb"), 2},
			{QStringLiteral("bank_lsb"), 3}, {QStringLiteral("program"), 17}}).ok);
		auto stateOf = [](InstrumentTrack* t) { return run(QStringLiteral("device.external_get_state"), {{QStringLiteral("track"), control::trackIdOf(t)}}).result; };
		QJsonObject a = stateOf(track);
		QJsonObject b = stateOf(viaCommand);
		a.remove(QStringLiteral("track"));
		b.remove(QStringLiteral("track"));
		QCOMPARE(a, b);
		QCOMPARE(card.state().value(QStringLiteral("program")).toInt(), 17);

		// the card follows a change it did not make (an agent's, or an undo)
		QVERIFY(run(QStringLiteral("device.external_set"), {{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("program"), 80}}).ok);
		card.refresh();
		QCOMPARE(program->value(), 80);
		// bank off sends none
		sentSince();
		msb->setValue(-1);
		lsb->setValue(-1);
		QCOMPARE(sentSince(), QStringLiteral("cc32=3 pc80 pc80"));
		// the output off
		port->setCurrentIndex(0);
		QVERIFY(!track->midiPort()->isWritable());
		QCOMPARE(port->currentIndex(), 0);
	}

	//! One control.undo puts back what one control change did, and the card shows it.
	void eachEditIsOneUndoStepAndTheCardFollowsIt()
	{
		InstrumentTrack* track = newTrack();
		ExternalDeviceCard card(control::trackIdOf(track));
		auto* program = part<QSpinBox>(card, "externalDeviceProgram");
		auto* channel = part<QSpinBox>(card, "externalDeviceChannel");
		auto* port = part<QComboBox>(card, "externalDevicePort");
		port->setCurrentIndex(port->findData(QStringLiteral("Synth:out")));
		program->setValue(30);
		channel->setValue(7);
		QCOMPARE(track->midiPort()->outputChannel(), 7);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		card.refresh();
		QCOMPARE(channel->value(), 1);
		QCOMPARE(program->value(), 30);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		card.refresh();
		QCOMPARE(program->value(), 1);
		QCOMPARE(track->midiPort()->outputProgram(), 1);
	}

	//! Save, change, Recall: the buttons run the preset commands; the combo lists the presets.
	void thePresetButtonsSaveRecallAndDelete()
	{
		InstrumentTrack* track = newTrack();
		ExternalDeviceCard card(control::trackIdOf(track));
		auto* port = part<QComboBox>(card, "externalDevicePort");
		auto* msb = part<QSpinBox>(card, "externalDeviceBankMsb");
		auto* program = part<QSpinBox>(card, "externalDeviceProgram");
		auto* name = part<QComboBox>(card, "externalDevicePresetName");
		auto* save = part<QPushButton>(card, "externalDevicePresetSave");
		auto* recall = part<QPushButton>(card, "externalDevicePresetRecall");
		auto* remove = part<QPushButton>(card, "externalDevicePresetRemove");
		QVERIFY(name && save && recall && remove);
		port->setCurrentIndex(port->findData(QStringLiteral("Synth:out")));
		msb->setValue(4);
		program->setValue(21);
		name->setEditText(QStringLiteral("Pad"));
		save->click();
		QVERIFY2(card.message().isEmpty(), qPrintable(card.message()));
		QCOMPARE(name->count(), 1);
		QCOMPARE(track->midiPort()->devicePresets().size(), 1);
		QCOMPARE(name->currentText(), QStringLiteral("Pad"));

		program->setValue(60);
		msb->setValue(-1);
		sentSince();
		recall->click();
		QCOMPARE(program->value(), 21);
		QCOMPARE(msb->value(), 4);
		QCOMPARE(sentSince(), QStringLiteral("cc0=4 pc21"));

		// The preset's port can disappear while another output is selected; tell the musician what was not applied.
		port->setCurrentIndex(port->findData(QStringLiteral("Other:out")));
		m_client.writable = QStringList{QStringLiteral("Other:out")};
		name->setEditText(QStringLiteral("Pad"));
		recall->click();
		QVERIFY(card.message().contains(QStringLiteral("Synth:out")));
		QCOMPARE(track->midiPort()->outputProgram(), 21);

		name->setEditText(QStringLiteral("Nothing"));
		recall->click();
		QVERIFY(!card.message().isEmpty());  // refused, and it says why
		QCOMPARE(program->value(), 21);
		name->setEditText(QStringLiteral("Pad"));
		remove->click();
		QCOMPARE(track->midiPort()->devicePresets().size(), 0);
		QCOMPARE(name->count(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		card.refresh();
		QCOMPARE(name->count(), 1);
	}

	//! The menu entry exists, declares the command it runs, and opens the card for that track.
	void theTrackMenuOpensTheCard()
	{
		InstrumentTrack* track = newTrack();
		QMenu menu;
		ShellCommandMenus::populateTrack(&menu, track);
		QAction* entry = test::menuAction(&menu, {QStringLiteral("MIDI Out"), QStringLiteral("External Device...")});
		QVERIFY2(entry != nullptr, "Track > MIDI Out has no External Device entry");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("device.external_set"));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("device.external_set")));
		entry->trigger();
		ExternalDeviceCard* opened = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (widget->objectName() == QLatin1String("externalDeviceCard-") + control::trackIdOf(track)) { opened = qobject_cast<ExternalDeviceCard*>(widget); }
		}
		QVERIFY2(opened != nullptr, "the entry did not open the card");
		QVERIFY(opened->isVisible());
		QCOMPARE(opened->state().value(QStringLiteral("track")).toString(), control::trackIdOf(track));
		opened->close();
	}

	//! Track > Versions > Use as Comp Lane > A: one entry adds a take lane named A holding A's clip copy (Q050-275).
	void versionsMenuUsesAVersionAsACompLane()
	{
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		const QString id = control::trackIdOf(track);
		const QJsonObject self{{QStringLiteral("track"), id}};
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), id}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 96}}).ok);
		QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), id}, {QStringLiteral("position"), 192}, {QStringLiteral("length"), 192}}).ok);
		QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);

		QMenu menu;
		menuparts::trackVersionEntries(&menu, track, self);
		QAction* entry = test::menuAction(&menu, {QStringLiteral("Versions"), QStringLiteral("Use as Comp Lane"), QStringLiteral("A")});
		QVERIFY2(entry != nullptr, "Versions > Use as Comp Lane has no entry for A");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("track.version_to_lane"));
		entry->trigger();

		const QJsonArray tracks = run(QStringLiteral("comp.lane_list"), {{QStringLiteral("track"), id}}).result.value(QStringLiteral("tracks")).toArray();
		QCOMPARE(tracks.size(), 1);
		QJsonObject lane;
		for (const QJsonValue& value : tracks.first().toObject().value(QStringLiteral("lanes")).toArray())
		{
			if (value.toObject().value(QStringLiteral("name")).toString() == QLatin1String("A")) { lane = value.toObject(); }
		}
		QVERIFY2(!lane.isEmpty(), "the menu entry did not add a take lane named A");
		QVERIFY2(!lane.value(QStringLiteral("takes")).toArray().isEmpty(), "the lane named A holds no copy of A's clips");
	}
};

QTEST_MAIN(ExternalDeviceCardTest)
#include "ExternalDeviceCardTest.moc"
