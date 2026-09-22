/*
 * ProjectIds.h - the project-scoped monotonic id counter of the agent surface
 *                (SPEC-stable-ids.md 2.1, 3.2).
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

#ifndef LMMS_PROJECT_IDS_H
#define LMMS_PROJECT_IDS_H

#include <QString>

#include "lmms_export.h"

class QDomElement;
class QDomNode;

namespace lmms
{

/*! The project-scoped monotonic counter behind every stable id.
 *
 * The rule the counter exists to keep (SPEC-stable-ids.md, invariant I1): an id
 * is a property of the OBJECT, not of its position. It is handed out once, at
 * construction; it never changes while the object is alive; and a number that
 * has been handed out is never handed out again while the document lives, so a
 * retired id cannot be reborn as a different object.
 *
 * One counter for every id family (tracks today; clips/notes/channels later), so
 * `next-id` is one number in the project file and one repair rule. The counter is
 * written to the project as the root's `next-id` attribute and read back on load;
 * because a legacy file carries no counter, ids are also assigned deterministically
 * at load (see Track::Track, which allocates in construction order).
 *
 * This is NOT the journal's jo_id_t: that identifies automation routing endpoints,
 * is allocated per session, and does not exist on every addressable object.
 */
class LMMS_EXPORT ProjectIds
{
public:
	/*! The id families this ONE counter serves (SPEC-ARCH-4 §1.5 Requirement 4
	 *  and the S3 slice row; census row 8 - "the header says this was always the
	 *  plan"). Every family draws from the same `next-id` and answers to the
	 *  same duplicate-id repair rule; the prefix is only the address the agent
	 *  surface spells it with (`trk-7` vs `clip-7` are different addresses even
	 *  though one counter handed out both numbers).
	 *
	 *  S3 declares all five families the spec names (clip-, lane-, note-,
	 *  scene-, warp-, beside the trk-/ch-/fx- the counter already serves). The
	 *  LANE-, SCENE- and WARP-OBJECTS do not exist until their own slices (S7,
	 *  S8, S9) - this declaration is what those slices inherit, and it is purely
	 *  additive: nothing allocates per-family and `next-id` stays ONE number
	 *  (census row 8: "One counter, one `next-id` on the root").
	 */
	enum class IdFamily
	{
		Track,   //!< `trk-`  (control::trackId; the family that existed first)
		Clip,    //!< `clip-` (control::clipId)
		Lane,    //!< `lane-` (entity lands with S7)
		Note,    //!< `note-` (control::noteId)
		Scene,   //!< `scene-` (entity lands with S8)
		Warp,    //!< `warp-` (entity lands with S9)
		Channel, //!< `ch-`   (control::channelId)
		Effect   //!< `fx-`   (control::effectId)
	};

	//! The grammar prefix of \a family, e.g. `"clip-"` - the exact spelling
	//! `control::idToIndex()` parses back (SPEC-ARCH-4 census row 8).
	static const char* familyPrefix(IdFamily family);

	//! The value allocate() will hand out next. This is the file's `next-id`.
	static int next();

	/*! Raises the counter so next() is at least \a next. It never lowers it:
	 *  an id observed in the document must not be re-allocated, and a counter
	 *  read from a file that is behind the ids in that file must catch up
	 *  (SPEC-stable-ids.md rule R3).
	 */
	static void observeNext(int next);

	/*! The load pass's own counter: \a next is the `next-id` attribute of the
	 *  document that was just loaded, and it IS the counter for that document.
	 *
	 *  NOT observeNext(): a load walks the document and every object's
	 *  constructor calls allocate() for a placeholder before the element's own
	 *  `id` is read, so the counter arrives here ABOVE the document's value -
	 *  and a max-merge keeps it there, which made a save/load/save round trip
	 *  write a different `next-id` every time (measured by StableTrackIdsTest
	 *  and TempoMapPersistenceTest: round1 `next-id="3"`, round2 `next-id="5"`
	 *  for a three-object project, so the second file was not byte-identical to
	 *  the first). This restores the document's value, but never below an id the
	 *  document actually carries or a placeholder an object KEPT - so nothing
	 *  in the loaded document can be handed out again (R3).
	 */
	static void restoreFromDocument(int next);

	//! Raises the counter above \a id, so \a id can never be allocated again.
	static void observe(int id);

	//! n = next(); observe(n). The object constructors call this.
	static int allocate();

	//! A fresh document: the counter goes back to 0, the assignment count to 0.
	static void reset();

