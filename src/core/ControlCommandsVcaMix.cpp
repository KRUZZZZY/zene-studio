/*
 * ControlCommandsVcaMix.cpp - the `vca.*` group's MIX half: vca.set_gain,
 *                             vca.set_mute, vca.set_solo, vca.assign and
 *                             vca.unassign (SPEC A11-A16; the mix half is task
 *                             #622's entity, the 0.3.0 ladder's OWNER-31 item
 *                             11 carries both halves in one row).
 *
 * What these five do is the VCA semantics the register warns are the easy part
 * to get wrong: a group's fader SCALES its members rather than setting them
 * (VcaGroup publishes the factor as MixerChannel::m_vcaGain and never writes a
 * member's own fader), so moving the group fader and moving it back leaves every
 * member model bit-identical; mute is the same factor at zero rather than the
 * channel's mute model, so a muted group stays silent even for a soloed member;
 * and solo is the product's own exclusive solo applied to the group's members
 * (Mixer::applyGroupSolo), not a second solo concept.
 *
 * The other two halves of the group are in ControlCommandsVca.cpp (create /
 * remove / list / get_state / rename) and ControlCommandsVcaEdit.cpp (the
 * phase-locked edit set). Id form, helpers and the split's reason: see
 * ControlCommandsVcaShared.h.
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

#include <QJsonObject>
#include <QVector>

#include "ControlCommandsVcaShared.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "JournallingObject.h"

namespace lmms
{

using namespace control;      // the shared vocabulary lives in ControlVocabulary.h
using namespace vcacontrol;   // this group's helpers

namespace
{

//! The fader range this surface accepts, matching MixerChannel's own model:
//! 1.0 is unity and 2.0 is +6 dB. A fader outside it is a refusal, not a clamp
//! (SPEC A11: typed, never silently adjusted).
constexpr double kMaxGain = 2.0;

// ---------------------------------------------------------------------------
// vca.set_gain - the group fader.
// ---------------------------------------------------------------------------
ControlResult setGain(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	const double gain = args.value(QStringLiteral("gain")).toDouble();
	if (gain < 0.0 || gain > kMaxGain)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("gain %1 is outside 0..%2 (1.0 is unity, 2.0 is +6 dB)")
				.arg(gain).arg(kMaxGain));
	}

	const float previous = group->vcaModel()->value();
	// SPEC A16: the fader IS an AutomatableModel and therefore a
	// JournallingObject with an id of its own, so a checkpoint taken before the
	// write restores it exactly - the same mechanism mixer.set_volume uses for
	// a channel fader. Nothing else is written: the members' own faders are
	// never touched, only the gain published from this one
	// (VcaGroup's own dataChanged hook -> Mixer::refreshGroups).
	group->vcaModel()->addJournalCheckPoint();
	group->vcaModel()->setValue(static_cast<float>(gain));

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("previous_gain"),
		static_cast<double>(previous));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("volume"), static_cast<double>(previous)}},
			QStringLiteral("vca.set_gain"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("gain"), static_cast<double>(previous)}},
			true,
			QStringLiteral("ProjectJournal (VcaGroup fader FloatModel checkpoint: the group's "
				"own <vcagroup> element carries `vca`, and the members are never written - the "
				"factor is published, not stored on them)")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// vca.set_mute
// ---------------------------------------------------------------------------
ControlResult setMute(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	const bool muted = args.value(QStringLiteral("muted")).toBool();
	const bool previous = group->muteModel()->value();
	group->muteModel()->addJournalCheckPoint();
	group->muteModel()->setValue(muted);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("muted"), previous}},
			QStringLiteral("vca.set_mute"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("muted"), previous}},
			true,
			QStringLiteral("ProjectJournal (VcaGroup mute BoolModel checkpoint: the group's own "
				"<vcagroup> element carries `muted`). Mute is folded into the published gain "
				"rather than written into any member's own mute model, so a member's mute state "
				"survives a group mute/unmute pair unchanged")));
	return ControlResult::success(result);
}

/*! The composite checkpoint vca.set_solo needs.
 *
 *  Soloing a group is NOT one write: Mixer::applyGroupSolo clears any other
 *  group's solo flag (exclusive solo, exactly like a channel's), saves every
 *  channel's mute state and mutes them all, then unmutes the group's members and
 *  whatever they send to or receive from. That is N models across two
 *  containers, so a single-object checkpoint would make one agent command cost N
 *  Ctrl+Z presses - the defect SPEC A16 deliverable 3 exists to remove. The same
 *  shape, for the same reason, as `track.set_solo` in
 *  ControlCommandsArrangement.cpp.
 */
