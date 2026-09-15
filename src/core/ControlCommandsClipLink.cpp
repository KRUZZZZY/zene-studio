/*
 * ControlCommandsClipLink.cpp - the clip.link_* commands: the linked / smart
 *                                clip relation (feature-list row 6, board task
 *                                #645).
 *
 * THE DECISION is recorded in include/ClipLinks.h (the engine half) and in
 * docs/LINKED-CLIPS.md: the relation is a persisted group id on each member
 * (`Clip::linkId()`, the `link` attribute of the clip's own element) plus a
 * write-through mirror of the clip's CONTENT - its note list. It is NOT a shared
 * content object, and NOT copy-on-write-with-detach: an edit to one member is
 * seen by all of them, and unlinking - the detach - is its own command
 * (clip.link_remove).
 *
 * THE GROUP'S NAME is `clip.`, not a new `linked.`/`smart.` group: the relation
 * is a property of a clip, every id here addresses clips, and an agent that
 * knows clip.trim finds clip.link_create beside it. It is NOT `link.*` - that
 * name is taken by session tempo/beat sync (docs/LINK-SYNC.md), a different
 * feature that happens to share the English word.
 *
 * WHAT PROPAGATES, stated here because the row asks for the one line: content
 * edits propagate. clip.link_create and clip.link_sync mirror the source's note
 * list to every member, and the note.* verbs (add / remove / move / resize /
 * velocity_set) mirror through the engine's own note entry points, so an edit to
 * one member IS the edit to all of them. Position, length, source offset, fades,
 * gain, mute, name, colour and take lane are PER-MEMBER and never propagate -
 * they describe where and how a member plays the shared content, which is what
 * makes a link a smart clip rather than a rename. See docs/LINKED-CLIPS.md §4
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
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClip.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

/*! The reason every mutating command in this file records: what a link IS is the
 *  clip's own serialized state, so the engine's own checkpoint is a live inverse
 *  and not a snapshot.
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
const QString ClauseClipLinkJournalled = QStringLiteral("ProjectJournal (Clip checkpoints: "
	"Clip::saveClipEdits writes the 'link' attribute onto a member's own element and "
	"Clip::loadClipEdits resets it to 0 when the attribute is absent, so the checkpoint taken "
	"before a FIRST link restores 'unlinked' exactly, and MidiClip::loadSettings clears and "
	"re-loads the note list that a mirror writes; one checkpoint covers every member written "
	"and the registry merges them into one undo step)");

/*! The clip-<n> id of \p clip, or an empty string when it is not in the song's
 *  arrangement (a clip of another container, mid-delete). Every id this group
 *  reports goes through here, so a member that is not addressable is reported as
 *  an empty id rather than a wrong one. */
QString clipIdOf(const Clip* clip)
{
	if (clip == nullptr) { return QString(); }
	for (const ClipRef& ref : enumerateClips())
	{
		if (ref.clip == clip) { return clipId(ref.ordinal); }
	}
	return QString();
}

//! The state a caller reads back: the clip's own membership plus its group.
QJsonObject membershipState(const Clip* clip)
{
	QJsonObject out;
	out.insert(QStringLiteral("clip"), clipIdOf(clip));
	out.insert(QStringLiteral("group"), clip == nullptr ? 0 : clip->linkId());
	return out;
}

