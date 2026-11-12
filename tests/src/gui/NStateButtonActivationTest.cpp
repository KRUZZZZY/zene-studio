/*
 * NStateButtonActivationTest.cpp - a multi-state button advances on every way of activating it
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

/*! cluster gui-1030-main-loop-points: the timeline's "Loop points" button (an NStateButton) advanced
 *  its state only in mousePressEvent, so the activations that never press the mouse - the accessible
 *  button action (QAbstractButton::click()), the keyboard's Space - emitted clicked() and left the
 *  state, the Timeline's loop flag and the ruler unchanged. Each path is driven here on the real
 *  toolbar TimeLineWidget::addToolButtons builds, and the mouse path is held so it still advances
 *  exactly once per click. */

#include <QtTest>

#include <QToolBar>
#include <QToolButton>

#include <memory>

#include "Engine.h"
#include "NStateButton.h"
#include "Song.h"
#include "TimeLineWidget.h"
#include "Timeline.h"

using namespace lmms;
using namespace lmms::gui;

class NStateButtonActivationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void init()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopEnabled(false);
		m_ruler = std::make_unique<TimeLineWidget>(0, 0, 16.f, timeline, TimePos(0), nullptr);
		m_bar = std::make_unique<QToolBar>();
		m_ruler->addToolButtons(m_bar.get());
		m_loop = nullptr;
		for (auto* button : m_bar->findChildren<NStateButton*>())
		{
			if (button->accessibleName() == QStringLiteral("Loop points")) { m_loop = button; }
		}
		QVERIFY2(m_loop, "the toolbar has no 'Loop points' button");
		QCOMPARE(m_loop->state(), 0);
		QVERIFY(!timeline.loopEnabled());
	}

	void cleanup()
	{
		m_loop = nullptr;
		m_bar.reset();
		m_ruler.reset();
	}

	// The finding: the accessible button action is QAbstractButton::click().
	void anAccessibleActivationEnablesTheLoop()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		QSignalSpy changed(m_loop, &NStateButton::changedState);

		m_loop->click();
		QCOMPARE(m_loop->state(), 1);
		QVERIFY(timeline.loopEnabled());
		QCOMPARE(changed.count(), 1);

		m_loop->click();  // and a second activation turns it off again
		QCOMPARE(m_loop->state(), 0);
		QVERIFY(!timeline.loopEnabled());
	}

	// The keyboard path into the same handler: Space on the focused button.
	void theSpaceKeyEnablesTheLoop()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		QTest::keyClick(m_loop, Qt::Key_Space);
		QCOMPARE(m_loop->state(), 1);
		QVERIFY(timeline.loopEnabled());
	}

	// The mouse path must keep advancing exactly once per click (not zero, not twice).
	void aMouseClickAdvancesExactlyOneState()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		QSignalSpy changed(m_loop, &NStateButton::changedState);

		QTest::mouseClick(m_loop, Qt::LeftButton);
		QCOMPARE(m_loop->state(), 1);
		QVERIFY(timeline.loopEnabled());
		QCOMPARE(changed.count(), 1);

		QTest::mouseClick(m_loop, Qt::LeftButton);
		QCOMPARE(m_loop->state(), 0);
		QVERIFY(!timeline.loopEnabled());
	}

	// Only the left button activates it: a right click does not move the state.
	void aRightClickLeavesTheStateAlone()
	{
		QTest::mouseClick(m_loop, Qt::RightButton);
		QCOMPARE(m_loop->state(), 0);
	}

	// The timeline's own change (a loop toggled from the ruler's menu) still drives the button.
	void aTimelineChangeStillMovesTheButton()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopEnabled(true);
		QCOMPARE(m_loop->state(), 1);
		m_loop->click();
		QCOMPARE(m_loop->state(), 0);
		QVERIFY(!timeline.loopEnabled());
	}

	// The other multi-state buttons share the class: the three-state auto-scroll cycles by click().
	void aThreeStateButtonCyclesThroughAllItsStates()
	{
		NStateButton* scroll = nullptr;
		for (auto* button : m_bar->findChildren<NStateButton*>())
		{
			if (button->accessibleName() != QStringLiteral("Loop points") && button->toolTip().contains(QStringLiteral("scroll"), Qt::CaseInsensitive))
			{
				scroll = button;
			}
		}
		QVERIFY2(scroll, "no auto-scroll button");
		const int start = scroll->state();
		scroll->click();
		QCOMPARE(scroll->state(), (start + 1) % 3);
		scroll->click();
		scroll->click();
		QCOMPARE(scroll->state(), start);
	}

private:
	std::unique_ptr<TimeLineWidget> m_ruler;
	std::unique_ptr<QToolBar> m_bar;
	NStateButton* m_loop = nullptr;
};

QTEST_MAIN(NStateButtonActivationTest)
#include "NStateButtonActivationTest.moc"