QVector<JournallingObject*> soloStepObjects()
{
	QVector<JournallingObject*> step;
	for (VcaGroup* other : Engine::mixer()->vcaGroups())
	{
		step.append(other->soloModel());
		step.append(other->muteModel());
	}
	const int channels = static_cast<int>(Engine::mixer()->numChannels());
	for (int ch = 0; ch < channels; ++ch)
	{
		step.append(Engine::mixer()->mixerChannel(ch)->m_muteModel);
	}
	return step;
}

ControlResult setSolo(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	const bool soloed = args.value(QStringLiteral("soloed")).toBool();
	const bool previous = group->soloModel()->value();

	// One step for the whole action, taken BEFORE it: see soloStepObjects.
	control::addUndoStep(soloStepObjects());
	// The model write is the whole action: VcaGroup's constructor connects the
	// solo model's dataChanged straight to Mixer::applyGroupSolo (a direct
	// connection), so the member/solo/mute pass happens here whether or not
	// there is a display. A GUI instance needs no second call - unlike
	// track.set_solo, where the cross-track mute lives in a VIEW and therefore
	// has to be applied by hand when there is none.
	group->soloModel()->setValue(soloed);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("previous_soloed"), previous);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("soloed"), previous}},
			QStringLiteral("vca.set_solo"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("soloed"), previous}},
			true,
			QStringLiteral("composite checkpoint: the group's own solo flag, every other "
				"group's solo flag and every mixer channel's mute model are recorded as ONE "
				"undo step, so one control.undo or one Ctrl+Z restores the whole action "
				"(Mixer::applyGroupSolo is exclusive and rewrites the channels' mutes). LIMIT: "
				"MixerChannel::m_muteBeforeSolo is transient and is not part of the project "
				"file, so it is not restored - it is re-derived on the next solo action, "
				"exactly as track.set_solo states for Track::mutedBeforeSolo")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// vca.assign / vca.unassign - the mix membership.
// ---------------------------------------------------------------------------
ControlResult assignChannel(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	MixerChannel* channel = memberChannel(args, group, &error);
	if (channel == nullptr)
	{
		return error;
	}
	const int index = channel->index();
	if (group->contains(static_cast<mix_ch_t>(index)))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already a member of %2")
				.arg(control::channelId(index), vcaGroupId(group->id())));
	}

	const int id = group->id();
	auto mixerRef = Engine::mixer();
	const mix_ch_t member = static_cast<mix_ch_t>(index);
	// The membership is a list of channel indices on the group; a MixerChannel
	// has no back-reference a checkpoint could restore, so the inverse is the
	// recorded OPERATION that removes the member this call adds - and the
	// published gain follows it back to unity, because removeMember calls
	// Mixer::refreshGroups.
	control::addUndoStep(
		[mixerRef, id, member]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->removeMember(member); }
		},
		[mixerRef, id, member]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->addMember(member); }
		});
	group->addMember(member);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("assigned"), control::channelId(index));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelId(index)},
				{QStringLiteral("assigned"), false}},
			QStringLiteral("vca.unassign"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelId(index)}},
			true,
			QStringLiteral("action checkpoint: group membership is a list of channel indices "
				"on the group and a MixerChannel keeps no back-reference, so the recorded undo "
				"step removes the member this command added, through the same "
				"VcaGroup::removeMember call the write uses - which also republishes the "
				"channel's vca gain back to unity")));
	return ControlResult::success(result);
}

ControlResult unassignChannel(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr)
	{
		return error;
	}
	MixerChannel* channel = memberChannel(args, group, &error);
	if (channel == nullptr)
	{
		return error;
	}
	const int index = channel->index();
	if (!group->contains(static_cast<mix_ch_t>(index)))
	{
		// A well-formed channel that is simply not a member of THIS group is a
		// refusal rather than a silent success, the same rule the shape-specific
		// resolvers apply everywhere else: a command that reported success for
		// removing something that was never there hides a wrong id.
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not a member of %2")
				.arg(control::channelId(index), vcaGroupId(group->id())));
	}

	const int id = group->id();
	auto mixerRef = Engine::mixer();
	const mix_ch_t member = static_cast<mix_ch_t>(index);
	control::addUndoStep(
		[mixerRef, id, member]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->addMember(member); }
		},
		[mixerRef, id, member]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->removeMember(member); }
		});
	group->removeMember(member);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("unassigned"), control::channelId(index));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelId(index)},
				{QStringLiteral("assigned"), true}},
			QStringLiteral("vca.assign"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelId(index)}},
			true,
			QStringLiteral("action checkpoint: the recorded undo step puts the channel back "
				"through the same VcaGroup::addMember call the write path uses, which "
				"republishes the group's gain into it. The channel keeps its own fader "
				"throughout - a group scales, it never assigns a value")));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// The registrations.
