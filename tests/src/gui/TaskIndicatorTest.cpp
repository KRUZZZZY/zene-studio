/*
 * TaskIndicatorTest.cpp - the status bar's Tasks indicator and its list
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

/*! 0.5.0 UX decision (theme 24): the indicator counts the running jobs in words ("TASKS 2"), is a keyboard target
 *  of at least the token size, opens a non-modal list with a row per job - Cancel only where the job can stop, wired
 *  to task.cancel - and moves only the progress bars while nothing else changes, so a focused Cancel keeps its focus.
 *  task.show refuses, typed, without a main window. */

#include <QtTest>

#include <QProgressBar>
#include <QPushButton>
#include <QSpacerItem>

#include "ControlRegistry.h"
#include "ControlLayout.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "TaskCenter.h"
#include "TaskIndicator.h"

using namespace lmms;
using namespace lmms::gui;

class TaskIndicatorTest : public QObject
{
	Q_OBJECT

private:
	int m_owner = 0;  //!< its address is the jobs' owner key

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

	void cleanup()
	{
		TaskCenter::instance().forget(&m_owner);
		TaskCenter::instance().clearFinished();
	}

	void theIndicatorCountsRunningJobsInWords()
	{
		TaskIndicator indicator;
		QCOMPARE(indicator.label(), QStringLiteral("TASKS"));
		QCOMPARE(indicator.focusPolicy(), Qt::TabFocus);
		QVERIFY(indicator.sizeHint().height() >= tokens::targetSize());
		QCOMPARE(indicator.accessibleName(), QStringLiteral("Tasks"));
		const TaskToken renderToken = TaskCenter::instance().begin(&m_owner, 1, {QStringLiteral("render"), QStringLiteral("Render a.wav"), {}, {}, false});
		TaskCenter::instance().begin(&m_owner, 2, {QStringLiteral("index"), QStringLiteral("Index"), {}, {}, true});
		QCOMPARE(indicator.label(), QStringLiteral("TASKS 2"));
		QVERIFY(indicator.accessibleDescription().contains(QLatin1Char('2')));
		TaskCenter::instance().finish(renderToken, TaskState::Done);
		QCOMPARE(indicator.label(), QStringLiteral("TASKS 1"));
	}

	void theListShowsARowPerJobWithCancelWhereItCanStop()
	{
		bool stopped = false;
		TaskCenter::instance().begin(&m_owner, 3, {QStringLiteral("stem_split"), QStringLiteral("Split stems"),
			[&stopped] { stopped = true; }, {}, false});
		TaskCenter::instance().begin(&m_owner, 4, {QStringLiteral("mastering"), QStringLiteral("Master the mix"), {}, {}, false});
		QWidget host;
		host.resize(400, 300);
		auto* indicator = new TaskIndicator(&host);
		indicator->move(10, 260);
		host.show();
		QVERIFY(indicator->setListShown(1));
		TaskList* list = indicator->list();
		QCOMPARE(list->rowCount(), 2);
		QVERIFY(list->cancelButton(0) == nullptr);  // newest first: the mastering run, which runs to its end
		QPushButton* cancel = list->cancelButton(1);
		QVERIFY(cancel != nullptr);
		QVERIFY(!cancel->accessibleName().isEmpty());
		cancel->click();  // task.cancel through the registry
		QVERIFY(stopped);
		QVERIFY(!indicator->setListShown(0));
		QVERIFY(!list->isVisible());
	}

	void onlyTheProgressMovesWhileNothingElseChanges()
	{
		double done = 0.2;
		const TaskToken extractToken = TaskCenter::instance().begin(&m_owner, 5, {QStringLiteral("video_audio"),
			QStringLiteral("Extract audio"), [] {}, [&done] { return done; }, false});
		TaskList list;
		list.show();
		list.refresh();
		QPushButton* cancel = list.cancelButton(0);
		QVERIFY(cancel != nullptr);
		auto* bar = list.findChild<QProgressBar*>();
		QVERIFY(bar != nullptr);
		QCOMPARE(bar->value(), 200);
		done = 0.6;
		list.refresh();
		QCOMPARE(list.cancelButton(0), cancel);  // the same row: not rebuilt
		QCOMPARE(bar->value(), 600);
		TaskCenter::instance().finish(extractToken, TaskState::Done);
		list.refresh();
		QVERIFY(list.cancelButton(0) == nullptr);  // ended: rebuilt, without Cancel
	}

	void anUnknownLengthShowsABusyBar()
	{
		TaskCenter::instance().begin(&m_owner, 6, {QStringLiteral("mastering"), QStringLiteral("Master"), {}, {}, false});
		TaskList list;
		list.show();
		list.refresh();
		auto* bar = list.findChild<QProgressBar*>();
		QVERIFY(bar != nullptr);
		QCOMPARE(bar->maximum(), 0);  // Qt's busy indicator
	}

	void showingTheListNeedsAWindow()
	{
		const ControlResult shown = ControlRegistry::instance()->invoke(QStringLiteral("task.show"));
		QVERIFY(!shown.ok);
		QCOMPARE(shown.errorKind, ControlErrorKind::Requires);
	}

	void nonWidgetItemsCanBeIndexedAndTaken()
	{
		QWidget parent;
		ControlLayout layout(&parent);
		auto* spacer = new QSpacerItem(8, 8);
		layout.addItem(spacer);

		QCOMPARE(layout.count(), 1);
		QCOMPARE(layout.itemAt(0), static_cast<QLayoutItem*>(spacer));
		QCOMPARE(layout.takeAt(0), static_cast<QLayoutItem*>(spacer));
		delete spacer;
	}

	void nonWidgetItemsAreDrainedOnDestruction()
	{
		QWidget parent;
		{
			ControlLayout layout(&parent);
			layout.addItem(new QSpacerItem(8, 8));
			QCOMPARE(layout.count(), 1);
		}
	}
};

QTEST_MAIN(TaskIndicatorTest)
#include "TaskIndicatorTest.moc"
