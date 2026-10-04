/*
 * TaskIndicator.h - the status bar's Tasks indicator and its list of background jobs
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

#ifndef LMMS_GUI_TASK_INDICATOR_H
#define LMMS_GUI_TASK_INDICATOR_H

#include <vector>

#include <QFrame>
#include <QPointer>
#include <QToolButton>

#include "TaskCenter.h"
#include "lmms_export.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace lmms::gui
{

/*! 0.5.0 UX decision (theme 24): the background jobs (TaskCenter.h) in a list that never blocks the window - a Qt
 *  popup under the Tasks indicator, closed by Escape or a click elsewhere. Each row is the job's title, its state in
 *  words (colour is never the only cue), a status line, a progress bar (a busy bar when the job cannot say how far it
 *  is) and Cancel where the job supports it. Cancel and Clear finished run task.cancel and task.clear_finished through
 *  the registry, as an agent would. While it is open the running jobs' progress is re-read four times a second. */
class LMMS_EXPORT TaskList : public QFrame
{
	Q_OBJECT
public:
	explicit TaskList(QWidget* parent = nullptr);

	//! Show it with its bottom-left corner at @a anchor's top-left (above a status bar), or under it when there is no
	//! room above.
	void popUpAt(QWidget* anchor);
	int rowCount() const;
	//! The Cancel button of row @a row, or nullptr when that job cannot be cancelled (for the test).
	QPushButton* cancelButton(int row) const;

public slots:
	//! Show the list as it is now: the rows are rebuilt when a job began, ended or changed its status line, and only
	//! their progress bars move otherwise (so a focused Cancel keeps its focus).
	void refresh();

protected:
	void mousePressEvent(QMouseEvent* event) override;

private:
	struct Row
	{
		int id = 0;
		TaskState state = TaskState::Running;
		QString status;
		QProgressBar* bar = nullptr;
		QPushButton* cancel = nullptr;
	};

	bool updateInPlace(const std::vector<TaskInfo>& newestFirst);
	void rebuild(const std::vector<TaskInfo>& newestFirst);
	QWidget* makeRow(const TaskInfo& task, Row* row);
	void clearRows();

	QVBoxLayout* m_rows;
	QLabel* m_empty;
	QPushButton* m_clear;
	QTimer* m_poll;
	QPointer<QWidget> m_anchor;
	std::vector<Row> m_live;
};

/*! The status bar's Tasks indicator: "TASKS" while nothing runs, "TASKS 2" with a busy mark and the running jobs'
 *  combined progress along its bottom edge while jobs run. A click or Space opens the list (task.show does the
 *  same over the socket). When a job ends, a screen reader hears it ("Render mix.wav: done"). Sized from the design
 *  tokens: the target grows in accessible mode and with the one global zoom. */
class LMMS_EXPORT TaskIndicator : public QToolButton
{
	Q_OBJECT
public:
	explicit TaskIndicator(QWidget* parent = nullptr);

	QSize sizeHint() const override;
	//! Show (or hide) the list; @a shown absent toggles it. Whether it is shown now.
	bool setListShown(int shown);  // -1 toggles, 0 hides, 1 shows
	TaskList* list() const { return m_list; }
	//! The words the indicator shows ("TASKS", "TASKS 2").
	QString label() const;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	void tasksChanged();
	void announceEnded();
	double combinedProgress() const;
	QColor fillColour() const;

	QPointer<TaskList> m_list;
	int m_active = 0;
	std::vector<int> m_announced;  //!< ended task ids already spoken
};

} // namespace lmms::gui

#endif // LMMS_GUI_TASK_INDICATOR_H
