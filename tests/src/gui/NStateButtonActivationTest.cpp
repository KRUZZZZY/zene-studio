/*
 * NStateButtonActivationTest.cpp - an n-state button advances on every activation path
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

/*! Cluster gui-1105-main-loop-points: the timeline's "Loop points" button (an NStateButton) advanced
 *  its state only from a mouse press, so an accessibility / automation activation
 *  (QAbstractButton::click(), what QAccessible's press action calls) and a keyboard Space press left
 *  the loop disabled. Every activation path must advance exactly one state; a mouse click must still
 *  advance once (not twice), and a press dragged off the button keeps its press-time advance without
 *  swallowing the next activation. */

#include <QtTest>

#include <QAccessible>
#include <QPixmap>

#include "NStateButton.h"

using lmms::gui::NStateButton;

class NStateButtonActivationTest : public QObject
{
	Q_OBJECT

private:
	static void addTwoStates(NStateButton& button)
	{
		QPixmap off(16, 16);
		off.fill(Qt::black);
		QPixmap on(16, 16);
		on.fill(Qt::white);
		button.addState(off);
		button.addState(on);
	}

private slots:
	// The finding: QAbstractButton::click() (no mouse event) must advance the state.
	void programmaticClickAdvancesTheState()
	{
		NStateButton button(nullptr);
		button.setGeneralToolTip("Loop points");
		addTwoStates(button);
		QSignalSpy spy(&button, &NStateButton::changedState);
		QCOMPARE(button.state(), 0);

		button.click();
		QCOMPARE(button.state(), 1);
		QCOMPARE(spy.count(), 1);
		QCOMPARE(spy.at(0).at(0).toInt(), 1);

		button.click();
		QCOMPARE(button.state(), 0);
		QCOMPARE(spy.count(), 2);
	}

	// The accessibility press action, the path Luna's sweep drove.
	void accessiblePressActionAdvancesTheState()
	{
		NStateButton button(nullptr);
		addTwoStates(button);
		button.show();
		QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(&button);
		QVERIFY(iface != nullptr);
		QAccessibleActionInterface* actions = iface->actionInterface();
		QVERIFY(actions != nullptr);
		QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::pressAction()));

		actions->doAction(QAccessibleActionInterface::pressAction());
		QCOMPARE(button.state(), 1);
	}

	// Keyboard activation (Space) goes through click() as well.
	void spaceKeyAdvancesTheState()
	{
		NStateButton button(nullptr);
		addTwoStates(button);
		QTest::keyClick(&button, Qt::Key_Space);
		QCOMPARE(button.state(), 1);
	}

	// A mouse click still advances exactly once, at press time as before.
	void mouseClickAdvancesExactlyOnce()
	{
		NStateButton button(nullptr);
		addTwoStates(button);
		button.resize(20, 20);
		QSignalSpy spy(&button, &NStateButton::changedState);

		QTest::mousePress(&button, Qt::LeftButton, {}, QPoint(5, 5));
		QCOMPARE(button.state(), 1);
		QTest::mouseRelease(&button, Qt::LeftButton, {}, QPoint(5, 5));
		QCOMPARE(button.state(), 1);
		QCOMPARE(spy.count(), 1);
	}

	// A press released off the button keeps its press-time advance (no clicked()), and the
	// next programmatic activation is not swallowed.
	void pressReleasedOffTheButtonDoesNotSwallowTheNextClick()
	{
		NStateButton button(nullptr);
		addTwoStates(button);
		button.resize(20, 20);

		QTest::mousePress(&button, Qt::LeftButton, {}, QPoint(5, 5));
		QTest::mouseRelease(&button, Qt::LeftButton, {}, QPoint(200, 200));
		QCOMPARE(button.state(), 1);

		button.click();
		QCOMPARE(button.state(), 0);
	}

	// A right click does not advance on any path.
	void rightClickDoesNotAdvance()
	{
		NStateButton button(nullptr);
		addTwoStates(button);
		button.resize(20, 20);
		QTest::mouseClick(&button, Qt::RightButton, {}, QPoint(5, 5));
		QCOMPARE(button.state(), 0);
	}

	// A button with no states ignores activation.
	void emptyButtonIgnoresActivation()
	{
		NStateButton button(nullptr);
		button.click();
		QCOMPARE(button.state(), -1);
	}
};

QTEST_MAIN(NStateButtonActivationTest)
#include "NStateButtonActivationTest.moc"
