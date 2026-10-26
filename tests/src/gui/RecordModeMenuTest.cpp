/*
 * RecordModeMenuTest.cpp - Q050-094: the record mode is ONE control for MIDI and audio
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

/*! The Record menu's "Record Mode" submenu and the header's Rec mode readout offer the same three
 *  choices (Overdub, Replace, Keep History) that transport.record_options takes, and choosing one
 *  in either place runs that command: the preference changes, and so does what the other control
 *  shows. The menu is installed on a bare QMenuBar exactly as the shell installs it, and its
 *  actions are triggered, not the command called. */

#include <QtTest>

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

#include "ControlRegistry.h"
#include "Engine.h"
#include "RecordLaunchMenus.h"
#include "RecordOptions.h"
#include "TransportReadouts.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QMenu* recordModeSubmenu(QMainWindow& window)
{
	QMenu* record = window.findChild<QMenu*>(QStringLiteral("recordMenu"));
	if (record == nullptr) { return nullptr; }
	for (QAction* action : record->actions())
	{
		if (action->menu() != nullptr && action->text() == QObject::tr("Record Mode")) { return action->menu(); }
	}
	return nullptr;
}

} // namespace

class RecordModeMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		recordoptions::setOptions({});
	}

	void cleanupTestCase()
	{
		recordoptions::setOptions({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The submenu is titled for both media, lists every mode the command takes, and each entry
	//! runs transport.record_options and leaves itself checked the next time the menu opens.
	void theRecordMenuOffersTheThreeModesAndEachOneRunsTheCommand()
	{
		QMainWindow window;
		installRecordLaunchMenus(window.menuBar());
		QMenu* sub = recordModeSubmenu(window);
		QVERIFY2(sub != nullptr, "the Record menu has no 'Record Mode' submenu (it was titled '(MIDI)')");

		const QStringList names = recordoptions::modeNames();
		QCOMPARE(names, (QStringList{QStringLiteral("overdub"), QStringLiteral("replace"), QStringLiteral("keep_history")}));
		QCOMPARE(sub->actions().size(), names.size());
		for (int i = 0; i < names.size(); ++i)
		{
			QAction* action = sub->actions().at(i);
			const auto mode = static_cast<recordoptions::Mode>(i);
			QCOMPARE(action->text(), recordoptions::modeChoice(mode));
			QCOMPARE(action->toolTip(), recordoptions::modeScope(mode));
			QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("transport.record_options"));
			action->trigger();
			QVERIFY2(recordoptions::options().mode == mode, qPrintable(names.at(i)));
		}
		// The submenu names what the last choice does to audio as well as MIDI.
		QVERIFY(recordoptions::modeScope(recordoptions::Mode::KeepHistory).contains(QStringLiteral("take lane")));

		recordoptions::setOptions({});
		QMetaObject::invokeMethod(sub, "aboutToShow");
		QVERIFY(sub->actions().at(0)->isChecked());
		sub->actions().at(1)->trigger();
		QMetaObject::invokeMethod(sub, "aboutToShow");
		QVERIFY(sub->actions().at(1)->isChecked() && !sub->actions().at(0)->isChecked());
	}

	//! The header's Rec mode readout offers the same three choices (right-click) and takes typed words.
	void theHeaderReadoutOffersTheSameThreeChoices()
	{
		recordoptions::setOptions({});
		TransportReadouts readouts;
		ReadoutField* field = readouts.recordMode();
		QVERIFY(field != nullptr);
		QCOMPARE(field->actions().size(), 3);
		QCOMPARE(field->valueText(), recordoptions::modeLabel(recordoptions::Mode::Overdub));

		field->actions().at(2)->trigger();  // Keep History
		QVERIFY(recordoptions::options().mode == recordoptions::Mode::KeepHistory);
		readouts.tick();  // the readouts' periodic sync follows a mode set elsewhere
		for (int i = 0; i < 14; ++i) { readouts.tick(); }
		QCOMPARE(field->valueText(), QStringLiteral("Keep History"));
		QVERIFY2(field->toolTip().contains(recordoptions::modeScope(recordoptions::Mode::KeepHistory)), qPrintable(field->toolTip()));

		field->actions().at(1)->trigger();
		QVERIFY(recordoptions::options().mode == recordoptions::Mode::Replace);
		recordoptions::setOptions({});
	}
};

QTEST_MAIN(RecordModeMenuTest)
#include "RecordModeMenuTest.moc"