//! One link group as clip.link_get_state reports it.
QJsonObject groupState(const QVector<Clip*>& members)
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
	for (Clip* member : members)
	{
		const QString id = clipIdOf(member);
		ids.append(id);
		if (auto* midi = dynamic_cast<MidiClip*>(member))
		{
			++contentMembers;
			// The group's FIRST member in arrangement order is the yardstick a
			// reader can reproduce (no hidden "leader" field exists): every member
			// is compared against it, and a member that differs is named in
			// `divergent` instead of being averaged away.
			if (ClipLinks::contentFingerprint(midi)
					== ClipLinks::contentFingerprint(members.first()))
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

/*! The members of \p clip's group, \p clip included, as ids - or empty when it
 *  is unlinked. */
QVector<Clip*> groupOf(const Clip* clip) { return ClipLinks::groupOf(clip); }

/*! Reads the `clips` argument: an array of clip ids, non-empty, no duplicates.
 *  Refuses typed and leaves \p out untouched otherwise. */
bool readClipList(const QJsonObject& args, QVector<QString>* out, ControlResult* error)
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
		if (!readClipList(args, &requested, &error)) { return error; }

		// Resolve and validate EVERY member before anything is written, so a
		// refusal cannot leave a half-built group behind (the same rule
		// clip.trim's planTrim follows).
		QVector<Clip*> newcomers;
		for (const QString& id : requested)
		{
			ClipRef ref;
			if (!resolveClip(id, &ref, &error)) { return error; }
			if (ref.clip == anchor.clip)
			{
				return ControlResult::failure(ControlErrorKind::InvalidArgs,
					QStringLiteral("'clips' names the anchor itself ('%1')").arg(id));
			}
			if (dynamic_cast<MidiClip*>(ref.clip) == nullptr)
			{
				return ControlResult::failure(ControlErrorKind::InvalidArgs,
					QStringLiteral("'%1' cannot be a link member: a link group shares a note "
						"list, and this clip has none (audio clips are their own source in "
						"0.3.0 - docs/KNOWN-LIMITATIONS.md)").arg(id));
			}
			if (ref.clip->linkId() == anchor.clip->linkId() && anchor.clip->linkId() > 0)
			{
				return ControlResult::failure(ControlErrorKind::InvalidArgs,
					QStringLiteral("'%1' is already a member of this link group").arg(id));
			}
			if (ref.clip->linkId() > 0)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("'%1' is already a member of link group %2; clip.link_remove "
						"it first - joining it here would merge two groups' content")
						.arg(id).arg(ref.clip->linkId()));
			}
			newcomers.append(ref.clip);
		}

		const int group = anchor.clip->linkId() > 0
			? anchor.clip->linkId()
			: ClipLinks::allocateGroupId();

		// The clips whose own serialized state this command changes: the anchor
		// when it was unlinked, plus every newcomer. ONE checkpoint, taken before
		// the first write.
		QVector<JournallingObject*> toCheckpoint;
		if (anchor.clip->linkId() <= 0) { toCheckpoint.append(anchor.clip); }
		for (Clip* newcomer : newcomers) { toCheckpoint.append(newcomer); }
		if (ProjectJournal* journal = Engine::projectJournal(); journal != nullptr)
		{
			journal->addJournalCheckPoint(toCheckpoint);
		}

		anchor.clip->setLinkId(group);
		for (Clip* newcomer : newcomers) { newcomer->setLinkId(group); }

		// The group starts in sync: the members adopt the anchor's content. The
		// mirror takes its own checkpoint over the members it rewrites, and the
		// registry merges both into this command's one undo step.
		const ClipLinks::MirrorReport mirror = ClipLinks::mirrorContent(anchor.clip);

		QJsonObject result = groupState(ClipLinks::group(group));
		result.insert(QStringLiteral("adopted"), [&]() {
			QJsonArray adopted;
			for (Clip* newcomer : newcomers) { adopted.append(clipIdOf(newcomer)); }
			return adopted;
		}());
		result.insert(QStringLiteral("mirrored"), mirror.written);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), clipIdOf(anchor.clip));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(membershipState(anchor.clip),
				QStringLiteral("clip.link_remove"), inverseArgs, true,
				ClauseClipLinkJournalled));
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
		const QVector<Clip*> members = groupOf(ref.clip);
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
		result.insert(QStringLiteral("removed"), clipIdOf(ref.clip));
		result.insert(QStringLiteral("group"), group);
		QJsonArray ids;
		for (Clip* member : remaining) { ids.append(clipIdOf(member)); }
		result.insert(QStringLiteral("remaining"), ids);
		result.insert(QStringLiteral("dissolved"), dissolved);
		/* The inverse of an unlink is the link itself: the removed clip rejoins
		 * the group it left, which `clip.link_create` with the remaining member
		 * as the anchor's content source performs. `applies: journal` is what
		 * control.undo actually uses - the tag is part of each clip's checkpointed
		 * state - and this inverse names the same operation for a reader and for a
		 * client replaying the transaction record. */
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), clipIdOf(remaining.isEmpty()
			? ref.clip : remaining.first()));
		QJsonArray inverseClips;
		inverseClips.append(clipIdOf(ref.clip));
		inverseArgs.insert(QStringLiteral("clips"), inverseClips);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(membershipState(ref.clip), QStringLiteral("clip.link_create"),
				inverseArgs, true, ClauseClipLinkJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipLinkGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.link_get_state");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("link_get_state");
	cmd.description = QStringLiteral("Read the link groups: every group with its members, the "
		"content channel it shares ('notes'), the note count, which member is the reference "
		"(the group's first member in arrangement order) and - per member - whether its content "
		"still matches the reference (`in_sync`) or has drifted (`divergent`). With 'clip', the "
		"report is that clip's group only; without it, every group in the song. Read-only.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("groups"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("linked"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		QJsonArray groups;
		if (args.contains(QStringLiteral("clip")))
		{
			ControlResult error;
			ClipRef ref;
			if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
			{
				return error;
			}
			const QVector<Clip*> members = groupOf(ref.clip);
			if (!members.isEmpty()) { groups.append(groupState(members)); }
			QJsonObject result;
			result.insert(QStringLiteral("clip"), clipIdOf(ref.clip));
			result.insert(QStringLiteral("linked"), ref.clip->linkId() > 0);
			result.insert(QStringLiteral("group"), ref.clip->linkId());
			result.insert(QStringLiteral("count"), groups.size());
			result.insert(QStringLiteral("groups"), groups);
			return ControlResult::success(result);
		}

		// Every group of the song, in the order its first member is addressed.
		QVector<int> seen;
		for (const ClipRef& ref : enumerateClips())
		{
			const int id = ref.clip->linkId();
			if (id <= 0 || seen.contains(id)) { continue; }
			seen.append(id);
			groups.append(groupState(ClipLinks::group(id)));
		}
		QJsonObject result;
		result.insert(QStringLiteral("count"), groups.size());
		result.insert(QStringLiteral("groups"), groups);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipLinkSync(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.link_sync");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("link_sync");
	cmd.description = QStringLiteral("Force the group to agree: every other member of 'clip'\"s "
		"link group is given this clip's content, and the members that had to be rewritten are "
		"named in `written`. An edit through the note.* verbs already propagates on its own - "
		"this is the REPAIR verb for the edit kinds that do not (a piano-roll gesture the "
		"engine's note entry points do not see, a hand-edited project file), and it is what "
		"makes a disagreement fixable instead of permanent. Reversible through the "
		"ProjectJournal (one Clip checkpoint per member written).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("group"), integerProperty()},
		{QStringLiteral("members"), arrayProperty()},
		{QStringLiteral("divergent_before"), arrayProperty()},
		{QStringLiteral("written_count"), integerProperty()},
		{QStringLiteral("content"), stringProperty()},
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
				QStringLiteral("'%1' is not a member of a link group: there is nothing to sync")
					.arg(args.value(QStringLiteral("clip")).toString()));
		}
		if (dynamic_cast<MidiClip*>(ref.clip) == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' has no note list, so it cannot be a group's content source")
					.arg(args.value(QStringLiteral("clip")).toString()));
		}

		// Who disagrees BEFORE the write: the answer a caller needs in order to
		// tell "this repaired a drift" from "this was already in sync".
		const QString wanted = ClipLinks::contentFingerprint(ref.clip);
		QJsonArray before;
		QJsonArray members;
		for (Clip* member : groupOf(ref.clip))
		{
			members.append(clipIdOf(member));
			if (member != ref.clip && ClipLinks::contentFingerprint(member) != wanted)
			{
				before.append(clipIdOf(member));
			}
		}

		const ClipLinks::MirrorReport mirror = ClipLinks::mirrorContent(ref.clip);

		QJsonObject result;
		result.insert(QStringLiteral("clip"), clipIdOf(ref.clip));
		result.insert(QStringLiteral("group"), ref.clip->linkId());
		result.insert(QStringLiteral("members"), members);
		result.insert(QStringLiteral("divergent_before"), before);
		result.insert(QStringLiteral("written_count"), mirror.written);
		result.insert(QStringLiteral("content"), QStringLiteral("notes"));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(membershipState(ref.clip), QStringLiteral("clip.link_sync"),
				QJsonObject{{QStringLiteral("clip"), clipIdOf(ref.clip)}}, true,
				ClauseClipLinkJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipLinkCommands(ControlRegistry& registry)
{
	registerClipLinkCreate(registry);
	registerClipLinkRemove(registry);
	registerClipLinkGetState(registry);
	registerClipLinkSync(registry);
}

} // namespace lmms
