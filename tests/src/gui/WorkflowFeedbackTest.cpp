/*
 * WorkflowFeedbackTest.cpp - design theme 19: the recording indicator and the run report
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

/*! Design theme 19's visible parts, as widgets: the recording indicator says what is being recorded in words
 *  (not colour alone) and its Stop runs workflow.record_stop; the run report says which step failed and why,
 *  lists every step's outcome in words, offers Retry and Skip only while the run can be resumed, opens for a
 *  run that stopped and not for one that went through, and is updated - not duplicated - by a resumed run. */

#include <QtTest>

#include <QJsonArray>
#include <QPushButton>

#include "WorkflowRecordingIndicator.h"
#include "WorkflowRunReport.h"

using namespace lmms::gui;

namespace
{

QJsonObject stepOf(int number, const char* command)
{
	return {{QStringLiteral("step"), number}, {QStringLiteral("command"), QLatin1String(command)},
		{QStringLiteral("name"), QLatin1String(command)}};
}

//! A run of three steps that stopped at the second (or, @a finished, went through with it skipped).
QJsonObject outcome(bool finished)
{
	QJsonObject out{{QStringLiteral("name"), QStringLiteral("demo")}, {QStringLiteral("state"), QStringLiteral("done")},
		{QStringLiteral("total"), 3}, {QStringLiteral("steps"), QJsonArray{stepOf(1, "track.add"), stepOf(2, "track.remove"),
			stepOf(3, "track.add")}}, {QStringLiteral("resumable"), !finished},
		{QStringLiteral("message"), finished ? QStringLiteral("Workflow 'demo' finished.") : QStringLiteral("Workflow 'demo' stopped at step 2 of 3.")}};
	if (finished)
	{
		out.insert(QStringLiteral("applied_steps"), QJsonArray{1, 3});
		out.insert(QStringLiteral("skipped_steps"), QJsonArray{2});
		return out;
	}
	out.insert(QStringLiteral("applied_steps"), QJsonArray{1});
	out.insert(QStringLiteral("not_run"), QJsonArray{3});
	out.insert(QStringLiteral("failed"), QJsonObject{{QStringLiteral("step"), 2}, {QStringLiteral("error"), QStringLiteral("no track 'x'")}});
	return out;
}

} // namespace

class WorkflowFeedbackTest : public QObject
{
	Q_OBJECT

private slots:
	void theIndicatorSaysWhatIsRecordedAndStops()
	{
		WorkflowRecordingIndicator indicator;
		QVERIFY(indicator.isHidden());
		indicator.setRecording(true, QStringLiteral("Bounce stems"), 3);
		QVERIFY(!indicator.isHidden());
		QVERIFY2(indicator.text().contains(QStringLiteral("Bounce stems")) && indicator.text().contains(QStringLiteral("3")),
			qPrintable(indicator.text()));
		QCOMPARE(indicator.accessibleName(), indicator.text());
		QCOMPARE(indicator.stopButton()->property("controlCommand").toString(), QStringLiteral("workflow.record_stop"));
		QVERIFY(!indicator.stopButton()->accessibleName().isEmpty());
		indicator.setRecording(false, QString(), 0);
		QVERIFY(indicator.isHidden());
	}

	void theReportSaysWhereItStoppedAndOffersRecovery()
	{
		QWidget window;
		QVERIFY(WorkflowRunReport::showOutcome(&window, outcome(true)) == nullptr);  // went through, none open: nothing
		WorkflowRunReport* report = WorkflowRunReport::showOutcome(&window, outcome(false));
		QVERIFY(report != nullptr && report->isVisible());
		QCOMPARE(report->stepCount(), 3);
		QCOMPARE(report->stepOutcome(0), QStringLiteral("Applied"));
		QCOMPARE(report->stepOutcome(1), QStringLiteral("Failed: no track 'x'"));
		QCOMPARE(report->stepOutcome(2), QStringLiteral("Not run"));
		QVERIFY(report->summaryText().contains(QStringLiteral("step 2 of 3")));
		QVERIFY(report->canResume());
		QCOMPARE(WorkflowRunReport::showOutcome(&window, outcome(true)), report);  // the resume's outcome, same window
		QCOMPARE(report->stepOutcome(1), QStringLiteral("Skipped"));
		QCOMPARE(report->stepOutcome(2), QStringLiteral("Applied"));
		QVERIFY(!report->canResume());
		QCOMPARE(window.findChildren<WorkflowRunReport*>().size(), 1);
	}
};

QTEST_MAIN(WorkflowFeedbackTest)
#include "WorkflowFeedbackTest.moc"
