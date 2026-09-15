/*
 * ControlReversibilityTableDetect.cpp - the `detect.*` group's rows of THE SPEC
 *                                       A16 classification table.
 *
 * A GROUP file, on the seam ControlReversibilityTable{TrackFolder,Vca,Routing,
 * Mastering}.cpp already use: the block's CLASS comes from each row, never from
 * the file it happens to live in, so a group's rows - here one recorded-action
 * true_inverse row and the group's two not_mutating inspectors - are joined into
 * reversibilityRowTable() through reversibilityDetectRowTable(). The rows are one
 * file because they are one feature (docs/FEATURE-LIST-0.3.0.md row 34, import
 * detection), and a reviewer reads the whole contract for that feature in one
 * place.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ControlReversibility.h"

/*! The import-detection surface (feature row 34, BACKLOG.md item 10). Two reads
 *  and one writer:
 *
 *   * `detect.analyze` reads ONE audio file and returns what it implies - the
 *     tempo, the first transient and the key. It writes no project state at all;
 *     the file it reads is the caller's input, and the suggestion it produces is
 *     the point of the feature ("shown as SUGGESTIONS the user accepts - never
 *     applied silently");
 *   * `detect.get_state` reads the project's own key field and the tempo map's
 *     state, and writes nothing;
 *   * `detect.apply` writes TWO things that no single live object carries: a
 *     tempo-map event (the map is not a JournallingObject and is NOT inside the
 *     Song's own checkpoint, which captures TrackContainer::saveSettings - the
 *     finding ControlCommandsTransportMap.cpp records) and the project's
 *     `<detected-key>` value (a plain value on the Song, the shape GroovePool
 *     has). Its inverse is therefore a recorded ACTION checkpoint that writes
 *     BOTH captures back, and one control.undo takes the whole detection off.
 *
 *  THE TRAP THIS ROW NAMES. `detect.apply` can be the FIRST thing a project ever
 *  does, so the pre-write key state is "no element at all" and the pre-write map
 *  state is "empty and inactive". An inverse that only restored what it captured
 *  would leave both writes behind on that first call - the shape the mastering
 *  row (ControlReversibilityTableMastering.cpp) records for its own output
 *  directory. Here the removal half is expressible as a restore: ProjectKey has
 *  an explicit clear() (and its loadSettings clears on an absent element), so
 *  writing back "the empty key" is a value, not a deletion - and TempoMap's
 *  operator= puts the captured map back whatever it held.
 */

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

const ReversibilityRow kDetectRows[] = {
	R("detect.analyze", RC::NotMutating, false,
		"it reads one audio file and reports the tempo, the first transient and the key "
		"the file implies. No project state is touched: BACKLOG.md item 10's rule is that "
		"a detection is a SUGGESTION, and this is the suggestion verb (detect.apply is "
		"what writes). The only state it keeps is none",
		"no write",
		""),
	R("detect.get_state", RC::NotMutating, false,
		"it reads the project's own key field, the tempo map's state and the static "
		"bounds and vocabulary of this feature. Nothing is written",
		"no write",
		""),
	R("detect.apply", RC::TrueInverse, true,
		"it writes the detected tempo as a tempo-map event at tick 0 AND the detected key "
		"into the project's `<detected-key>` field. Neither is reachable by a live "
		"checkpoint: the tempo map is not a JournallingObject and is not inside the Song's "
		"own checkpoint (ControlCommandsTransportMap.cpp records the finding), and the key "
		"field is a plain value on the Song. A half the analysis found nothing for is "
		"REFUSED before the first write, so a refusal leaves no half-detection behind",
		"action checkpoint: the tempo map AND the project key element are captured before "
		"the first write (the map by value, the key as its own XML text, carried whole in "
		"the transaction record's before-state) and the recorded step writes both back - "
		"control.undo takes the whole detection off in ONE step. The pre-first-detection "
		"state is expressible: an empty key is a value ProjectKey::clear() writes, and "
		"TempoMap::operator= restores an empty-and-inactive map. Re-issuing detect.apply "
		"is the forward path",
		""),
};

constexpr int kDetectRowCount = static_cast<int>(sizeof(kDetectRows) / sizeof(kDetectRows[0]));

#undef R

} // namespace

const ReversibilityRow* reversibilityDetectRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kDetectRowCount; }
	return kDetectRows;
}

} // namespace control
} // namespace lmms
