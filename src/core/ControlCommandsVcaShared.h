/*
 * ControlCommandsVcaShared.h - what the three translation units of the `vca.*`
 *                              command group share (SPEC A11-A16; the 0.3.0
 *                              ladder's OWNER-31 item 11, "phase-locked
 *                              multitrack edit groups").
 *
 * Why this header exists.
 *
 * The group is three files because of the 500-line file ratchet, exactly as the
 * folder-track, session, warp, rack, comp and automation groups are split:
 *
 *   ControlCommandsVca.cpp      the GROUP + ENTITY half: create, remove, list,
 *                               get_state, rename (and the registration point).
 *   ControlCommandsVcaMix.cpp   the MIX half: set_gain, set_mute, set_solo,
 *                               assign, unassign - one fader over member
 *                               mixer channels.
 *   ControlCommandsVcaEdit.cpp  the EDIT half: set_phase_lock, track_add,
 *                               track_remove, edit_move - the phase-locked
 *                               multitrack edit itself.
 *
 * All three report a GROUP and the two sets it holds (member channels and edit
 * tracks), so the two helpers that turn that into the wire shape - the typed
 * `vca-<n>` resolver and groupState() - are the group's, not one file's. The
 * alternative this codebase has already paid for four times is each half
 * carrying its own copy of the same rule (ControlVocabulary.cpp's header records
 * the duplicate-symbol link failure that ends that way).
 *
 * The id form is `vca-<n>`, where n is the group's own stable id
 * (`VcaGroup::id()`, the `id` attribute of its `<vcagroup>` element, preserved
 * across a save/load round trip). That is the grammar every other addressable
 * object in this surface already uses - `ch-<n>`, `trk-<n>`, `clip-<n>`,
 * `dev-<n>` - and it is chosen for the same reason those were: the id a caller
 * holds stays valid after a sibling is created or removed, so an agent never
 * has to re-derive an address from a position. The formatter lives here rather
 * than in ControlVocabulary.h because this group is its only producer and its
 * only consumer; it is `inline` in a named namespace, so every translation unit
 * that includes this header sees one definition.
 *
 * Nothing is exported: `inline` definitions in a named namespace, so no
 * translation unit depends on another's static initialisation order.
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

#ifndef LMMS_CONTROL_COMMANDS_VCA_SHARED_H
#define LMMS_CONTROL_COMMANDS_VCA_SHARED_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Clip.h"            // control::clipState()'s object, and a moved clip's own state
#include "ControlEdit.h"       // control::resolveTrack()
#include "ControlRegistry.h"   // ControlResult, ControlErrorKind
#include "ControlVocabulary.h" // control::channelIdOf(), control::trackId(), idToIndex()
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"
#include "VcaGroup.h"

namespace lmms
{
namespace vcacontrol
{

//! `vca-<n>`, the group's own stable id in wire form.
inline QString vcaGroupId(int id)
{
	return QStringLiteral("vca-%1").arg(id);
}

/*! Resolve the `group` argument of a `vca.*` command against the live mixer.
 *
 *  A malformed id (`vcagroup0`, `3`, an empty string) is a typed InvalidArgs and
 *  a well-formed id naming no group is a typed NotFound - never a silent
 *  success, and never a fallback to "the first group", which would make an
 *  agent's second call edit something it did not name. The same discipline
 *  `control::resolveTrack` applies to `trk-<n>`.
 */
inline VcaGroup* resolveGroup(const QJsonObject& args, ControlResult* error)
{
	const QString id = args.value(QStringLiteral("group")).toString();
	const int wanted = control::idToIndex(id, QStringLiteral("vca-"));
	if (wanted < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a group id of the form vca-<n>").arg(id));
		return nullptr;
	}
	VcaGroup* group = Engine::mixer()->vcaGroup(wanted);
	if (group == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no VCA group %1 (the mix has %2)")
				.arg(id).arg(Engine::mixer()->vcaGroups().size()));
		return nullptr;
	}
	return group;
}

/*! One group's whole state, as every command of this group reports it.
 *
 *  Two sets, reported as data rather than as a count: `members` (the mixer
 *  channels the fader scales, each with the gain published into it right now -
 *  the number the audio path actually uses) and `tracks` (the edit set, by
 *  stable `trk-<n>` id). A track id that names no live track is reported in
 *  `missing_tracks` rather than dropped from `tracks`: the group does hold it
 *  (a delete does not rewrite a group's membership - see include/VcaGroup.h),
 *  and an agent that cannot see the difference between "not a member" and
 *  "a member whose track is gone" cannot repair either.
 */
