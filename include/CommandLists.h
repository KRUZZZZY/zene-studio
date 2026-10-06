/*
 * CommandLists.h - the list surfaces: revisions, missing files, groups, markers, zones, plug-ins
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

#ifndef LMMS_GUI_COMMAND_LISTS_H
#define LMMS_GUI_COMMAND_LISTS_H

#include "CommandListDialog.h"

namespace lmms::gui
{

/*! Every "list of things, act on one" surface the 0.5.0 W1 work adds, each declared as a
 *  ListSpec over registered commands (CommandListDialog runs them). A surface is the list
 *  command, the columns worth reading, and the row actions - nothing else to write. */
namespace CommandLists
{

//! WF-031: the project's revisions (journal and git), restore one, compare one with now.
LMMS_EXPORT ListSpec revisions();
//! WF-035: the project's file references, relink a missing one, collect media.
LMMS_EXPORT ListSpec missingFiles(const QString& project);
//! MX-069: VCA groups - rename, remove, create.
LMMS_EXPORT ListSpec vcaGroups();
//! ED-147: a sample clip's warp markers - move, remove, add.
LMMS_EXPORT ListSpec warpMarkers(const QString& clip);
//! MX-107: a rack channel's key/velocity zones - remove, add.
LMMS_EXPORT ListSpec rackZones(const QString& channel);
//! MX-168: every device in the project with its track and hosting state - bypass, enable,
//! open its editor, restart its host, remove it.
LMMS_EXPORT ListSpec pluginsInProject();
//! MX-164: every device this build can load, with its format and whether it loads - favourite,
//! hide, file in a folder; rescan, the folders scanned (MX-163), the module scan's records.
LMMS_EXPORT ListSpec pluginManager();
//! MX-164: the plug-in modules the scan cache remembers, their status - quarantine one, release it.
LMMS_EXPORT ListSpec pluginModules();
//! ED-171: a track's take lanes - remove, add, comp a range.
LMMS_EXPORT ListSpec takeLanes(const QString& track);
//! ED-141: the groove pool - rename, remove.
LMMS_EXPORT ListSpec grooves();
//! WF-123: safe start - what was skipped after a crash, acknowledge, skip next time or not.
LMMS_EXPORT ListSpec safeStart();

//! WF-110/111: every menu action's keys - set (captured), reset, export and import a set.
LMMS_EXPORT ListSpec shortcuts();
//! WF-114 / design theme 19: the saved workflows (once "macros"; the function keeps that name) in the
//! library in use - run, remove, save a new one.
LMMS_EXPORT ListSpec macros();
//! WF-124: the controls a MIDI controller drives - soft takeover, feedback - and the saved
//! mapping templates.
LMMS_EXPORT ListSpec controllerMappings();
LMMS_EXPORT ListSpec mappingTemplates();
//! ED-048: the project's markers - jump, rename/move/recolour, remove, add at the play position.
LMMS_EXPORT ListSpec markers();
//! 0.5.0 L04: the arrangement's clips (ED-036) and the song's sections (ED-047).
LMMS_EXPORT ListSpec clips();
LMMS_EXPORT ListSpec sections();
//! L07 MIDI event list: every note of a clip as a row - edit its velocity, position, length, probability.
LMMS_EXPORT ListSpec noteEvents(const QString& clip);
//! L12 (MX-170): the project's mixer snapshots - recall all of one or only its faders, remove, store a new one.
LMMS_EXPORT ListSpec mixerSnapshots();
//! L12 (WF-044): the tracks and the clips locked against editing - unlock one.
LMMS_EXPORT ListSpec lockedTracks();
LMMS_EXPORT ListSpec lockedClips();
//! L11 (WF-025): a device's presets (user and factory) - load, rename, remove; save the device's state as a new one.
//! @a device is the device's address ({target, plugin}).
LMMS_EXPORT ListSpec devicePresets(const QJsonObject& device);
//! L12 (WF-040): the render queue - each job's state and report; cancel, report; queue a render, convert files, clear.
LMMS_EXPORT ListSpec renderQueue();
//! L12 (WF-046): every background task (renders, the library scan, stem separation, mastering) - cancel one.
LMMS_EXPORT ListSpec backgroundTasks();
//! L12 (WF-045): the project's subprojects - render one again, open its file; add one.
LMMS_EXPORT ListSpec subprojects();
//! L12 (WF-032): the project's alternatives - switch to one, remove one; keep the session as a new one.
LMMS_EXPORT ListSpec alternatives();

//! 0.5.0 W3 L13/L14 (CommandListsCustom.cpp): the theme's colour roles - change one, put the palette's back,
//! reset, export, import (WF-117).
LMMS_EXPORT ListSpec themeColors();
//! WF-097: every track in one list - mute, solo, show or hide, rename, colour, icon (WF-120), move, remove -
//! with its channel's processing load (WF-104).
LMMS_EXPORT ListSpec trackManager();
//! WF-098: the memory locations - go to one, remove it, save the current place.
LMMS_EXPORT ListSpec memoryLocations();
//! WF-106: the saved selections - select one again, remove it, save the current selection.
LMMS_EXPORT ListSpec savedSelections();
//! WF-122: every setting that travels, with its value - change one, search, back up, restore; and the
//! settings presets - apply, remove, save the current settings as one.
LMMS_EXPORT ListSpec settingsCatalogue();
LMMS_EXPORT ListSpec settingsPresets();

//! 0.5.0 F2 (InstrumentCommandMenus.cpp), each over one instrument track @a track (trk-<n>):
//! MX-114: its instrument's output buses - route one to a mixer channel, un-route it, a channel for each.
LMMS_EXPORT ListSpec outputBuses(const QString& track);
//! MX-114: its MIDI parts (a key/velocity zone sent on one MIDI channel) - edit one in place, remove one, add one.
LMMS_EXPORT ListSpec midiParts(const QString& track);
//! MX-112: its layer rack's key/velocity zones (which layers a note plays on) - remove one, add one. The layers
//! themselves are LayerRackPanel's.
LMMS_EXPORT ListSpec layerZones(const QString& track);

} // namespace CommandLists

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_LISTS_H
