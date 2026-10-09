/*
 * ArrangerFileDrop.h - files dropped on the arranger become clips
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
/*! 0.5.0 L04 (Q050-031): a plug-in editor (VST, CLAP, LV2) drags a generated MIDI file or an audio file out of its
 *  window as a `text/uri-list`, with none of the arranger's own drag keys; so do the desktop and file managers.
 *  The arranger takes such a drag on a track's lane, on a clip, on a track's header and on the empty space below
 *  the tracks, and this is the one place that turns it into a clip: through clip.import_file, the command an agent
 *  runs for the same file, so one drop is one undo step and a file that cannot be read changes nothing. What the
 *  command refused is said in the status bar and in a notice, never in a modal box that waits for a click. */
#ifndef LMMS_ARRANGER_FILE_DROP_H
#define LMMS_ARRANGER_FILE_DROP_H

#include <QString>

#include "TimePos.h"
#include "lmms_export.h"

class QMimeData;

namespace lmms
{

class Track;

namespace gui
{

class TrackContainerView;

namespace arrangerdrop
{

//! Whether \a mime carries at least one local file: what a drag from a plug-in editor or the desktop holds.
LMMS_EXPORT bool carriesFile(const QMimeData* mime);

//! The tick under \a x (in the coordinates of the track container, the track head included), snapped to the
//! arranger's grid and never before the left edge of the visible time; 0 in a container whose clips are fixed.
LMMS_EXPORT TimePos tickAtContainerX(const TrackContainerView* view, int x);

/*! Makes a clip of the first local file \a mime carries, at \a tick, on \a track - or on a new track when \a track
 *  is null. Returns whether a clip was made; \a message is what to tell the user either way (empty when the drag held
 *  no local file at all). Any further files in the drag are not imported and the message says so. */
LMMS_EXPORT bool importDroppedFile(const QMimeData* mime, Track* track, TimePos tick, QString* message = nullptr);

//! importDroppedFile(), and the answer shown to the user: the status bar and a notice.
LMMS_EXPORT bool dropFileAndReport(const QMimeData* mime, Track* track, TimePos tick);

} // namespace arrangerdrop

} // namespace gui

} // namespace lmms

#endif // LMMS_ARRANGER_FILE_DROP_H
