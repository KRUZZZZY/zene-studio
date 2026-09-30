/*
 * UndoHistoryPanel.h - Edit > Undo History: the undo stack and the command record, from the interface
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

#ifndef LMMS_GUI_UNDO_HISTORY_PANEL_H
#define LMMS_GUI_UNDO_HISTORY_PANEL_H

#include <QDialog>

#include "lmms_export.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace lmms::gui
{

/*! Edit > Undo History (KNOWN-LIMITATIONS "there is no undo-history UI"): the engine's undo stack
 *  as control.undo_depth reports it - depth, redo depth, the two caps, the bytes kept and the
 *  steps a bound has evicted - with Undo and Redo buttons that run control.undo / control.redo,
 *  and the record control.transactions keeps, newest first. That record lists the edits made
 *  through the command registry (agents, scripts, the palette, every registry-first menu item);
 *  an edit made directly in an editor is undone by Undo but is not in the list, and the panel
 *  says so rather than let the list read as the whole history. */
class LMMS_EXPORT UndoHistoryPanel : public QDialog
{
public:
	explicit UndoHistoryPanel(QWidget* parent = nullptr);

	//! Re-reads both commands. Called on show, after Undo/Redo, and by the refresh button.
	void refresh();
	int rowCount() const;
	QString summary() const;

private:
	QLabel* m_summary = nullptr;
	QListWidget* m_list = nullptr;
	QPushButton* m_undo = nullptr;
	QPushButton* m_redo = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_UNDO_HISTORY_PANEL_H
