/*
 * TempoMapPanelTest.cpp - Edit > Tempo Map lists, adds, removes, activates and clears
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

/*! The panel over a real engine, every edit through its transport.tempo_map_* command: an event
 *  added at a tick appears as a row (with its bar) and in transport.tempo_map_get; the Active box
 *  follows and sets the map's switch; Remove deletes the selected row's event; Clear empties it.
 *  And the Standard MIDI File round trip the Export MIDI... / Import MIDI... buttons run: a map
 *  exported, cleared and imported comes back with its events, and a file that is not MIDI is
 *  refused with the map left as it was. Theme 7: an event's position reads in bars and beats, and in ticks
 *  once the panel's own picker (transport.time_format) chooses them; Remove still finds the event's tick. */

#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "TempoMapPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QJsonObject mapState()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_get"), QJsonObject{}).result;
}

//! The panel's own "Ramp to Next" button: the only one whose label names a ramp.
QPushButton* rampButton(QWidget* panel)
{
	for (QPushButton* button : panel->findChildren<QPushButton*>())
	{
		if (button->text().contains(QStringLiteral("Ramp"))) { return button; }
	}
	return nullptr;
}

} // namespace

class TempoMapPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePanelEditsTheMapThroughTheCommands()
	{
		TempoMapPanel panel;
		QCOMPARE(panel.rowCount(), 0);
		QVERIFY(panel.addEvent(0, 120));
		QVERIFY(panel.addEvent(4 * 192, 90));  // bar 5
		QCOMPARE(panel.rowCount(), 2);
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 2);
		auto* table = panel.findChild<QTableWidget*>();
		QCOMPARE(table->item(1, 0)->text(), QStringLiteral("5"));
		QCOMPARE(table->item(1, 1)->text(), QStringLiteral("5.1.1"));  // bars and beats by default
		QCOMPARE(table->item(1, 2)->text(), QStringLiteral("90"));
		auto* format = panel.findChild<QComboBox*>();
		QVERIFY(format != nullptr);
		QCOMPARE(format->property("controlCommand").toString(), QStringLiteral("transport.time_format"));
		format->setCurrentIndex(1);
		emit format->activated(1);  // as a person picks Ticks
		QCoreApplication::processEvents();
		QCOMPARE(table->item(1, 1)->text(), QStringLiteral("768 ticks"));
		format->setCurrentIndex(0);
		emit format->activated(0);
		QCoreApplication::processEvents();
		QCOMPARE(table->item(1, 1)->text(), QStringLiteral("5.1.1"));

		auto* active = panel.findChild<QCheckBox*>();
		const bool wasActive = mapState().value(QStringLiteral("active")).toBool();
		QCOMPARE(active->isChecked(), wasActive);
		active->setChecked(!wasActive);
		QCOMPARE(mapState().value(QStringLiteral("active")).toBool(), !wasActive);

		QVERIFY(panel.removeRow(1));
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 1);
		QCOMPARE(panel.rowCount(), 1);

		for (QPushButton* button : panel.findChildren<QPushButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("transport.tempo_map_clear")) { button->click(); }
		}
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 0);
		QCOMPARE(panel.rowCount(), 0);
	}

	void theMapRoundTripsThroughAMidiFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TempoMapPanel panel;
		QVERIFY(panel.addEvent(0, 128));
		QVERIFY(panel.addEvent(8 * 192, 100));  // bar 9
		const QString file = dir.filePath(QStringLiteral("conductor.mid"));
		QCOMPARE(panel.exportTo(file), QString());
		QVERIFY(QFileInfo(file).size() > 0);
		// Written again over the same file: the panel's own save dialog has already asked.
		QCOMPARE(panel.exportTo(file), QString());

		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		panel.refresh();
		QCOMPARE(panel.rowCount(), 0);
		QCOMPARE(panel.importFrom(file), QString());
		QCOMPARE(panel.rowCount(), 2);
		auto* table = panel.findChild<QTableWidget*>();
		QCOMPARE(table->item(1, 0)->text(), QStringLiteral("9"));
		QCOMPARE(table->item(1, 2)->text(), QStringLiteral("100"));

		const QString notMidi = dir.filePath(QStringLiteral("not.mid"));
		{
			QFile garbage(notMidi);
			QVERIFY(garbage.open(QIODevice::WriteOnly));
			garbage.write("this is not a MIDI file");
		}
		QVERIFY(!panel.importFrom(notMidi).isEmpty());
		QCOMPARE(panel.rowCount(), 2);
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
	}

	//! BUG 6 (cluster 1768): the bar column counted one metre for the whole map. A 3/4 event at tick
	//! 192 shortens every bar after it, so the event at tick 336 is bar 3 - reading it as bar 2 came
	//! from dividing every tick by the project's single ticksPerBar and ignoring the map's own
	//! time-signature events.
	void theBarColumnCountsTheTimeSignatureEvents()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		TempoMapPanel panel;
		QVERIFY(panel.addEvent(0, 120));
		// 3/4 from tick 192: 144 ticks a bar, so tick 336 opens bar 3.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 192}, {QStringLiteral("bpm"), 120}, {QStringLiteral("numerator"), 3},
				{QStringLiteral("denominator"), 4}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 336}, {QStringLiteral("bpm"), 120}, {QStringLiteral("numerator"), 3},
				{QStringLiteral("denominator"), 4}}).ok);
		panel.refresh();
		auto* table = panel.findChild<QTableWidget*>();
		QCOMPARE(panel.rowCount(), 3);
		QCOMPARE(table->item(0, 0)->text(), QStringLiteral("1"));  // tick 0
		QCOMPARE(table->item(1, 0)->text(), QStringLiteral("2"));  // tick 192, the 3/4 bar itself
		QCOMPARE(table->item(2, 0)->text(), QStringLiteral("3"));  // tick 336: bar 2 ended at 192+144
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
	}

	//! BUG 7 (cluster new-p9-475): "Add at playhead" sent the SONG's global time signature (and its
	//! global tempo) with the new event, so it overwrote what the map had in force at that tick: a
	//! 3/4 map met a 4/4 song and the add put the song's 4/4 back into the map.
	void theAddAtPlayheadKeepsTheMappedMetreAndTempo()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 0);
		// 3/4 at 120 BPM from tick 0 in the map; the song's own metre (4/4) and tempo stay as they are.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}, {QStringLiteral("numerator"), 3},
				{QStringLiteral("denominator"), 4}}).ok);
		TempoMapPanel panel;
		// What the "Add at playhead" button does: the event at the play position, with the tempo and
		// the metre the MAP has in force there (0: the map's own tempo).
		QVERIFY(panel.addEvent(480, 0));
		const QJsonArray events = mapState().value(QStringLiteral("events")).toArray();
		QCOMPARE(events.size(), 2);
		const QJsonObject added = events.at(1).toObject();
		QCOMPARE(added.value(QStringLiteral("tick")).toInt(), 480);
		QCOMPARE(added.value(QStringLiteral("numerator")).toInt(), 3);  // the metre in force, not the song's 4/4
		QCOMPARE(added.value(QStringLiteral("denominator")).toInt(), 4);
		QCOMPARE(added.value(QStringLiteral("bpm")).toInt(), 120);  // the tempo in force, not the song's
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
	}

	//! BUG 4 (cluster new-p17-187): the Ramp button was enabled for ANY selected row, but a
	//! time-signature-only row has no tempo to ramp - the click did nothing and said nothing.
	void theRampButtonIsOffOnAMetreOnlyRow()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 0);
		// A metre-only event (no tempo) and a tempo event.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("numerator"), 3},
				{QStringLiteral("denominator"), 4}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 192}, {QStringLiteral("bpm"), 120}}).ok);
		TempoMapPanel panel;
		auto* table = panel.findChild<QTableWidget*>();
		auto* ramp = rampButton(&panel);
		QVERIFY(table != nullptr);
		QVERIFY(ramp != nullptr);
		QCOMPARE(panel.rowCount(), 2);
		table->setCurrentCell(0, 0);  // the metre-only row: nothing to ramp
		QVERIFY(!ramp->isEnabled());
		table->setCurrentCell(1, 0);  // the tempo row
		QVERIFY(ramp->isEnabled());
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
	}

	//! BUG 3 (cluster new-p17-186): a metre change BETWEEN bar boundaries left the bar grid anchored
	//! to the old origin, so the bars after it were counted from boundaries that do not follow it.
	void anOffGridMetreChangeStartsItsOwnBar()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
		QCOMPARE(mapState().value(QStringLiteral("event_count")).toInt(), 0);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		// 3/4 from tick 100, in the middle of the first 4/4 bar.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 100}, {QStringLiteral("numerator"), 3},
				{QStringLiteral("denominator"), 4}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 250}, {QStringLiteral("bpm"), 100}}).ok);
		TempoMapPanel panel;
		panel.refresh();
		auto* table = panel.findChild<QTableWidget*>();
		QCOMPARE(panel.rowCount(), 3);
		QCOMPARE(table->item(0, 0)->text(), QStringLiteral("1"));  // tick 0
		QCOMPARE(table->item(1, 0)->text(), QStringLiteral("2"));  // tick 100: the 3/4 bar opens here
		QCOMPARE(table->item(2, 0)->text(), QStringLiteral("3"));  // tick 250: that bar ended at 100+144
		ControlRegistry::instance()->invoke(QStringLiteral("transport.tempo_map_clear"), QJsonObject{});
	}
};

QTEST_MAIN(TempoMapPanelTest)
#include "TempoMapPanelTest.moc"
