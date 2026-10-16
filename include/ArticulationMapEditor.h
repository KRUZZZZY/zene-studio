/*
 * ArticulationMapEditor.h - Track > Articulations: create, rename, reorder and remove a track's articulations
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
#ifndef LMMS_GUI_ARTICULATION_MAP_EDITOR_H
#define LMMS_GUI_ARTICULATION_MAP_EDITOR_H

#include <vector>

#include <QDialog>
#include <QString>

#include "ArticulationMap.h"
#include "lmms_export.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace lmms::gui
{

/*! 0.5.0 L07 (Q050-015, ED-121): the editor of an instrument track's articulation map, from the track's menu
 *  and from the note editor's Articulation menu. One row per articulation, in the order a note's
 *  'articulation' counts them (1-based): its name, and what it sends before a note that asks for it - a
 *  keyswitch key, a program change, a control change (controller and value), each blank for "not that".
 *  Every edit is one track.articulations_set run through the registry (the socket's and Lua's path), so it is
 *  one undo step and the map it leaves is the one the command leaves; moving or removing an entry passes
 *  'from' so the notes follow their articulation. The table shows what track.articulations answers. */
class LMMS_EXPORT ArticulationMapEditor : public QDialog
{
	Q_OBJECT
public:
	//! The columns after the row number and the name, as setNumber() takes them.
	enum class Field { Keyswitch = 2, Program = 3, Controller = 4, Value = 5 };

	explicit ArticulationMapEditor(const QString& trackId, QWidget* parent = nullptr);

	//! The editor of the track @a trackId under @a parent: the one already open, raised, else a new one shown.
	static ArticulationMapEditor* open(const QString& trackId, QWidget* parent);

	//! Reads the track's map again (after an undo, or an agent's edit).
	void refresh();
	int rowCount() const;
	const std::vector<Articulation>& entries() const { return m_entries; }
	//! The reason the last edit was refused; empty after one that went through.
	QString message() const { return m_message; }

	//! Appends a new, empty articulation named @a name (blank: "Articulation N"); false when refused.
	bool addEntry(const QString& name = QString());
	//! A copy of @a row, right after it; its name gets " copy" (and a number when that is taken too).
	bool duplicateEntry(int row);
	bool renameEntry(int row, const QString& name);
	//! Moves @a row by @a delta places (-1 up, +1 down); the notes follow.
	bool moveEntry(int row, int delta);
	//! Removes @a row; the notes that asked for it ask for none.
	bool removeEntry(int row);
	//! Sets one of the numbers of @a row to @a value (0..127), or -1 for "not that".
	bool setNumber(int row, Field field, int value);

protected:
	void changeEvent(QEvent* event) override;

private:
	//! Writes @a entries through track.articulations_set; @a from is where each one stood (0: new). Selects @a selectRow.
	bool commit(const std::vector<Articulation>& entries, const std::vector<int>& from, int selectRow);
	//! The table from the track's map, with @a selectRow (-1: the row that was) current.
	void reload(int selectRow);
	void fillRows(int current);
	bool fail(const QString& why);
	QString freeName(const QString& wanted) const;
	bool nameTaken(const QString& name, int exceptRow) const;
	void cellEdited(int row, int column);
	void followRow();

	QString m_track;
	std::vector<Articulation> m_entries;
	QString m_message;
	bool m_refreshing = false;
	QLabel* m_status = nullptr;
	QTableWidget* m_table = nullptr;
	QPushButton* m_add = nullptr;
	QPushButton* m_duplicate = nullptr;
	QPushButton* m_up = nullptr;
	QPushButton* m_down = nullptr;
	QPushButton* m_remove = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_ARTICULATION_MAP_EDITOR_H
