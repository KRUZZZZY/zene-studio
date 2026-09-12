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

#include "lmms_export.h"

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
	//! The value allocate() will hand out next. This is the file's `next-id`.
	static int next();

	/*! Raises the counter so next() is at least \a next. It never lowers it:
	 *  an id observed in the document must not be re-allocated, and a counter
	 *  read from a file that is behind the ids in that file must catch up
	 *  (SPEC-stable-ids.md rule R3).
	 */
	static void observeNext(int next);

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

	//! A loader found an element with no id attribute, so it had to assign one.
	static void noteLoadAssignment();
};

} // namespace lmms

#endif // LMMS_PROJECT_IDS_H
