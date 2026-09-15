/*
 * ControlClipLinkSupport.h - the helpers the two clip.link_* translation units
 *                            share: the ids, the group's read-back shape and
 *                            the argument reader (feature-list row 6, board
 *                            task #645).
 *
 * WHY IT IS A HEADER. The group is split across two translation units because
 * the file-length ratchet is not moved for a new feature (the same reason
 * clip.trim/clip.slip got their own file, and the read/edit split the comp,
 * automation, warp and chain-preset groups already follow - see
 * src/core/CMakeLists.txt and ControlRegistryGroups.h). Both halves need to name
 * a clip by its clip-<n> id, shape a group for a result, and validate the
 * `clips` argument, so those three answers live here ONCE rather than being
 * copied into both. Everything here is `inline` and stateless: no state, no
 * registry, no build switch.
 *
 * The design decision this whole group implements - a persisted group id plus a
 * write-through mirror, not a shared content object and not copy-on-write - is
 * recorded in include/ClipLinks.h and in docs/LINKED-CLIPS.md.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_CONTROL_CLIP_LINK_SUPPORT_H
#define LMMS_CONTROL_CLIP_LINK_SUPPORT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Clip.h"
#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "MidiClip.h"

namespace lmms
{

namespace control
{

//! The clip.link_* group's shared vocabulary, in one place for both TUs.
namespace cliplink
{

/*! The reason every mutating command of this group records: what a link IS is
 *  the clip's own serialized state, so the engine's own checkpoint is a live
 *  inverse and not a snapshot.
 *
 *  - `link` is written by Clip::saveClipEdits ONLY when the clip is a member and
 *    read back by Clip::loadClipEdits with a RESET-ON-ABSENCE rule, so the
 *    checkpoint taken before a FIRST link carries no `link` attribute and
 *    restoring it returns the clip to "unlinked" exactly. That is what makes the
 *    inverse of clip.link_create a real inverse (compare `clip.trim`'s clause in
 *    ControlCommandsClipTrim.cpp for the same rule on the edge attributes).
 *  - the members' note lists are MidiClip state: MidiClip::loadSettings clears
 *    and re-loads the list, which is the mechanism the piano roll's own note
 *    edits already reverse with.
 *  - ProjectJournal's multi-object overload takes ONE checkpoint covering every
 *    member written, and the registry's mergeCheckpointsFrom() folds it into the
 *    one step the command makes, so ONE undo restores the whole group rather than
 *    only the member the caller happened to name.
 */
inline const QString ClauseClipLinkJournalled = QStringLiteral("ProjectJournal (Clip checkpoints: "
	"Clip::saveClipEdits writes the 'link' attribute onto a member's own element and "
	"Clip::loadClipEdits resets it to 0 when the attribute is absent, so the checkpoint taken "
	"before a FIRST link restores 'unlinked' exactly, and MidiClip::loadSettings clears and "
	"re-loads the note list that a mirror writes; one checkpoint covers every member written "
	"and the registry merges them into one undo step)");

/*! The clip-<n> id of \p clip, or an empty string when it is not in the song's
 *  arrangement (a clip of another container, mid-delete). Every id this group
 *  reports goes through here, so a member that is not addressable is reported as
 *  an empty id rather than a wrong one. */
inline QString clipIdOf(const Clip* clip)
{
	if (clip == nullptr) { return QString(); }
	for (const ClipRef& ref : enumerateClips())
	{
		if (ref.clip == clip) { return clipId(ref.id); }
	}
	return QString();
}

//! The state a caller reads back: the clip's own membership plus its group.
inline QJsonObject membershipState(const Clip* clip)
{
	QJsonObject out;
	out.insert(QStringLiteral("clip"), clipIdOf(clip));
	out.insert(QStringLiteral("group"), clip == nullptr ? 0 : clip->linkId());
	return out;
}

//! One link group as clip.link_get_state reports it.
inline QJsonObject groupState(const QVector<Clip*>& members)
{
	QJsonObject out;
	if (members.isEmpty()) { return out; }

	out.insert(QStringLiteral("group"), members.first()->linkId());
	out.insert(QStringLiteral("size"), static_cast<int>(members.size()));
	// The content channel, named so a reader does not have to infer what a link
	// shares: this release's link group shares the clip's note list.
	out.insert(QStringLiteral("content"), QStringLiteral("notes"));

	QJsonArray ids;
	QJsonArray inSync;
	QJsonArray divergent;
	int contentMembers = 0;
	// The group's FIRST member in arrangement order is the yardstick a reader can
	// reproduce (no hidden "leader" field exists): every member is compared
	// against it, and a member that differs is named in `divergent` instead of
	// being averaged away. Its fingerprint is serialised ONCE.
	const QString referenceFingerprint = ClipLinks::contentFingerprint(members.first());
	for (Clip* member : members)
	{
		const QString id = clipIdOf(member);
		ids.append(id);
		if (auto* midi = dynamic_cast<MidiClip*>(member))
		{
			++contentMembers;
			if (ClipLinks::contentFingerprint(midi) == referenceFingerprint)
			{
				inSync.append(id);
			}
			else
			{
				divergent.append(id);
			}
		}
	}
	out.insert(QStringLiteral("members"), ids);
	out.insert(QStringLiteral("reference"), ids.first());
	out.insert(QStringLiteral("content_members"), contentMembers);
	out.insert(QStringLiteral("notes"), [&]() {
		if (auto* midi = dynamic_cast<MidiClip*>(members.first()))
		{
			return static_cast<int>(midi->notes().size());
		}
		return 0;
	}());
	out.insert(QStringLiteral("in_sync"), inSync);
	out.insert(QStringLiteral("divergent"), divergent);
	return out;
}

//! The members of \p clip's group, \p clip included, or empty when unlinked.
inline QVector<Clip*> groupOf(const Clip* clip) { return ClipLinks::groupOf(clip); }

/*! Reads the `clips` argument: an array of clip ids, non-empty, no duplicates.
 *  Refuses typed and leaves \p out untouched otherwise. */
inline bool readClipList(const QJsonObject& args, QVector<QString>* out, ControlResult* error)
{
	const QJsonValue value = args.value(QStringLiteral("clips"));
	if (!value.isArray())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'clips' must be an array of clip ids (clip-<n>)"));
		return false;
	}
	const QJsonArray array = value.toArray();
	if (array.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'clips' is empty: a link needs at least one other clip"));
		return false;
	}
	QStringList seen;
	for (const QJsonValue& entry : array)
	{
		if (!entry.isString())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'clips' must hold clip ids (clip-<n>) as strings"));
			return false;
		}
		const QString id = entry.toString();
		if (seen.contains(id))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'clips' names '%1' twice").arg(id));
			return false;
		}
		seen.append(id);
		out->append(id);
	}
	return true;
}

} // namespace cliplink

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_CLIP_LINK_SUPPORT_H
