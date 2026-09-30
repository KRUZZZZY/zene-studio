/*
 * UndoHistoryPanelTest.cpp - Edit > Undo History lists, undoes and redoes through the registry
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

/*! The panel over a real engine: a command-surface edit (track.add) appears in its list, its
 *  summary reads the stack's depth, and its Undo and Redo buttons run control.undo / control.redo -
 *  the track goes and comes back - with the buttons' enabled state following the stack. */

#include <QtTest>

#include <QPushButton>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "UndoHistoryPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QPushButton* buttonFor(UndoHistoryPanel& panel, const char* command)
{
	for (QPushButton* button : panel.findChildren<QPushButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String(command)) { return button; }
	}
	return nullptr;
}

} // namespace

class UndoHistoryPanelTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePanelListsUndoesAndRedoes()
	{
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("sample")}}).ok);

		UndoHistoryPanel panel;
		QVERIFY(panel.rowCount() >= 1);
		QVERIFY2(panel.summary().contains(QStringLiteral("to undo")), qPrintable(panel.summary()));
		QPushButton* undo = buttonFor(panel, "control.undo");
		QPushButton* redo = buttonFor(panel, "control.redo");
		QVERIFY(undo != nullptr && redo != nullptr);
		QVERIFY(undo->isEnabled());

		QTest::mouseClick(undo, Qt::LeftButton);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
		QVERIFY(redo->isEnabled());

		QTest::mouseClick(redo, Qt::LeftButton);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 1);
	}
};

QTEST_MAIN(UndoHistoryPanelTest)
#include "UndoHistoryPanelTest.moc"
