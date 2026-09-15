/*
 * ControlReversibilityTableStructure.cpp - THE SPEC A16 classification table's
 *                                          STRUCTURAL rows: the created /
 *                                          deleted / reordered objects whose
 *                                          inverse is a recorded operation
 *                                          carrying a captured document (Zene
 *                                          Studio, task #664, feature row 75).
 *
 * This file is data. It holds the four rows of the structural group -
 *
 *   track.add       the inverse is the OPERATION: remove the track it created
 *   track.move      the inverse is the recorded PAIR: the index it came from
 *   track.remove    the inverse is the captured DOCUMENT: the track's own XML,
 *                   clips and notes included, restored by the project loader's
 *                   own call, at the index it was removed from
 *   plugin.unload   the same shape for a device: the state document, the same
 *                   plugin, the same index in the chain
 *
 * - which were the last rows of ControlReversibilityTableAction.cpp before that
 * file crossed the 500-line file-length ratchet. The split follows the seam the
 * table already uses for the chain-preset group
 * (ControlReversibilityTableChain.cpp): the class is unchanged (all four are
 * true_inverse ACTION rows), and reversibilityActionRowTable() still JOINS this
 * block with its own rows, so every caller - ReversibilityTable's constructor,
 * the anti-drift test and control.transactions - reads ONE true_inverse block
 * with ONE row count.
 *
 * WHY THE ROWS ARE TOGETHER: the three track rows and the device row are the
 * same class of edit - an object that is CREATED or DESTROYED rather than
 * changed - and their mechanism is the same primitive
 * (ProjectJournal::addJournalStructure, control::addStructuralUndoStep), which
 * differs from an action step in exactly one way: the document the inverse
 * carries is MEASURED and charged to the undo stack's byte budget. A reader who
 * wants to know what a structural edit costs reads these four rows.
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
 */

#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
//! Same macro, same field order as every other table file - the row SHAPE is
//! one definition even though the files are several.
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kStructureRows[] = {

	R("track.add", RC::TrueInverse, true,
		"a created Track has no before-state to restore, so the inverse is the "
		"operation, not a snapshot. DISAGREEMENT with A16-STATUS-MEASURED.md, "
		"which records this as reversible:false (no checkpoint exists)",
		"action checkpoint: the recorded undo step removes the created track "
		"through the product's own TrackContainerView::deleteTrackView path "
		"(the path whose checkpoint upstream left commented out); a fresh "
		"instrument track carries only defaults, so removing it restores the "
		"container exactly. The project-scoped next-id counter is monotonic and "
		"is NOT rewound: a re-add gets a fresh trk-<n>, which is the documented "
		"limit of this inverse",
		""),
	R("track.move", RC::TrueInverse, true,
		"a container's ORDER is not a serialized property of any track, so no "
		"object checkpoint can express a reorder: the state that changed is the "
		"song's track list, and restoring a Track checkpoint would put back the "
		"track's own contents, not its position",
		"action checkpoint (structural step, payload 0 bytes - there is no "
		"document to capture, only the index it came from): the recorded undo "
		"step puts the track back at its previous index through the engine's own "
		"TrackContainer::moveTrack, the same call the product's drag and the "
		"arrow-key moves make; the redo sends it forward again",
		""),
	R("track.remove", RC::TrueInverse, true,
		"a deleted JournallingObject's journal id resolves to nullptr, so a "
		"checkpoint cannot bring it back. DISAGREEMENT with "
		"A16-STATUS-MEASURED.md, which records this as reversible:false",
		"action checkpoint (structural step): the track's own XML "
		"(Track::saveState - its clips and their notes included) is captured "
		"before the delete and the recorded undo step recreates it with "
		"Track::create(element, song), the same call TrackContainer::loadSettings "
		"makes, AT THE INDEX IT WAS REMOVED FROM. WITH ITS CLIPS: a checkpoint "
		"taken inside the container cannot do this, because ~Track destroys the "
		"clips before removeTrack() is reached - the track would come back and "
		"the music would not. The capture is bounded at 64 KiB and its measured "
		"size is charged to the undo stack's byte budget",
		"a track whose XML is over the 64 KiB cap records no inverse and says so "
		"('UNIMPLEMENTED for this track'); the transaction's before-state still "
		"names what was removed"),
	R("plugin.unload", RC::TrueInverse, true,
		"a removed Effect has no live object a checkpoint could restore - "
		"EffectChain::removeEffect + deleteLater destroys the instance, and its "
		"settings live only in it. DISAGREEMENT with A16-STATUS-MEASURED.md, "
		"which records this as reversible:false ('snapshot only, rebuild it by "
		"hand')",
		"action checkpoint (structural step): the device's own state document "
		"(controlEffectStateXml, which carries the sub-plugin key that identifies "
		"a hosted plugin and not merely its descriptor name) is captured before "
		"the removal, and the recorded undo step RE-INSTANTIATES the device "
		"through the chain's own instantiate path and puts the settings back, AT "
		"THE INDEX IT WAS REMOVED FROM. The document's measured size is charged "
		"to the undo stack's byte budget",
		"a device whose state document is over the 64 KiB cap records no inverse "
		"and reports reversible:false; before.state_xml is then the bounded "
		"record that allows a rebuild by hand (write it to a file, plugin.load "
		"the same dev-<n>, plugin.state_load the file - the chain ORDER is not "
		"restored by that manual path)"),

};

constexpr int kStructureRowCount =
	static_cast<int>(sizeof(kStructureRows) / sizeof(kStructureRows[0]));

} // namespace

const ReversibilityRow* reversibilityStructureRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kStructureRowCount; }
	return kStructureRows;
}

} // namespace control
} // namespace lmms
