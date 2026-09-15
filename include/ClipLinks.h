/*
 * ClipLinks.h - the link relation between clips (the engine half of linked /
 *               smart clips; feature-list row 6, board task #645).
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

#ifndef LMMS_CLIP_LINKS_H
#define LMMS_CLIP_LINKS_H

#include <QString>
#include <QVector>

#include "lmms_export.h"

namespace lmms
{

class Clip;
class MidiClip;

/*! The link relation: what makes two clips one smart clip.
 *
 *  THE DECISION (recorded here and in docs/LINKED-CLIPS.md §2, because the row
 *  asks for it): the relation is a **persisted group id plus a write-through
 *  mirror**, NOT a shared content object and NOT copy-on-write.
 *
 *  - `Clip::linkId()` is an int on the clip, 0 meaning "unlinked", written to
 *    the clip's OWN element as the `link` attribute (Clip::saveClipEdits /
 *    Clip::loadClipEdits) beside the take lane. There is no second registry in
 *    the project file and no pointer between clips, so a save carries the whole
 *    relation and a reload rebuilds every group from the clips themselves.
 *  - An edit to a member is **mirrored** to the other members in the same step:
 *    the edited clip's content (its note list - the clip's source in the MIDI
 *    domain) is copied onto every member that differs, so an edit to one IS
 *    seen by all. The mirror is one function (mirrorContent) and the engine's
 *    note-mutating entry points call it, so the content channel is link-aware
 *    wherever it is written.
 *  - The alternative - one NoteVector aliased by N clips through a shared
 *    pointer - buys instant sharing at the cost of a load-order re-linking step
 *    (the very step a save/reload round trip has to prove), of a lifetime rule
 *    for the list, and of the piano roll's NoteView pointers crossing clips.
 *    It also makes the clips' own elements no longer self-describing. It was
 *    rejected for those reasons, not for want of a shared_ptr.
 *  - Copy-on-write in its strict sense (share until someone edits, then
 *    DETACH) is the opposite of what this feature means: an edit to one must be
 *    seen by all, so a write fans out instead of detaching. Unlinking - the
 *    detach - is its own operation (clip.link_remove), never a side effect of
 *    an edit.
 *
 *  WHAT THE GROUP SHARES is the clip's CONTENT (the note list). What it does
 *  NOT share is where and how each member plays it: position, length, source
 *  offset, fades, gain, mute, name, colour and take lane all stay per-member,
 *  which is what makes a link a smart clip rather than a rename. See
 *  docs/LINKED-CLIPS.md §4 and the one-line note in docs/KNOWN-LIMITATIONS.md.
 */
namespace ClipLinks
{

//! Every clip of the song whose linkId() is \p linkId, in arrangement order
//! (tracks in song order, clips by start position). Empty for \p linkId <= 0.
LMMS_EXPORT QVector<Clip*> group(int linkId);

/*! The group \p clip is a member of, \p clip included; empty when it is
 *  unlinked. A group of one is a legal (if degenerate) group: a relation whose
 *  other members were deleted. clip.link_get_state reports it and
 *  clip.link_remove clears it. */
LMMS_EXPORT QVector<Clip*> groupOf(const Clip* clip);

//! A group id no live group has used: ProjectIds::allocate() (project-scoped,
//! written to the project as `next-id` and read back on load, so a reloaded
//! document can never hand the same number out twice - SPEC-stable-ids.md I1).
LMMS_EXPORT int allocateGroupId();

/*! The members of \p clip's group that can carry the shared content, \p clip
 *  excluded. A link group's content channel is the note list, so this is every
 *  other MidiClip of the group: a SampleClip cannot be a member
 *  (clip.link_create refuses one, typed), and this skips rather than asserts
 *  so a hand-edited project file cannot crash a read. */
LMMS_EXPORT QVector<MidiClip*> contentMembersOf(const Clip* clip);

//! The content fingerprint of \p clip: its note list serialised, so two members
//! compare equal exactly when they carry the same notes (every field a change
//! can touch, through the same serialiser the project file uses). Empty string
//! for a clip with no note list.
LMMS_EXPORT QString contentFingerprint(Clip* clip);

//! What mirrorContent() did.
struct MirrorReport
{
	bool ok = true;
	//! Members whose content was rewritten by this call.
	int written = 0;
	//! Members whose content was already the source's, so nothing was written.
	int alreadyInSync = 0;
	//! Members of the group that cannot carry content at all (skipped).
	int skipped = 0;
	//! Members of the group, the source included.
	int members = 0;
	//! Why nothing was written, when nothing was: a human-readable reason.
	QString reason;
};

/*! Mirrors \p source's content onto every other member of its group - THE
 *  propagation. This is the one write path behind "an edit to one is seen by
 *  all": the engine's note-mutating entry points call it, clip.link_sync calls
 *  it, and clip.link_create calls it so a group starts in sync.
 *
 *  ONE journal checkpoint covers every member (ProjectJournal's
 *  multi-object overload), so in the command path the registry's
 *  mergeCheckpointsFrom() folds the whole write into ONE undo step: one
 *  control.undo restores every member of the group, not just the edited one.
 *
 *  Re-entrant-safe: while a mirror is in flight every nested call is a no-op,
 *  so the members' own note entry points cannot mirror back into the source.
 *  Returns ok=false with \p reason filled only when \p source is not a clip
 *  with a content channel at all; an unlinked source is a no-op, not an error.
 */
LMMS_EXPORT MirrorReport mirrorContent(Clip* source);

/*! Suspends mirroring for the current call: used by a LOAD pass, where each
 *  clip is re-created in turn and a member's notes are not its own yet. A
 *  project's members are written in sync (that is what mirroring means), so a
 *  reload that skipped the writes reconstructs the same group; a file whose
 *  members were made to disagree by hand loads as it stands, and
 *  clip.link_get_state reports the disagreement rather than hiding it.
 */
LMMS_EXPORT void suspendMirroring();
LMMS_EXPORT void resumeMirroring();

//! RAII form of the pair above (a load pass that throws or returns early must
//! not leave mirroring off for the rest of the session).
class LMMS_EXPORT MirroringSuspension
{
public:
	MirroringSuspension() { suspendMirroring(); }
	~MirroringSuspension() { resumeMirroring(); }
	MirroringSuspension(const MirroringSuspension&) = delete;
	MirroringSuspension& operator=(const MirroringSuspension&) = delete;
};

} // namespace ClipLinks

} // namespace lmms

#endif // LMMS_CLIP_LINKS_H