inline QJsonObject groupState(VcaGroup* group)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), vcaGroupId(group->id()));
	out.insert(QStringLiteral("name"), group->name());
	out.insert(QStringLiteral("gain"), static_cast<double>(group->gain()));
	out.insert(QStringLiteral("volume"), static_cast<double>(group->vcaModel()->value()));
	out.insert(QStringLiteral("muted"), group->muteModel()->value());
	out.insert(QStringLiteral("soloed"), group->soloModel()->value());
	out.insert(QStringLiteral("phase_locked"), group->isPhaseLocked());

	QJsonArray members;
	for (mix_ch_t member : group->members())
	{
		QJsonObject entry;
		MixerChannel* channel = Engine::mixer()->mixerChannel(member);
		// The channel's PERSISTENT id (SPEC-stable-ids.md slice 2), read off
		// the object the position names: the membership itself is stored as
		// positions (VcaGroup::members() is a list of mix_ch_t, and
		// Mixer::refreshGroups walks channels by position), but the id a
		// caller is handed is the channel's OWN, so it still names that
		// channel after a sibling channel is deleted.
		entry.insert(QStringLiteral("channel"), control::channelIdOf(channel));
		entry.insert(QStringLiteral("gain_for_member"),
			static_cast<double>(channel->vcaGain()));
		members.append(entry);
	}
	out.insert(QStringLiteral("members"), members);
	out.insert(QStringLiteral("member_count"), members.size());

	QJsonArray tracks;
	QJsonArray missing;
	for (int trackId : group->editTracks())
	{
		const QString id = control::trackId(trackId);
		tracks.append(id);
		ControlResult ignored;
		if (control::resolveTrack(id, &ignored) == nullptr)
		{
			missing.append(id);
		}
	}
	out.insert(QStringLiteral("tracks"), tracks);
	out.insert(QStringLiteral("track_count"), tracks.size());
	out.insert(QStringLiteral("missing_tracks"), missing);
	out.insert(QStringLiteral("missing_count"), missing.size());
	return out;
}

/*! Apply \p edit to EVERY track of the group's edit set, phase-locked.
 *
 *  The correspondence rule, stated once and applied by every edit verb: the
 *  clip the caller named is the ANCHOR and moves to exactly the position asked
 *  for; every other member track's clips that OVERLAP the anchor's
 *  pre-command span move by the SAME DELTA. Then all of them, anchor included,
 *  are one undo step.
 *
 *  A member with no clip in that span is not an error - it has nothing to lock
 *  to the anchor, which is the normal case for a take that is missing on one
 *  input - and it is reported in `unlocked_tracks` rather than silently
 *  counted as a success. A member whose track is gone is reported in
 *  `skipped_tracks`.
 *
 *  The delta is applied as a delta, not as "move everything to the anchor's new
 *  position": two members deliberately offset by a few ticks stay offset, which
 *  is what makes this a LOCK and not a "snap every member onto one grid line"
 *  (the semantics the register warns are easy to get wrong, DAW-GAP §5 rank 6).
 */
struct LockedEditResult
{
	QJsonArray moves;            //!< {track, clip, from, to} per clip moved
	QJsonArray unlockedTracks;   //!< member tracks with nothing in the span
	QJsonArray skippedTracks;    //!< member tracks that no longer exist
	int checkpoints = 0;         //!< live Clip checkpoints taken (one per move)
};

//! The anchor clip's pre-command span, and the delta a caller asked for.
struct LockedEdit
{
	int anchorTrackId = -1;
	tick_t spanStart = 0;
	tick_t spanEnd = 0;
	QString op;                  //!< the command id, for the refusal text
};

//! One track's clips that overlap [spanStart, spanEnd), in clip order.
inline std::vector<Clip*> clipsInSpan(Track* track, tick_t spanStart, tick_t spanEnd)
{
	std::vector<Clip*> found;
	for (Clip* clip : track->getClips())
	{
		const tick_t start = clip->startPosition().getTicks();
		const tick_t end = clip->endPosition().getTicks();
		if (end > spanStart && start < spanEnd) { found.push_back(clip); }
	}
	return found;
}

