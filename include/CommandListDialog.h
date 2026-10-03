/*
 * CommandListDialog.h - a list a command answers, with row actions that run other commands
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

#ifndef LMMS_GUI_COMMAND_LIST_DIALOG_H
#define LMMS_GUI_COMMAND_LIST_DIALOG_H

#include <QDialog>
#include <QHash>
#include <QJsonArray>
#include <QList>

#include "CommandForm.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace lmms::gui
{

//! An action on the selected row: runs `id` with `fixed` plus, for each `fromRow` entry, the
//! argument named by its key set to the row's field named by its value.
struct RowAction
{
	QString label;
	QString id;
	QHash<QString, QString> fromRow;  //!< argument key -> row field
	QJsonObject fixed;
	bool ask = false;      //!< through the command's form (the row's fields fixed)
	bool confirm = false;  //!< ask "are you sure" first (on a screen; never headless)
	bool showResult = false;  //!< show the reply (a comparison, an analysis)
};

//! A button under the list that is not about one row (Add, Collect, Refresh...).
struct ListExtra
{
	QString label;
	CommandCall call;
};

/*! What a CommandListDialog shows: `listId` run with `listArgs`, the array `arrayKey` of its
 *  result as rows, the row fields `columns` as columns (every field of the first row when
 *  empty), and `summaryKeys` - top-level result fields - as a line above the table.
 *  With `nestedKeys` the rows are one level down: under each item, every key's array (or single
 *  object), each row carrying its parent's `parentFields` (as `parent_<field>`), for a list
 *  grouped by owner - every chain's devices and its instrument, every track's take lanes. */
struct ListSpec
{
	QString title;
	QString listId;
	QJsonObject listArgs;
	QString arrayKey;
	QStringList nestedKeys;
	QStringList parentFields;
	QStringList columns;
	QStringList summaryKeys;
	QList<RowAction> actions;
	QList<ListExtra> extras;
};

/*! The one dialog behind every "list of things, act on one" surface - revisions, missing
 *  files, VCA groups, warp markers, rack zones, the plug-ins in a project. It holds nothing of
 *  its own: the rows are whatever the list command answers, re-read after every action, and
 *  every action is a registered command (SPEC A11), so a feature that has a list command and an
 *  action command has its interface by declaring a ListSpec. */
class LMMS_EXPORT CommandListDialog : public QDialog
{
	Q_OBJECT
public:
	explicit CommandListDialog(const ListSpec& spec, QWidget* parent = nullptr);

	//! Re-run the list command and redraw.
	void refresh();
	int rowCount() const { return static_cast<int>(m_rows.size()); }
	QJsonObject row(int index) const { return m_rows.at(index).toObject(); }
	//! The last list result (the summary's source).
	const QJsonObject& listResult() const { return m_listed; }
	//! Run action @a action on row @a index. A form action answers Busy (the form is open).
	ControlResult runAction(int index, int action);
	ControlResult runExtra(int index);
	//! The line under the table: the last outcome.
	QString statusText() const;

private:
	//! Show m_rows; the row @a kept (the selected one before a refresh) stays selected where it still is.
	void fill(const QJsonValue& kept);
	void syncButtons();
	void report(const QString& label, const ControlResult& result);
	ControlResult dispatch(const QString& label, const CommandCall& call);

	ListSpec m_spec;
	QJsonObject m_listed;
	QJsonArray m_rows;
	QLabel* m_summary = nullptr;
	QTableWidget* m_table = nullptr;
	QLabel* m_status = nullptr;
	QList<QPushButton*> m_actionButtons;
};

//! The rows @a spec makes of the list result @a listed (the dialog's reading; a test's too).
LMMS_EXPORT QJsonArray listRows(const ListSpec& spec, const QJsonObject& listed);

//! Show @a spec's dialog non-modally (deleted on close) and return it.
LMMS_EXPORT CommandListDialog* showCommandList(QWidget* parent, const ListSpec& spec);

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_LIST_DIALOG_H
