/*
 * WorkflowRecordingIndicator.cpp - the header's "recording a workflow" indicator, with Stop
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

#include "WorkflowRecordingIndicator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "DesignTokens.h"
#include "MainWindow.h"
#include "Shell.h"
#include "ShellChrome.h"

namespace lmms::gui
{

namespace
{

const QString IndicatorName = QStringLiteral("workflowRecording");

} // namespace

WorkflowRecordingIndicator::WorkflowRecordingIndicator(QWidget* parent) :
	QFrame(parent),
	m_mark(new QLabel(QStringLiteral("●"), this)),
	m_text(new QLabel(this)),
	m_stop(new QPushButton(tr("Stop"), this))
{
	setObjectName(IndicatorName);
	setFrameShape(QFrame::StyledPanel);
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(6, 0, 2, 0);
	row->addWidget(m_mark);
	row->addWidget(m_text);
	row->addWidget(m_stop);
	const QColor record = tokens::palette().record;
	m_mark->setStyleSheet(QStringLiteral("color: %1;").arg(record.name()));
	m_mark->setAccessibleName(tr("Recording"));
	QFont bold = m_text->font();
	bold.setBold(true);
	m_text->setFont(bold);
	// The way out, keyboard-reachable and named: a registered command (the agent surface's rule for the header).
	m_stop->setObjectName(QStringLiteral("workflowRecordingStop"));
	m_stop->setProperty("controlCommand", QStringLiteral("workflow.record_stop"));
	m_stop->setAccessibleName(tr("Stop recording the workflow and save it"));
	m_stop->setToolTip(m_stop->accessibleName());
	m_stop->setMinimumHeight(tokens::targetSize());
	connect(m_stop, &QPushButton::clicked, this, [this] {
		runCommand(window(), CommandCall{QStringLiteral("workflow.record_stop"), {}, {}, QString(), false});
	});
	hide();
}

QString WorkflowRecordingIndicator::recordingText(const QString& name, int steps)
{
	return tr("Recording workflow \"%1\" - %n step(s) so far", "", steps).arg(name);
}

void WorkflowRecordingIndicator::setRecording(bool on, const QString& name, int steps)
{
	const bool was = isVisibleTo(parentWidget());
	m_text->setText(on ? recordingText(name, steps) : QString());
	setAccessibleName(on ? m_text->text() : tr("Not recording a workflow"));
	setVisible(on);
	if (on != was)
	{
		a11y::announceStatus(m_text, on ? m_text->text() : tr("Workflow recording stopped"));
	}
}

QString WorkflowRecordingIndicator::text() const
{
	return m_text->text();
}

WorkflowRecordingIndicator* WorkflowRecordingIndicator::inWindow(QWidget* window, bool make)
{
	auto* main = qobject_cast<MainWindow*>(window);
	Shell* shell = main != nullptr ? main->shell() : nullptr;
	if (shell == nullptr || shell->header() == nullptr) { return nullptr; }
	auto* found = shell->header()->strip()->findChild<WorkflowRecordingIndicator*>(IndicatorName, Qt::FindDirectChildrenOnly);
	if (found != nullptr || !make) { return found; }
	auto* made = new WorkflowRecordingIndicator;
	shell->header()->addToStrip(made);
	return made;
}

} // namespace lmms::gui