	/*! How many ids a load pass had to ASSIGN, i.e. objects whose element carried
	 *  no id attribute (and duplicate-id repairs, which are also an assignment).
	 *  project.open reports this as `ids_assigned`: a legacy file is upgraded
	 *  rather than silently rewritten. Reset by beginLoad().
	 */
	static int loadAssignments();

	//! Starts a load pass: clears loadAssignments(). Does not touch the counter.
	static void beginLoad();

	/*! Ends a load pass. Until it is called, allocate()'s ids are the walk's
	 *  PLACEHOLDERS - a number every object takes in its constructor and that
	 *  the element's own `id` attribute overwrites a moment later - so they are
	 *  not counted as "handed out" and the document's own counter can be
	 *  restored over them. An object that KEEPS its placeholder says so through
	 *  noteLoadAssignment(), which does count it.
	 */
	static void endLoad();

	/*! A loader found an element with no usable id attribute, so the object
	 *  KEEPS the id it was constructed with: it is a live id from now on, and
	 *  \\a id is the one the caller passed to noteLoadAssignment(). */
	static void noteLoadAssignment(int id);

	/*! True when \a node belongs to a DOCUMENT element tree - a project file, a
	 *  journal checkpoint - and false when it belongs to a COPY payload (the clip
	 *  drag/copy DataFile, Track::clone()'s temporary document, an instrument or
	 *  device preset, a plugin.state_* document).
	 *
	 *  This is rule R4 - "a copy is a new object" - made checkable at the one
	 *  place every reader of a stored id passes through. A copy payload carries
	 *  the SOURCE object's attributes verbatim, id included; an object built from
	 *  one is a different object, so its id must not be taken from the payload or
	 *  two live objects would answer to one `<prefix>-<n>` and the address would
	 *  be ambiguous. The payload containers are named in ProjectIds.cpp, each with
	 *  the writer that produces it.
	 */
	static bool isDocumentElement(const QDomNode& node);

	// ---------------------------------------------------------------------
	// The per-object revision pair (SPEC-ARCH-4 §1.5 Requirement 4, S3):
	// `rev`, "a counter the writer increments on every change to that object",
	// and `writer`, "an instance identifier in <head>". Neither is needed to
	// load a file: an absent `rev` means 0 (readRevision's rule), so every
	// project written before this slice loads and behaves exactly as it did.
	// ---------------------------------------------------------------------

	//! The instance id every `writer` attribute this process stamps carries,
	//! and the value Song writes into `<head>` when a save holds a revision.
	//! Stable within one process (so a save/load/save round trip is byte
	//! identical) and different in the next one (so two writers of two branches
	//! name themselves). QUuid, first 12 hex digits.
	static QString writerInstance();

	//! Starts a project save: clears revisionWritten()'s flag. Song calls it
	//! the moment it begins serialising a document.
	static void beginSave();

	//! True when THIS save has stamped a `rev` on any object - the one
	//! condition under which Song writes the `<head>` writer attribute, so a
	//! project nobody revised re-saves exactly the bytes it always had.
	static bool revisionWritten();

	/*! Reads the revision pair off a loaded element: `rev` (absent, unparseable
	 *  or non-positive all mean 0 - "neither is needed to load a file"), the
	 *  `writer` string (empty when absent) and the content fingerprint the next
	 *  save compares against. The three references are the OBJECT's own fields
	 *  (Track, Clip); nothing is counted, observed or repaired here - a
	 *  revision is content state, not an id.
	 */
	static void readRevision(const QDomElement& element, int& rev,
		QString& writer, QString& contentHash);

	/*! Stamps the revision pair on \a element, the LAST thing a writer does
	 *  with the element. Compares the element's content fingerprint with the
	 *  one read at load (or recorded at the previous save):
	 *
	 *  - no fingerprint yet (an object that was never loaded) -> this IS the
	 *    baseline; nothing is written, because a brand-new object has made no
	 *    revisions (`rev` absent == 0);
	 *  - content differs -> this writer changed the object: rev+1, writer =
	 *    writerInstance(), fingerprint updated;
	 *  - content matches -> an unchanged object keeps the `rev`/`writer` it
	 *    came with (written back verbatim when it has them, nothing when it
	 *    has none), which is what keeps save/load/save byte-identical.
	 *
	 *  `rev > 0` also raises revisionWritten() for Song's `<head>` gate.
	 */
	static void writeRevision(QDomElement& element, int& rev,
		QString& writer, QString& contentHash);
};

} // namespace lmms

#endif // LMMS_PROJECT_IDS_H
