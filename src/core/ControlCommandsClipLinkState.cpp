/*
 * ControlCommandsClipLinkState.cpp - the clip.link_* group's READ half and its
 *                                     repair verb: clip.link_get_state and
 *                                     clip.link_sync (linked / smart clips,
 *                                     feature-list row 6, board task #645).
 *
 * WHY THE GROUP IS SPLIT. The two commands that create and break the relation are
 * ControlCommandsClipLink.cpp; this file is the one that reads a group back and
 * the one that makes a disagreeing group agree again. The file-length ratchet is
 * not moved for a new feature (the same reason clip.trim/clip.slip got their own
 * translation unit), so the group is two TUs and both register through
 * registerClipLinkCommands() (the writing half's file) and
 * registerClipLinkStateCommands() below. The helpers the two halves share are
 * include/ControlClipLinkSupport.h.
 *
 * WHY A SYNC VERB EXISTS AT ALL, since the note.* verbs already propagate: an
 * edit can reach a clip's note list without passing the entry points the engine
 * mirrors at (a piano-roll in-place gesture, a hand-edited project file, a Lua
 * edit), and a disagreement that a user cannot see or repair is worse than one
 * that is reported. clip.link_get_state names the divergent members and
 * clip.link_sync repairs them, in one undoable step.
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
			const QVector<Clip*> members = cliplink::groupOf(ref.clip);
			if (!members.isEmpty()) { groups.append(cliplink::groupState(members)); }
			QJsonObject result;
			result.insert(QStringLiteral("clip"), cliplink::clipIdOf(ref.clip));
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
			groups.append(cliplink::groupState(ClipLinks::group(id)));
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
		for (Clip* member : cliplink::groupOf(ref.clip))
		{
			members.append(cliplink::clipIdOf(member));
			if (member != ref.clip && ClipLinks::contentFingerprint(member) != wanted)
			{
				before.append(cliplink::clipIdOf(member));
			}
		}

		const ClipLinks::MirrorReport mirror = ClipLinks::mirrorContent(ref.clip);

		QJsonObject result;
		result.insert(QStringLiteral("clip"), cliplink::clipIdOf(ref.clip));
		result.insert(QStringLiteral("group"), ref.clip->linkId());
		result.insert(QStringLiteral("members"), members);
		result.insert(QStringLiteral("divergent_before"), before);
		result.insert(QStringLiteral("written_count"), mirror.written);
		result.insert(QStringLiteral("content"), QStringLiteral("notes"));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(cliplink::membershipState(ref.clip), QStringLiteral("clip.link_sync"),
				QJsonObject{{QStringLiteral("clip"), cliplink::clipIdOf(ref.clip)}}, true,
				cliplink::ClauseClipLinkJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


} // namespace

void registerClipLinkStateCommands(ControlRegistry& registry)
{
	registerClipLinkGetState(registry);
	registerClipLinkSync(registry);
}

} // namespace lmms
