/*
 * ControlCommandsClipLink.cpp - the clip.link_* group's WRITING half: the two
 *                                commands that create and break the link
 *                                relation (clip.link_create, clip.link_remove)
 *                                - linked / smart clips, feature-list row 6,
 *                                board task #645.
 *
 * THE DECISION is recorded in include/ClipLinks.h (the engine half) and in
 * docs/LINKED-CLIPS.md: the relation is a persisted group id on each member
 * (`Clip::linkId()`, the `link` attribute of the clip's own element) plus a
 * write-through mirror of the clip's CONTENT - its note list. It is NOT a shared
 * content object, and NOT copy-on-write-with-detach: an edit to one member is
 * seen by all of them, and unlinking - the detach - is its own command.
 *
 * WHY THE GROUP IS SPLIT. The read half and the repair verb are
 * ControlCommandsClipLinkState.cpp; the file-length ratchet is not moved for a
 * new feature (the same reason clip.trim/clip.slip got their own translation
 * unit), so the group is two TUs and both register through
 * registerClipLinkCommands() below. The helpers the two halves share are
 * include/ControlClipLinkSupport.h.
 *
 * THE GROUP'S NAME is `clip.`, not a new `linked.`/`smart.` group: the relation
 * is a property of a clip, every id here addresses clips, and an agent that
 * knows clip.trim finds clip.link_create beside it. It is NOT `link.*` - that
 * name is taken by session tempo/beat sync (docs/LINK-SYNC.md), a different
 * feature that happens to share the English word.
 *
 * WHAT PROPAGATES, stated here because the row asks for the one line: content
 * edits propagate. clip.link_create and clip.link_sync mirror one member's note
 * list to every member, and the note.* verbs (add / remove / move / resize /
 * velocity_set) mirror through the engine's own note entry points, so an edit to
 * one member IS the edit to all of them. Position, length, source offset, fades,
 * gain, mute, name, colour and take lane are PER-MEMBER and never propagate -
 * they describe where and how a member plays the shared content, which is what
 * makes a link a smart clip rather than a rename. See docs/LINKED-CLIPS.md §2
 * and the one-line note in docs/KNOWN-LIMITATIONS.md.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "Clip.h"
#include "ClipLinks.h"
#include "ControlClipLinkSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClip.h"
#include "ProjectJournal.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{


/*! Resolves and validates the 'clips' list against \p anchor BEFORE anything is
 *  written, so a refusal cannot leave a half-built group behind (the same rule
 *  clip.trim's planTrim follows). Every id must name a live clip, none may be
 *  the anchor itself, a duplicate, or a member of another group - merging two
 *  groups would silently overwrite one group's content with the other's - and
 *  each must carry a note list, because that is the group's content channel.
 *
 *  Split out of the handler for the same reason its siblings here are: the
 *  per-method complexity target is CCN 10 and the ratchet is not moved for a new
 *  feature (tests/complexity-gate.sh). */
bool resolveNewMembers(const ClipRef& anchor, const QVector<QString>& requested,
	QVector<Clip*>* newcomers, ControlResult* error)
{
	for (const QString& id : requested)
	{
		ClipRef ref;
		if (!resolveClip(id, &ref, error)) { return false; }
		if (ref.clip == anchor.clip)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'clips' names the anchor itself ('%1')").arg(id));
			return false;
		}
		if (dynamic_cast<MidiClip*>(ref.clip) == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' cannot be a link member: a link group shares a note list, "
					"and this clip has none (audio clips are their own source in 0.3.0 - "
					"docs/KNOWN-LIMITATIONS.md)").arg(id));
			return false;
		}
		if (anchor.clip->linkId() > 0 && ref.clip->linkId() == anchor.clip->linkId())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' is already a member of this link group").arg(id));
			return false;
		}
		if (ref.clip->linkId() > 0)
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("'%1' is already a member of link group %2; clip.link_remove it "
					"first - joining it here would merge two groups' content")
					.arg(id).arg(ref.clip->linkId()));
			return false;
		}
		newcomers->append(ref.clip);
	}
	return true;
}

//! The clips whose own serialized state a link changes: the anchor (when it was
//! unlinked) and every newcomer. ONE checkpoint, taken before the first write.
QVector<JournallingObject*> linkCheckpointList(Clip* anchor, const QVector<Clip*>& newcomers)
{
	QVector<JournallingObject*> toCheckpoint;
	if (anchor != nullptr && anchor->linkId() <= 0) { toCheckpoint.append(anchor); }
	for (Clip* newcomer : newcomers) { toCheckpoint.append(newcomer); }
	return toCheckpoint;
}

//! The clip-<n> ids of \p clips, in order - the `adopted` list a create reports.
QJsonArray clipIdsOf(const QVector<Clip*>& clips)
{
	QJsonArray ids;
	for (Clip* clip : clips) { ids.append(cliplink::clipIdOf(clip)); }
	return ids;
}

