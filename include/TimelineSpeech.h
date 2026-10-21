/*
 * TimelineSpeech.h - spoken timeline position, selection, and markers (Q050-080)
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

#ifndef LMMS_TIMELINE_SPEECH_H
#define LMMS_TIMELINE_SPEECH_H

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

class Clip;
class MidiClip;
class Note;
class Track;
struct Marker;

//! Q050-080: what the timeline says out loud. Every message carries the item
//! and its bar/beat position (musicaltime::positionText, so the configured
//! time format in transport.time_format is what is heard). Said Polite, so
//! view.announcements gates it: All hears it, Errors and Off do not.
namespace speech
{

//! The position as the musician configured it (Bars|Beats, ticks or time).
LMMS_EXPORT QString positionSpeech(tick_t tick);
//! "Riff, on Bass, 5.1.0, 2.0.0 long" - a clip with its bar/beat span.
LMMS_EXPORT QString clipSpeech(const Clip* clip);
//! "Marker Drop at 13.1.0" (or "Marker at 13.1.0" with no name).
LMMS_EXPORT QString markerSpeech(const Marker& marker);
//! A crossed marker while playing: "Marker Drop, 13.1.0".
LMMS_EXPORT QString markerCrossingSpeech(const Marker& marker);
//! "C4, 1.1.0, 1.0.0 long, velocity 100" - a note with its bar/beat span.
LMMS_EXPORT QString noteSpeech(const Note& note);
//! Say the note.select choice: the count and the earliest selected note.
LMMS_EXPORT void announceNoteSelection(const MidiClip* clip, int selectedCount);
//! Say the current shell selection - the clip (@a clipSpeech), else the track or the mixer
//! channel with the edit cursor. One message, so a mouse click (ShellSelection's event
//! filter), window.select and the note editor all speak through this; silent when nothing
//! is selected. Polite, so view.announcements gates it like every other timeline message.
LMMS_EXPORT void announceSelection(const Clip* clip, const Track* track, const QString& channel);
//! Say a named marker snapshot after its crossing action has run.
LMMS_EXPORT void announceMarkerCrossing(const Marker& marker);
//! Speak @a message when view.announcements permits it (Polite). Headless it
//! still records lastAnnouncement(), so tests hear it without a screen reader.
LMMS_EXPORT void say(const QString& message);

} // namespace speech

} // namespace lmms

#endif // LMMS_TIMELINE_SPEECH_H
