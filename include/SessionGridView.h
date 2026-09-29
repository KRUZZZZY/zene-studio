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

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QGridLayout;
class QLabel;
class QToolButton;

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

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	void rebuild(int tracks, int scenes);
	void launchCell(int track, int scene);
	void stopColumn(int track);
	QToolButton* makeButton(const QString& command);

	QGridLayout* m_grid = nullptr;
	QLabel* m_status = nullptr;
	QToolButton* m_stopAll = nullptr;
	std::vector<QToolButton*> m_cells;
	std::vector<QToolButton*> m_sceneButtons;
	std::vector<QToolButton*> m_stopButtons;
	std::vector<int> m_playingScene;  //!< per column, from the last refresh; -1 when idle
	int m_tracks = -1;
	int m_scenes = -1;
	QTimer m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_GRID_VIEW_H
