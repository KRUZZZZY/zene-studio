/*
 * SessionGridSelect.cpp - the clip-launch grid's cell selection, which a sub-scene is made from (WF-064)
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

/*! 0.5.0 L02 (WF-064): SessionGridView's selection half. Ctrl+click (or Ctrl+Space on a focused cell) selects or
 *  deselects a cell without launching it; a selected cell has a dotted outline and says "selected", so not by colour
 *  alone. The selection is the grid's own view state - nothing in the project - and becomes a sub-scene only through
 *  session.subscene_create (Sub-scenes > Make a Sub-scene From the Selection). */

#include <algorithm>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolButton>

#include "SessionGridView.h"

namespace lmms::gui
{

bool SessionGridView::isCellSelected(int track, int scene) const
{
	const int index = track * m_scenes + scene;
	return cellButton(track, scene) != nullptr && std::find(m_selected.begin(), m_selected.end(), index) != m_selected.end();
}


void SessionGridView::toggleCellSelection(int track, int scene)
{
	if (cellButton(track, scene) == nullptr) { return; }
	const int index = track * m_scenes + scene;
	const auto found = std::find(m_selected.begin(), m_selected.end(), index);
	if (found == m_selected.end()) { m_selected.push_back(index); }
	else { m_selected.erase(found); }
	refresh();
}


QJsonObject SessionGridView::selection() const
{
	if (m_selected.empty() || m_scenes <= 0) { return {}; }
	const int scene = m_selected.front() % m_scenes;
	std::vector<int> columns;
	for (const int index : m_selected)
	{
		if (index % m_scenes != scene) { return {}; }  // a sub-scene is one scene's cells
		columns.push_back(index / m_scenes);
	}
	std::sort(columns.begin(), columns.end());
	QJsonArray tracks;
	for (const int column : columns) { tracks.append(column); }
	return {{QStringLiteral("scene"), scene}, {QStringLiteral("tracks"), tracks}};
}


bool SessionGridView::selectOnCell(QToolButton* button, QEvent* event)
{
	const auto cell = std::find(m_cells.begin(), m_cells.end(), button);
	if (cell == m_cells.end() || m_scenes <= 0) { return false; }
	bool toggles = false;
	if (event->type() == QEvent::MouseButtonPress)
	{
		const auto* mouse = static_cast<QMouseEvent*>(event);
		toggles = mouse->button() == Qt::LeftButton && mouse->modifiers().testFlag(Qt::ControlModifier);
	}
	else if (event->type() == QEvent::KeyPress)
	{
		const auto* key = static_cast<QKeyEvent*>(event);
		toggles = key->key() == Qt::Key_Space && key->modifiers().testFlag(Qt::ControlModifier);
	}
	if (!toggles) { return false; }
	const int index = static_cast<int>(cell - m_cells.begin());
	toggleCellSelection(index / m_scenes, index % m_scenes);
	return true;
}

} // namespace lmms::gui