// ---------------------------------------------------------------------------
void registerVcaSetGain(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.set_gain");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("set_gain");
	cmd.description = QStringLiteral("Set a group's fader (0..2, 1.0 is unity and 2.0 is +6 dB). "
		"The fader SCALES every member channel relative to the channel's own fader - no member "
		"model is written - so moving it and moving it back leaves every member bit-identical, "
		"and a member keeps the value the user set for it. The result reports the gain actually "
		"published to the members, which is 0 while the group is muted. Reversible through the "
		"ProjectJournal (the group's fader model checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("gain"), control::numberProperty()},
	}, {QStringLiteral("group"), QStringLiteral("gain")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("gain"), control::numberProperty()},
		{QStringLiteral("volume"), control::numberProperty()},
		{QStringLiteral("previous_gain"), control::numberProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setGain(args); };
	registry.registerCommand(cmd);
}

void registerVcaSetMute(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.set_mute");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("set_mute");
	cmd.description = QStringLiteral("Mute or unmute a group. Mute is a gain of zero published "
		"from the group, NOT the members' own mute models - so muting a group silences every "
		"member, a soloed member of a muted group stays silent, and each member's own mute state "
		"survives the pair untouched. Reversible through the ProjectJournal (the group's mute "
		"model checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
	}, {QStringLiteral("group"), QStringLiteral("muted")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("gain"), control::numberProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setMute(args); };
	registry.registerCommand(cmd);
}

void registerVcaSetSolo(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.set_solo");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("set_solo");
	cmd.description = QStringLiteral("Solo or unsolo a group. Soloing a group makes exactly its "
		"members audible (the same exclusive solo a channel has, so soloing a group clears any "
		"other group's), and clearing the flag restores the pre-solo mute state of every "
		"channel. Reversible: the whole action - the flag, every other group's flag and every "
		"channel's mute - is one recorded undo step.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("soloed"), control::booleanProperty()},
	}, {QStringLiteral("group"), QStringLiteral("soloed")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("soloed"), control::booleanProperty()},
		{QStringLiteral("previous_soloed"), control::booleanProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setSolo(args); };
	registry.registerCommand(cmd);
}

void registerVcaAssign(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.assign");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("assign");
	cmd.description = QStringLiteral("Put a mixer channel into a group, so the group's fader "
		"scales it. Refused, typed, for ch-0 (master), for a channel already in this group, and "
		"for a channel that belongs to ANOTHER group - a channel is in at most one, which is "
		"what keeps 'soloing a member' single-valued; the refusal names the group holding it. "
		"Reversible through the ProjectJournal (a recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("channel"), control::stringProperty()},
	}, {QStringLiteral("group"), QStringLiteral("channel")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("assigned"), control::stringProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
		{QStringLiteral("member_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return assignChannel(args); };
	registry.registerCommand(cmd);
}

void registerVcaUnassign(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("vca.unassign");
	cmd.group = QStringLiteral("vca");
	cmd.verb = QStringLiteral("unassign");
	cmd.description = QStringLiteral("Take a mixer channel out of a group. The channel plays at "
		"its own fader again (its published gain returns to unity) and keeps that fader's value "
		"throughout. A channel that is not a member of this group is REFUSED rather than "
		"silently accepted, so a wrong id cannot look like a success. Reversible through the "
		"ProjectJournal (a recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("channel"), control::stringProperty()},
	}, {QStringLiteral("group"), QStringLiteral("channel")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("id"), control::stringProperty()},
		{QStringLiteral("unassigned"), control::stringProperty()},
		{QStringLiteral("members"), control::arrayProperty()},
		{QStringLiteral("member_count"), control::integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return unassignChannel(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerVcaMixCommands(ControlRegistry& registry)
{
	registerVcaSetGain(registry);
	registerVcaSetMute(registry);
	registerVcaSetSolo(registry);
	registerVcaAssign(registry);
	registerVcaUnassign(registry);
}

} // namespace lmms
