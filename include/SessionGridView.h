/*
 * SessionGridView.h - R5.3: the clip-launch grid
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

#ifndef LMMS_GUI_SESSION_GRID_VIEW_H
#define LMMS_GUI_SESSION_GRID_VIEW_H

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QGridLayout;
class QLabel;
class QMenu;
class QPoint;
class QToolButton;

namespace lmms
{
struct ControlResult;
}

namespace lmms::gui
{

/*! The Session View's clip-launch grid (relief plan R5.3, the first new UI surface): track
 *  columns, scene rows, a scene-launch button per row, a stop button per column and Stop All.
 *
 *  REGISTRY-FIRST (M3.2). It holds no session state of its own and calls no model setter:
 *  every button runs a `session.*` command through the ControlRegistry, and every repaint is
 *  drawn from `session.get_state` - the grid, the slots, the scenes and the engine's
 *  per-column reading - so what the grid shows is what an agent reading the socket sees.
 *
 *  KEYBOARD AND ASSISTIVE TECHNOLOGY. Every cell is a focusable button with an accessible
 *  name that says where it is and what it holds ("Track 2, scene 3: bass, playing"). The
 *  arrow keys move between cells, Enter or Space launches the focused one - Live's model.
 *
 *  0.5.0 THEME 14 (DESIGN-DECISIONS.md; Bitwig's launcher is the model). Each column is headed by its track's name
 *  (numbered when it has none); each filled cell and each scene button carries a play mark whose shape says its state
 *  (SessionGridCells.h), and a scene's mark is its row's state - playing, queued, stopped or empty (empty also in
 *  italics). A cell's tooltip and accessible description say what launching it replaces in its track. An audio file
 *  dragged over a filled cell marks it "Replace" (a dashed outline and the text, so not by colour alone) and the
 *  status line says what the drop will replace; the drop is one session.set_slot - one undo step - and afterwards the
 *  status line says what it replaced, beside an Undo Replace button (control.undo, refused when something else was
 *  edited since). Direct replace with Undo, no confirmation: the research's industry default (theme14.md).
 */
class LMMS_EXPORT SessionGridView : public QWidget
{
	Q_OBJECT
public:
	explicit SessionGridView(QWidget* parent = nullptr);

	//! Re-reads session.get_state and redraws; the timer calls it. Public for tests.
	void refresh();

	//! The buttons, for tests and for the agent-surface sweep. nullptr off the grid.
	QToolButton* cellButton(int track, int scene) const;
	QToolButton* sceneButton(int scene) const;
	QToolButton* stopButton(int track) const;
	QToolButton* stopAllButton() const { return m_stopAll; }
	QToolButton* gridButton() const { return m_gridButton; }
	//! 0.5.0 theme 14: column @a track's header, and the button that undoes the last replacing drop.
	QLabel* columnHeader(int track) const;
	QToolButton* undoReplaceButton() const { return m_undoReplace; }
	//! The cell a drag is over (its index, track * scenes + scene), -1 when none: what the drop preview marks.
	int dropPreviewCell() const { return m_dropCell; }
	//! The status line's text (a test's view of it).
	QString statusLine() const;

	/*! What a right-click on @a button offers (a cell, a scene button; nullptr = the grid):
	 *  SessionGridMenus' entries, every one a session.* command. Public so a test can read
	 *  and trigger the menu without a modal exec. */
	void populateMenu(QMenu* menu, QToolButton* button) const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void rebuild(int tracks, int scenes);
	//! The status line: the last refusal while it stands, else the grid's size.
	QString statusText(int tracks, int scenes) const;
	//! After a button's command: keep a refusal in the status line until an action succeeds, then redraw.
	void report(const ControlResult& result);
	void launchCell(int track, int scene);
	//! 0.5.0 L02 (WF-053): an audio file dropped on a cell (from the browser or the desktop) becomes its
	//! clip - session.set_slot - and true when @a event was a drag or drop this handled (SessionGridDrop.cpp).
	//! 0.5.0 theme 14: a drag over a cell previews the drop, and a drop that replaced a clip says so.
	bool dropOnCell(QToolButton* cell, QEvent* event);
	void previewDrop(int cell, const QString& file);
	void finishDrop(int cell, const QString& file);
	//! The drop preview on @a button (cell @a index): "Replace" over a filled cell, the file over an empty one.
	void paintDropPreview(QToolButton* button, int index) const;
	QString dropPreviewText() const;
	void undoReplace();
	//! control.undo_depth's depth and evictions: what the Undo Replace button checks nothing has moved since.
	QJsonObject undoMark() const;
	void stopColumn(int track);
	QToolButton* makeButton(const QString& command);
	void readColumns(const QJsonArray& columns);
	std::vector<QString> cellNames(const QJsonArray& slotList) const;
	void paintHeaders();
	void paintCell(int track, int scene);
	//! What launching cell (@a track, @a scene) replaces in its track, for its tooltip; empty when nothing.
	QString launchNote(int track, int scene) const;
	void paintScenes(const QJsonArray& sceneStates);
	QWidget* neighbour(QToolButton* button, int key) const;
	void showMenu(QToolButton* button, const QPoint& global);
	QJsonObject slotAt(int track, int scene) const;

	QGridLayout* m_grid = nullptr;
	QLabel* m_status = nullptr;
	QToolButton* m_stopAll = nullptr;
	QToolButton* m_gridButton = nullptr;
	QToolButton* m_undoReplace = nullptr;  //!< shown after a drop replaced a clip, until the next action
	QJsonObject m_undoAt;                  //!< undoMark() just after that drop
	QString m_notice;          //!< the last button command's refusal, until one succeeds
	QJsonArray m_slots;        //!< session.get_state's slots, from the last refresh
	QJsonArray m_sceneStates;  //!< and its scenes
	std::vector<QToolButton*> m_cells;
	std::vector<QToolButton*> m_sceneButtons;
	std::vector<QToolButton*> m_stopButtons;
	std::vector<QLabel*> m_headers;    //!< per column: its track's name
	std::vector<QString> m_names;      //!< each cell's name from the last refresh, empty for an empty cell
	int m_dropCell = -1;               //!< the cell a drag is over (track * scenes + scene)
	QString m_dropFile;                //!< and the audio file it carries
	std::vector<int> m_playingScene;  //!< per column, from the last refresh; -1 when idle
	std::vector<QString> m_phases;    //!< per column: launching, playing, stopping or empty
	int m_tracks = -1;
	int m_scenes = -1;
	QTimer m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_GRID_VIEW_H
