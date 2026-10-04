/*
 * WorkflowRunReport.cpp - a workflow run's outcome, step by step, with Retry and Skip
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

#include "WorkflowRunReport.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "ListTable.h"

namespace lmms::gui
{

namespace
{

const QString ReportName = QStringLiteral("workflowRunReport");

//! Step @a number's outcome in words, from the outcome's applied, skipped and failed fields.
QString outcomeOf(const QJsonObject& outcome, int number)
{
	const QJsonObject failed = outcome.value(QStringLiteral("failed")).toObject();
	if (failed.value(QStringLiteral("step")).toInt() == number)
	{
		return WorkflowRunReport::tr("Failed: %1").arg(failed.value(QStringLiteral("error")).toString());
	}
	if (outcome.value(QStringLiteral("applied_steps")).toArray().contains(number)) { return WorkflowRunReport::tr("Applied"); }
	if (outcome.value(QStringLiteral("skipped_steps")).toArray().contains(number)) { return WorkflowRunReport::tr("Skipped"); }
	return WorkflowRunReport::tr("Not run");
}

} // namespace

WorkflowRunReport::WorkflowRunReport(QWidget* parent) :
	QDialog(parent)
{
	setObjectName(ReportName);
	setModal(false);
	auto* layout = new QVBoxLayout(this);
	m_summary = new QLabel(this);
	m_summary->setObjectName(QStringLiteral("workflowRunSummary"));
	m_summary->setWordWrap(true);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);  // copyable
	m_summary->setFocusPolicy(Qt::StrongFocus);
	m_summary->setAccessibleName(tr("Outcome"));
	layout->addWidget(m_summary);
	m_steps = new ListTable(this);
	m_steps->setAccessibleName(tr("Steps and their outcomes"));
	m_steps->setAccessibleDescription(tr("Each step of the workflow: Applied, Failed with the reason, Skipped or Not run"));
	layout->addWidget(m_steps, 1);
	auto* buttons = new QHBoxLayout;
	m_retry = new QPushButton(this);
	m_retry->setProperty("controlCommand", QStringLiteral("workflow.resume"));
	m_skip = new QPushButton(this);
	m_skip->setProperty("controlCommand", QStringLiteral("workflow.resume"));
	auto* close = new QPushButton(tr("Close"), this);
	buttons->addWidget(m_retry);
	buttons->addWidget(m_skip);
	buttons->addStretch(1);
	buttons->addWidget(close);
	layout->addLayout(buttons);
	connect(m_retry, &QPushButton::clicked, this, [this] { resume(false); });
	connect(m_skip, &QPushButton::clicked, this, [this] { resume(true); });
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	resize(600, 380);
}

void WorkflowRunReport::setOutcome(const QJsonObject& outcome)
{
	m_outcome = outcome;
	const bool stopped = outcome.value(QStringLiteral("resumable")).toBool();
	const QString name = outcome.value(QStringLiteral("name")).toString();
	setWindowTitle(stopped ? tr("Workflow Stopped: %1").arg(name) : tr("Workflow Finished: %1").arg(name));
	m_summary->setText(outcome.value(QStringLiteral("message")).toString());
	QList<ListRow> rows;
	for (const QJsonValue& value : outcome.value(QStringLiteral("steps")).toArray())
	{
		const QJsonObject step = value.toObject();
		const int number = step.value(QStringLiteral("step")).toInt();
		rows.append({{QString::number(number), step.value(QStringLiteral("name")).toString(), outcomeOf(outcome, number)},
			{QString(), step.value(QStringLiteral("command")).toString(), QString()}, QString()});
	}
	const int failed = outcome.value(QStringLiteral("failed")).toObject().value(QStringLiteral("step")).toInt();
	m_steps->setRows({tr("Step"), tr("Command"), tr("Outcome")}, rows, failed - 1);
	m_steps->resizeColumnsToContents();
	m_retry->setText(tr("Retry Step %1").arg(failed));
	m_skip->setText(tr("Skip Step %1 and Continue").arg(failed));
	m_retry->setVisible(stopped);
	m_skip->setVisible(stopped);
	if (stopped) { m_retry->setFocus(); }
	a11y::announceStatus(m_summary, m_summary->text(), stopped ? a11y::Politeness::Assertive : a11y::Politeness::Polite);
}

QString WorkflowRunReport::summaryText() const
{
	return m_summary->text();
}

QString WorkflowRunReport::stepOutcome(int index) const
{
	return outcomeOf(m_outcome, index + 1);
}

int WorkflowRunReport::stepCount() const
{
	return static_cast<int>(m_outcome.value(QStringLiteral("steps")).toArray().size());
}

bool WorkflowRunReport::canResume() const
{
	return m_outcome.value(QStringLiteral("resumable")).toBool();
}

void WorkflowRunReport::resume(bool skip)
{
	// Through the registry, as the socket and the menus resume; the resumed run's outcome comes back to this report.
	runCommand(this, CommandCall{QStringLiteral("workflow.resume"), {{QStringLiteral("skip"), skip}}, {},
		skip ? m_skip->text() : m_retry->text(), false});
}

WorkflowRunReport* WorkflowRunReport::showOutcome(QWidget* window, const QJsonObject& outcome)
{
	auto* report = window->findChild<WorkflowRunReport*>(ReportName, Qt::FindDirectChildrenOnly);
	const bool stopped = outcome.value(QStringLiteral("resumable")).toBool();
	if (report == nullptr && !stopped) { return nullptr; }  // a run that went through needs no window
	if (report == nullptr)
	{
		report = new WorkflowRunReport(window);
		report->setAttribute(Qt::WA_DeleteOnClose);
	}
	report->setOutcome(outcome);
	report->show();
	report->raise();
	return report;
}

} // namespace lmms::gui