//! Move one clip by \p delta and record it. Returns the checkpoint count (1).
inline int moveLockedClip(LockedEditResult* out, Track* track, Clip* clip, tick_t delta)
{
	const tick_t from = clip->startPosition().getTicks();
	// SPEC A16: a live Clip checkpoint, taken BEFORE the write, is a real
	// inverse - the clip's own serialized state carries its position. The
	// registry merges every checkpoint one command pushes into ONE undo step
	// (ControlRegistry::runHandler), which is what makes a many-track lock one
	// Ctrl+Z and not one per member.
	clip->addJournalCheckPoint();
	clip->movePosition(TimePos(from + delta));

	QJsonObject move;
	move.insert(QStringLiteral("track"), control::trackIdOf(track));
	move.insert(QStringLiteral("from"), from);
	move.insert(QStringLiteral("to"), clip->startPosition().getTicks());
	out->moves.append(move);
	++out->checkpoints;
	return out->checkpoints;
}

/*! Resolve the `channel` argument of vca.assign / vca.unassign.
 *
 *  `ch-<n>` is the mixer's own grammar and the number in it is the channel's
 *  PERSISTENT id (MixerChannel::id(), SPEC-stable-ids.md slice 2) - the same
 *  number mixer.get_state reports - so the id form is not this group's to
 *  invent; only the REFUSALS are, and they say what a GROUP is allowed to hold
 *  rather than what a mixer channel is: a group never takes master (the mix bus
 *  is not a member of anything) and never takes a channel that already belongs
 *  to another group. The second rule is VcaGroup::addMember's own ("a channel is
 *  in at most one group, so 'soloing a member' has exactly one meaning") and is
 *  surfaced here as a typed refusal that NAMES the group holding it, because an
 *  agent can only fix that by unassigning it there.
 */
inline MixerChannel* memberChannel(const QJsonObject& args, VcaGroup* group, ControlResult* error)
{
	Mixer* mixer = Engine::mixer();
	const QString id = args.value(QStringLiteral("channel")).toString();
	const int wanted = control::idToIndex(id, QStringLiteral("ch-"));
	if (wanted < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a channel id of the form ch-<n>").arg(id));
		return nullptr;
	}
	// By id, not by position (SPEC-stable-ids.md slice 2): the number names the
	// channel OBJECT (MixerChannel::id()), which is what the rest of the mixer
	// surface now reports, so the channel a vca.assign names is the channel the
	// caller read. No positional fallback - every channel carries an id from
	// construction, so one could only ever resolve a stale position.
	MixerChannel* channel = nullptr;
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* candidate = mixer->mixerChannel(i);
		if (candidate != nullptr && candidate->id() == wanted)
		{
			channel = candidate;
			break;
		}
	}
	if (channel == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no mixer channel %1 (the mixer has %2)")
				.arg(id).arg(static_cast<int>(mixer->numChannels())));
		return nullptr;
	}
	// Master is refused by IDENTITY (isMaster() is the channel's position 0,
	// the mix bus itself) and not by the number read off the wire: the master's
	// id is whatever the project's counter allocated at its construction - in a
	// fresh session measured on the shipped binary it is ch-1, not ch-0, and an
	// id is not a position. The refusal names the channel the caller addressed,
	// read off the object it resolved to.
	if (channel->isMaster())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is the master channel and cannot be a group member: a group "
				"scales the channels that feed the master").arg(control::channelIdOf(channel)));
		return nullptr;
	}
	// The membership itself stays the group's own bookkeeping, which is a
	// channel INDEX (VcaGroup::members() is a list of mix_ch_t positions, and
	// Mixer::refreshGroups walks the channels by position).
	const mix_ch_t channelIndex = static_cast<mix_ch_t>(channel->index());
	if (group != nullptr && !group->contains(channelIndex))
	{
		if (VcaGroup* holder = mixer->vcaGroupForChannel(channelIndex))
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 already belongs to %2; a channel is in at most one group, "
					"so unassign it there first")
					.arg(id, vcaGroupId(holder->id())));
			return nullptr;
		}
	}
	return channel;
}

} // namespace vcacontrol
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_VCA_SHARED_H
