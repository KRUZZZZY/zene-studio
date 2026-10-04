/*
 * WorkflowRecordingIndicator.h - the header's "recording a workflow" indicator, with Stop
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

#ifndef LMMS_GUI_WORKFLOW_RECORDING_INDICATOR_H
#define LMMS_GUI_WORKFLOW_RECORDING_INDICATOR_H

#include <QFrame>

#include "lmms_export.h"

class QLabel;
class QPushButton;

namespace lmms::gui
{

/*! Design theme 19: while a workflow is being recorded the window says so, unmistakably, and offers the way
 *  out - a record mark and "Recording workflow "name" - N steps so far" (in words, so colour is never the only
 *  cue), with a Stop button that runs workflow.record_stop (stop and save). It sits in the header's strip
 *  (ShellHeader::addToStrip, the strip the song editor's controls are added to), shown while recording and
 *  hidden otherwise; screen readers hear when recording starts and stops. */
class LMMS_EXPORT WorkflowRecordingIndicator : public QFrame
{
	Q_OBJECT
public:
	explicit WorkflowRecordingIndicator(QWidget* parent = nullptr);

	//! Show the recording of @a name with @a steps so far, or (@a on false) hide.
	void setRecording(bool on, const QString& name, int steps);
	QString text() const;
	QPushButton* stopButton() const { return m_stop; }

	//! The indicator in @a window's header - made there when @a make and there is none; nullptr without a shell.
	static WorkflowRecordingIndicator* inWindow(QWidget* window, bool make);
	//! The text the indicator and the Workflows menu's status line show.
	static QString recordingText(const QString& name, int steps);

private:
	QLabel* m_mark = nullptr;
	QLabel* m_text = nullptr;
	QPushButton* m_stop = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_WORKFLOW_RECORDING_INDICATOR_H
