/*
 * LivecodePanel.h - Edit > Live Code: schedule Lua scripts from the interface
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

#ifndef LMMS_GUI_LIVECODE_PANEL_H
#define LMMS_GUI_LIVECODE_PANEL_H

#include <QDialog>

#include "lmms_export.h"

class QComboBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace lmms::gui
{

/*! Edit > Live Code (Q050-154): the live-code panel.
 *
 *  The `livecode.*` group (board card #708) was drivable only from the socket: a
 *  musician could schedule a Lua script, cancel it and read its counters, but only
 *  an agent could do it. This panel is the interface half, and it is the SAME
 *  implementation: every control invokes the registered command - Schedule runs
 *  `livecode.schedule`, Cancel `livecode.unschedule`, Load `livecode.get_source`,
 *  Undo `control.undo` - never ScriptClock directly (SPEC A11).
 *
 *  What it shows and what it deliberately does not:
 *   - the source editor and the id/hook/budget a schedule is made with;
 *   - a compile refusal from `livecode.schedule` IN the panel, with the currently
 *     scheduled definition left active (the command refuses before the registry moves);
 *   - every schedule's id, hook, budget, source bytes, fire/error/budget-exceeded
 *     counters, last failure and last fire position, from `livecode.get_state`;
 *   - the bounds a live coder has to know (a fresh Lua state per fire, the source
 *     and schedule-count caps, the poll and the coalescing rule, and the per-run Lua
 *     memory budget), read from ScriptClock and ScriptEngine so the help text cannot
 *     drift from the implementation.
 *
 *  Runtime schedules are process-local (ScriptClock's registry); this panel saves
 *  nothing into the project, and says so.
 */
class LMMS_EXPORT LivecodePanel : public QDialog
{
	Q_OBJECT
public:
	explicit LivecodePanel(QWidget* parent = nullptr);

	//! Re-read the registry through livecode.get_state and fill the schedule table.
	void refresh();

	//! The source editor's text (the buffer that Schedule sends).
	QString source() const;
	void setSource(const QString& source);
	//! The id Schedule sends; empty means "let the command choose the next lc-<n>".
	QString scheduleId() const;
	void setScheduleId(const QString& id);
	void setHook(const QString& hook);
	void setBudget(int budget);

	//! Schedule the editor's text. Empty on success, else the command's refusal, which is
	//! also shown in the panel - the previously scheduled definition stays active.
	QString schedule();
	//! Cancel the selected schedule (or the id in the box). Empty on success, else the refusal.
	QString unschedule();
	//! Load one schedule's captured source (livecode.get_source) into the editor.
	QString loadSource(const QString& id);
	//! One control.undo; when the loaded id still has a schedule, its definition is reloaded.
	QString undo();
	//! Replace the editor buffer from a file (editor text, not a schedule).
	QString openFile(const QString& path);

	int scheduleCount() const;
	int rowCount() const;
	QString statusText() const;
	QString helpText() const;

private:
	QString runAndReport(const char* command, const QJsonObject& args, bool* ok = nullptr);
	QString targetId() const;
	void fillSlots();

	QPlainTextEdit* m_source = nullptr;
	QLineEdit* m_id = nullptr;
	QComboBox* m_hook = nullptr;
	QSpinBox* m_budget = nullptr;
	QTableWidget* m_table = nullptr;
	QLabel* m_summary = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_help = nullptr;
	QPushButton* m_cancelButton = nullptr;
	QPushButton* m_loadButton = nullptr;
	QPushButton* m_undoButton = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_LIVECODE_PANEL_H
