/*
 * SessionGridMenus.h - the clip launcher's context menus: cells, scenes and the grid
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

#ifndef LMMS_GUI_SESSION_GRID_MENUS_H
#define LMMS_GUI_SESSION_GRID_MENUS_H

#include <QJsonArray>
#include <QJsonObject>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

/*! The launcher's editing surface (0.5.0 W1, backlog WF-054/058/059/062): what a right-click
 *  offers on a cell, a scene and the grid. Every entry is a registered session.* command run
 *  through CommandForm - a form for the ones that take values, at once for a pick from a list -
 *  so the menu and an agent on the socket edit the launcher the same way (SPEC A11). */
namespace SessionGridMenus
{

//! A cell: edit it (type, clip, name, playback, follow actions), its launch mode and launch
//! quantisation, arm or disarm its follow actions, record audio into it (WF-055: an empty cell, session.record_slot), clear it. @a slot is the cell's
//! session.get_state entry, or empty for an empty cell.
LMMS_EXPORT void populateCell(QMenu* menu, int track, int scene, const QJsonObject& slot);

//! A scene: edit its name, tempo and metre, switch an override off, launch it.
LMMS_EXPORT void populateScene(QMenu* menu, int scene, const QJsonObject& sceneState);

/*! 0.5.0 L02 (WF-064): Sub-scenes - Make a Sub-scene From the Selection (session.subscene_create on @a selection,
 *  SessionGridView::selection(); disabled while it is empty), and for each of @a subScenes (session.get_state's) that
 *  is in @a scene (-1: none) Launch, Rename... and Delete it. */
LMMS_EXPORT void populateSubScenes(QMenu* menu, int scene, const QJsonArray& subScenes, const QJsonObject& selection);

//! The grid: its size, the global launch quantisation, follow actions on or off.
LMMS_EXPORT void populateGrid(QMenu* menu, int tracks, int scenes);

} // namespace SessionGridMenus

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_GRID_MENUS_H
