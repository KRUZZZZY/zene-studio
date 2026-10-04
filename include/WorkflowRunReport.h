/*
 * WorkflowRunReport.h - a workflow run's outcome, step by step, with Retry and Skip
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

#ifndef LMMS_GUI_WORKFLOW_RUN_REPORT_H
#define LMMS_GUI_WORKFLOW_RUN_REPORT_H

#include <QDialog>
#include <QJsonObject>

#include "lmms_export.h"

class QLabel;
class QPushButton;

namespace lmms::gui
{

class ListTable;

/*! Design theme 19: what a workflow run did, kept on screen until closed rather than floated for a few
 *  seconds - the outcome's sentence (which step failed and why, what applied and stays applied, what did not
 *  run), then every step with its outcome in words (Applied, Failed: the reason, Skipped, Not run - never
 *  colour alone), and the recovery: Retry Step N (workflow.resume) and Skip Step N and Continue
 *  (workflow.resume 'skip'). A resumed run's outcome updates the same report. */
class LMMS_EXPORT WorkflowRunReport : public QDialog
{
	Q_OBJECT
public:
	explicit WorkflowRunReport(QWidget* parent = nullptr);

	//! Show @a outcome (workflow.status's body once a run is done).
	void setOutcome(const QJsonObject& outcome);
	QString summaryText() const;
	//! Step @a index's outcome as the list shows it ("Applied", "Failed: ...", "Skipped", "Not run").
	QString stepOutcome(int index) const;
	int stepCount() const;
	//! Whether Retry and Skip can be pressed (the run stopped at a failing step).
	bool canResume() const;

	/*! A run that stopped at a failure is shown in @a window's report, made when there is none; a finished run
	 *  updates a report already open (a resume that went through) and opens none. Returns the report, or nullptr. */
	static WorkflowRunReport* showOutcome(QWidget* window, const QJsonObject& outcome);

private:
	void resume(bool skip);

	QJsonObject m_outcome;
	QLabel* m_summary = nullptr;
	ListTable* m_steps = nullptr;
	QPushButton* m_retry = nullptr;
	QPushButton* m_skip = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_WORKFLOW_RUN_REPORT_H
