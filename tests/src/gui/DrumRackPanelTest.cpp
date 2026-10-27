/*
 * DrumRackPanelTest.cpp - Q050-066 stage 1: the drum rack's pad panel runs the agents' commands
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
/*! The rack folder's Folder menu opens "Drum Rack Pads..."; its Add Pad... button, a pad's name field, its
 *  trigger-key box and its Sample... button each run a track.drum_rack_* command, so what the panel did is
 *  what track.drum_rack_get_state reads, each edit is one undo step, and an agent's edit shows in the panel. */

#include <QtTest>

#include <QApplication>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QToolButton>

#include "ControlRegistry.h"
#include "DrumRackPanel.h"
#include "Engine.h"
#include "Song.h"
#include "TrackFolderMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString sample(const char* file)
{
	return QStringLiteral(ZENE_DRUM_SAMPLES_DIR "/") + QLatin1String(file);
}

QAction* actionIn(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = actionIn(action->menu(), text)) { return inner; }
		}
	}
	return nullptr;
}

DrumRackPanel* openPanel()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* panel = qobject_cast<DrumRackPanel*>(widget); panel != nullptr && panel->isVisible()) { return panel; }
	}
	return nullptr;
}

//! Type @a text into @a widget as a person does: select what is there, type, press Return.
void typeInto(QWidget* widget, const QString& text)
{
	QTest::keyClick(widget, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClicks(widget, text);
	QTest::keyClick(widget, Qt::Key_Return);
}

} // namespace

class DrumRackPanelTest : public QObject
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
		DrumRackPanel::setSampleChooser({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePanelEditsPadsThroughTheCommands()
	{
		const ControlResult made = run(QStringLiteral("track.drum_rack"), {{QStringLiteral("name"), QStringLiteral("Kit")},
			{QStringLiteral("pads"), QJsonArray{QJsonObject{{QStringLiteral("sample"), sample("bassdrum01.ogg")}}}}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const QString folder = made.result.value(QStringLiteral("folder")).toString();
		const auto pads = [folder] {
			return run(QStringLiteral("track.drum_rack_get_state"), {{QStringLiteral("track"), folder}}).result.value(QStringLiteral("pads")).toArray();
		};

		// The folder's own menu opens the panel.
		QMenu menu;
		addTrackFolderMenu(&menu, Engine::getSong()->findTrackById(folder.section(QLatin1Char('-'), 1).toInt()));
		QAction* open = actionIn(&menu, QObject::tr("Drum Rack Pads..."));
		QVERIFY2(open != nullptr, "the folder's menu has no Drum Rack Pads... item");
		open->trigger();
		DrumRackPanel* panel = openPanel();
		QVERIFY2(panel != nullptr, "the item opened no pad panel");
		QCOMPARE(panel->rowCount(), 1);
		QCOMPARE(panel->nameEdit(0)->text(), QStringLiteral("bassdrum01"));

		// Add Pad...: the sample asked for, then track.drum_rack_pad_add on the next key.
		DrumRackPanel::setSampleChooser([](QWidget*) { return sample("snare01.ogg"); });
		QTest::mouseClick(panel->addButton(), Qt::LeftButton);
		QVERIFY2(panel->rowCount() == 2, qPrintable(panel->statusText()));
		QCOMPARE(pads().size(), 2);
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("key")).toInt(), 37);

		// The name field and the key box: typed, then Return.
		typeInto(panel->nameEdit(1), QStringLiteral("Snare"));
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Snare"));
		typeInto(panel->keyBox(1), QStringLiteral("40"));
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("key")).toInt(), 40);

		// Sample...: the pad plays another file.
		DrumRackPanel::setSampleChooser([](QWidget*) { return sample("snare02.ogg"); });
		QTest::mouseClick(panel->sampleButton(1), Qt::LeftButton);
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("sample")).toString(), sample("snare02.ogg"));

		// An agent's edit shows in the panel.
		QVERIFY(run(QStringLiteral("track.drum_rack_pad_set"), {{QStringLiteral("track"), pads().at(0).toObject().value(QStringLiteral("track"))},
			{QStringLiteral("name"), QStringLiteral("Kick")}}).ok);
		panel->refresh();
		QCOMPARE(panel->nameEdit(0)->text(), QStringLiteral("Kick"));

		// Each panel edit was ONE undo step: the agent's, the sample, the key, the name, then the pad itself.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("sample")).toString(), sample("snare01.ogg"));
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("key")).toInt(), 40);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("key")).toInt(), 37);
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Snare"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pads().at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("snare01"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pads().size(), 1);
		panel->refresh();
		QCOMPARE(panel->rowCount(), 1);
		panel->close();
	}
};

QTEST_MAIN(DrumRackPanelTest)
#include "DrumRackPanelTest.moc"