void registerClipLinkCreate(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.link_create");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("link_create");
	cmd.description = QStringLiteral("Link clips to a clip so they share its content: an edit to "
		"the note list of any member is seen by every member (linked / smart clips). 'clip' is "
		"the content source and the group's anchor - if it is already linked, the named clips "
		"join ITS group; otherwise a new group is created and 'clip' joins it. The new members "
		"ADOPT 'clip'\"s notes (the group starts in sync), while their own position, length, "
		"source offset, fades, gain and take lane stay their own: a link shares content, not "
		"placement. Reversible through the ProjectJournal (one Clip checkpoint per member "
		"written, merged into one undo step). See docs/LINKED-CLIPS.md.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("clips"), arrayProperty()},
	}, {QStringLiteral("clip"), QStringLiteral("clips")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("group"), integerProperty()},
		{QStringLiteral("members"), arrayProperty()},
		{QStringLiteral("size"), integerProperty()},
		{QStringLiteral("content"), stringProperty()},
		{QStringLiteral("notes"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef anchor;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &anchor, &error))
		{
			return error;
		}
		QVector<QString> requested;
		if (!cliplink::readClipList(args, &requested, &error)) { return error; }
		QVector<Clip*> newcomers;
		if (!resolveNewMembers(anchor, requested, &newcomers, &error)) { return error; }

		const int group = anchor.clip->linkId() > 0
			? anchor.clip->linkId()
			: ClipLinks::allocateGroupId();
		if (ProjectJournal* journal = Engine::projectJournal(); journal != nullptr)
		{
			journal->addJournalCheckPoint(linkCheckpointList(anchor.clip, newcomers));
		}
		anchor.clip->setLinkId(group);
		for (Clip* newcomer : newcomers) { newcomer->setLinkId(group); }

		// The group starts in sync: the members adopt the anchor's content. The
		// mirror takes its own checkpoint over the members it rewrites, and the
		// registry merges both into this command's one undo step.
		const ClipLinks::MirrorReport mirror = ClipLinks::mirrorContent(anchor.clip);

		QJsonObject result = cliplink::groupState(ClipLinks::group(group));
		result.insert(QStringLiteral("adopted"), clipIdsOf(newcomers));
		result.insert(QStringLiteral("mirrored"), mirror.written);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), cliplink::clipIdOf(anchor.clip));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(cliplink::membershipState(anchor.clip),
				QStringLiteral("clip.link_remove"), inverseArgs, true,
				cliplink::ClauseClipLinkJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}



void registerClipLinkRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.link_remove");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("link_remove");
	cmd.description = QStringLiteral("Unlink a clip: it leaves its link group and keeps the "
		"content it has, but stops following - and stops being followed by - the other "
		"members. This is the operation that detaches (there is no detach-on-edit: an edit to "
		"a member propagates). When the group is left with a single member that member's link "
		"is dissolved too, so no group of one outlives its last pair; a clip that is not "
		"linked is refused, typed. Reversible through the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("removed"), stringProperty()},
		{QStringLiteral("group"), integerProperty()},
		{QStringLiteral("remaining"), arrayProperty()},
		{QStringLiteral("dissolved"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		ClipRef ref;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		if (ref.clip->linkId() <= 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' is not a member of a link group")
					.arg(args.value(QStringLiteral("clip")).toString()));
		}

		const int group = ref.clip->linkId();
		const QVector<Clip*> members = cliplink::groupOf(ref.clip);
		QVector<Clip*> remaining;
		for (Clip* member : members)
		{
			if (member != ref.clip) { remaining.append(member); }
		}
		// A relation needs two ends: the last pair leaving leaves no group of one.
		const bool dissolved = remaining.size() == 1;

		QVector<JournallingObject*> toCheckpoint;
		toCheckpoint.append(ref.clip);
		if (dissolved) { toCheckpoint.append(remaining.first()); }
		if (ProjectJournal* journal = Engine::projectJournal(); journal != nullptr)
		{
			journal->addJournalCheckPoint(toCheckpoint);
		}

		ref.clip->setLinkId(0);
		if (dissolved) { remaining.first()->setLinkId(0); }

		QJsonObject result;
		result.insert(QStringLiteral("removed"), cliplink::clipIdOf(ref.clip));
		result.insert(QStringLiteral("group"), group);
		QJsonArray ids;
		for (Clip* member : remaining) { ids.append(cliplink::clipIdOf(member)); }
		result.insert(QStringLiteral("remaining"), ids);
		result.insert(QStringLiteral("dissolved"), dissolved);
		/* The inverse of an unlink is the link itself: the removed clip rejoins
		 * the group it left, which `clip.link_create` with the remaining member
		 * as the anchor's content source performs. `applies: journal` is what
		 * control.undo actually uses - the tag is part of each clip's checkpointed
		 * state - and this inverse names the same operation for a reader and for a
		 * client replaying the transaction record. */
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), cliplink::clipIdOf(remaining.isEmpty()
			? ref.clip : remaining.first()));
		QJsonArray inverseClips;
		inverseClips.append(cliplink::clipIdOf(ref.clip));
		inverseArgs.insert(QStringLiteral("clips"), inverseClips);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(cliplink::membershipState(ref.clip), QStringLiteral("clip.link_create"),
				inverseArgs, true, cliplink::ClauseClipLinkJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


} // namespace

void registerClipLinkCommands(ControlRegistry& registry)
{
	registerClipLinkCreate(registry);
	registerClipLinkRemove(registry);
	registerClipLinkStateCommands(registry);
}

} // namespace lmms
