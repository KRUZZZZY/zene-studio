/*
 * ItemList.h - the editable item list every editing canvas can show beside itself
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

#ifndef LMMS_GUI_ITEM_LIST_H
#define LMMS_GUI_ITEM_LIST_H

#include <functional>

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

#include "lmms_export.h"

class QLabel;
class QTableView;
class QTimer;

/*! 0.5.0 UX decision, theme 10, as the research adjusted it: the accessible item list is a SYNCHRONISED, FULLY
 *  EDITABLE equivalent of a canvas, not a read-only companion. One panel serves every canvas - the note area (with
 *  the expression lane's value as a column), the controller lane, the XY pad, the comp strip and the arranger's
 *  clips. It is a table a screen reader reads cell by cell (Qt's table interface on every bridge):
 *  - selection follows the canvas both ways where the canvas has one (notes, clips);
 *  - arrows move, F2 or typing edits a cell - positions and lengths in the time format ("5.2.1", "768t", "0:08.5"),
 *    pitches as names or numbers ("C5", "60"), values as numbers; + and - step the current cell (a grid step for a
 *    position or a length);
 *  - Insert adds an item after the current one, Delete removes the selected ones, Escape closes the list.
 *  Every edit is the canvas's own registered command - note.edit, note.add, note.remove_many, note.select,
 *  note.expression_set, clip.controllers_set, rack.macro_set, comp.select, clip.move, clip.resize, clip.add,
 *  clip.delete, window.select - so each is one undo step and what an agent sends; a refusal is shown on the list's
 *  status line and announced. The list re-reads when the canvas changes, whoever changed it.
 *  window.item_list opens it (Ctrl+Shift+L on each canvas). */
namespace lmms::gui
{

class AutomationEditor;
class CompSwipeStrip;
class NoteEditor;
class ShellButton;
class XyPadDialog;

//! One column of a list.
struct ItemColumn
{
	enum class Kind
	{
		Text,     //!< read-only words (a track's name)
		Number,   //!< an integer
		Position, //!< ticks, shown and typed in the time format
		Length,   //!< ticks, a length in the time format
		Key,      //!< 0..127, shown "C5"
		Percent   //!< 0..100, shown "40 %"
	};
	QString id;
	QString title;
	Kind kind = Kind::Number;
	int minimum = 0;
	int maximum = 0;
	bool editable = false;
};

//! What a canvas exposes to the list: its items, their values and the commands that change them.
class LMMS_EXPORT ItemListSource : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;

	virtual QString title() const = 0;
	virtual QList<ItemColumn> columns() const = 0;
	virtual int rowCount() const = 0;
	//! A row's stable id (a note-<n>, a clip-<n>, a tick): the list keeps its place by it across a re-read.
	virtual QString rowId(int row) const = 0;
	virtual int value(int row, int column) const = 0;
	//! The cell as shown and read: by the column's kind, unless the source says otherwise.
	virtual QString text(int row, int column) const;
	//! Write @a value through the canvas's command; false (lastError() says why) when it was refused.
	virtual bool setValue(int row, int column, int value) = 0;
	//! What + and - step a column's value by.
	virtual int step(int column) const;
	virtual bool canInsert() const { return false; }
	//! Add an item after @a row (-1: none chosen).
	virtual bool insertAfter(int row);
	virtual bool canRemove() const { return false; }
	virtual bool remove(const QList<int>& rows);
	//! Whether the canvas has a selection the list follows (and sets).
	virtual bool tracksSelection() const { return false; }
	virtual QList<int> selectedRows() const { return {}; }
	virtual void select(const QList<int>& rows);
	QString lastError() const { return m_error; }
	//! {title, columns: [{id, title, editable}], rows: [{id, <column id>: text}], count} - window.item_list's answer.
	QJsonObject describe(int maxRows) const;

	//! @a text parsed for column @a kind into @a value; false for text that is not one.
	static bool parse(ItemColumn::Kind kind, const QString& text, int* value);

signals:
	void changed();

protected:
	//! Run a registered command; on a refusal keep its message (lastError) and announce it. True when it ran.
	bool run(const QString& command, const QJsonObject& args);
	void refuse(const QString& why);

private:
	QString m_error;
};

//! The panel: a table over a source, its Insert / Delete / Close buttons and a status line. A tool window owned by
//! the canvas it lists, one per canvas and kind; closing it deletes it and its source.
class LMMS_EXPORT ItemListPanel : public QWidget
{
	Q_OBJECT
public:
	ItemListPanel(ItemListSource* source, QWidget* surface, const QString& kind);

	//! The open list of @a kind on @a surface, or nullptr.
	static ItemListPanel* find(QWidget* surface, const QString& kind);
	//! The list of @a kind on @a surface, made with @a make (given the panel to parent the source to) when none is;
	//! nullptr when @a surface is nullptr.
	static ItemListPanel* open(QWidget* surface, const QString& kind, const std::function<ItemListSource*(QObject*)>& make);

	ItemListSource* source() const { return m_source; }
	QTableView* table() const { return m_table; }
	QString statusText() const;

	//! The list's actions, as the keys and buttons run them; false when nothing was done.
	bool insertItem();
	bool removeItems();
	bool stepCurrent(int direction);
	//! Re-read the source now (a change is otherwise re-read on the next turn of the event loop).
	void reload();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void selectionEdited();
	void showResult(bool ok);
	QList<int> chosenRows() const;
	void restore(const QString& currentId, int currentColumn, const QStringList& selectedIds);
	int rowOf(const QString& id) const;  //!< the row with @a id, or -1

	ItemListSource* m_source;
	QTableView* m_table;
	class ItemListModel* m_model;
	QLabel* m_status;
	QTimer* m_reload;
	bool m_syncing = false;
};

//! The sources of the canvases (ItemListSources*.cpp, ItemListAutomationSource.cpp).
namespace itemlists
{
LMMS_EXPORT ItemListSource* notes(NoteEditor* editor, QObject* parent);
LMMS_EXPORT ItemListSource* controllerPoints(NoteEditor* editor, QObject* parent);
LMMS_EXPORT ItemListSource* xyAxes(XyPadDialog* pad, QObject* parent);
LMMS_EXPORT ItemListSource* compSegments(CompSwipeStrip* strip, QObject* parent);
LMMS_EXPORT ItemListSource* clips(QObject* parent);

/*! The List button every canvas with a list carries on its toolbar (ItemListPanel.cpp): the shell's button with the
 *  list icon, declaring window.item_list (controlCommand) and @a surface (itemListSurface). A press puts the keyboard
 *  focus on @a canvas - window.item_list lists the canvas holding it, as Ctrl+Shift+L there does - and runs
 *  window.item_list on @a surface, which opens the list and, pressed again, closes it. A refusal is announced. */
LMMS_EXPORT ShellButton* listButton(const QString& surface, QWidget* canvas, QWidget* parent);

//! ED-194: the open automation editor's points (ItemListAutomationSource.cpp).
LMMS_EXPORT ItemListSource* automationPoints(AutomationEditor* editor, QObject* parent);
} // namespace itemlists

} // namespace lmms::gui

namespace lmms
{
class ControlRegistry;
//! window.item_list and view.announcements (ControlCommandsAccessibleEditing.cpp); registerWorkspaceCommands calls it.
LMMS_EXPORT void registerAccessibleEditingCommands(ControlRegistry& registry);
} // namespace lmms

#endif // LMMS_GUI_ITEM_LIST_H
