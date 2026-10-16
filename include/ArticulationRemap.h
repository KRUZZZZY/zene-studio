/*
 * ArticulationRemap.h - keeps the notes pointing at the same articulations when a track's map is reordered or trimmed
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
#ifndef LMMS_ARTICULATION_REMAP_H
#define LMMS_ARTICULATION_REMAP_H

#include <vector>

#include <QString>

#include "lmms_export.h"

namespace lmms
{

class InstrumentTrack;

/*! 0.5.0 L07 (Q050-015): a note names an articulation by its 1-based position in the track's map, so
 *  moving or removing an entry would silently re-point every note that asks for one. When
 *  track.articulations_set is given where each new entry came from ('from'), the notes follow their
 *  articulation: a note whose articulation moved takes its new position, one whose articulation was
 *  removed asks for none. UI thread; nothing here runs on the audio thread. */
struct LMMS_EXPORT ArticulationNoteChange
{
	int clip = 0;    //!< Clip::id()
	int note = 0;    //!< Note::id()
	int before = 0;  //!< the note's articulation before the edit
	int after = 0;   //!< ... and after it
};

//! Checks a 'from' list for a map of @a newCount entries replacing one of @a oldCount: one old (1-based)
//! position or 0 (a new entry) per new entry, no old position twice. False, with @a error saying why, otherwise.
LMMS_EXPORT bool validateArticulationOrigins(const std::vector<int>& from, int oldCount, QString* error);

//! The notes of @a track's MIDI clips that change when the map becomes @a from (validated): where each goes.
LMMS_EXPORT std::vector<ArticulationNoteChange> planArticulationRemap(const InstrumentTrack& track, const std::vector<int>& from, int oldCount);

//! Writes every change's 'after' (@a forward) or 'before' to the notes of the track with the id @a trackId; a clip
//! or note that is gone is skipped. Written with the track locked, as the audio thread reads them.
LMMS_EXPORT void applyArticulationChanges(const QString& trackId, const std::vector<ArticulationNoteChange>& changes, bool forward);

} // namespace lmms

#endif // LMMS_ARTICULATION_REMAP_H
