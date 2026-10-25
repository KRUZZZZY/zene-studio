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

#include <cmath>

#include <QJsonDocument>
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

/*! Whether \a channel is a member of a group OTHER than \a own - the
 *  multiple-membership (`also:true`, 0.5.0 L09) state, which is what makes the
 *  re-add in `vca.unassign`'s undo a legal one.
 *
 *  Mixer::vcaGroupForChannel answers only "some group holds it", which is not
 *  enough here: the group being edited always holds the channel at that point,
 *  so it would report true for EVERY unassign. This asks the other direction -
 *  is there a SECOND holder - by walking the groups the mixer owns.
 */
bool sharedElsewhere(Mixer* mixer, VcaGroup* own, mix_ch_t channel)
{
	for (VcaGroup* other : mixer->vcaGroups())
	{
		if (other != own && other->contains(channel)) { return true; }
	}
	return false;
}

//! The complete solo state carried by a vca.set_solo transaction inverse.
QJsonObject soloState(Mixer* mixer)
{
	QJsonArray groups;
	for (VcaGroup* group : mixer->vcaGroups())
	{
		groups.append(QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
			{QStringLiteral("soloed"), group->soloModel()->value()}});
	}
	QJsonArray channels;
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		channels.append(QJsonObject{{QStringLiteral("channel"), control::channelIdOf(channel)},
			{QStringLiteral("muted"), channel->m_muteModel.value()},
			{QStringLiteral("mute_before_solo"), channel->m_muteBeforeSolo}});
	}
	return QJsonObject{{QStringLiteral("groups"), groups}, {QStringLiteral("channels"), channels}};
}

//! Resolve the stable channel id in a solo-state record against the live mix.
MixerChannel* channelById(Mixer* mixer, const QString& id)
{
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		if (control::channelIdOf(channel) == id) { return channel; }
	}
	return nullptr;
}

struct SoloMuteCacheEntry
{
	QString channel;
	bool muted;
};

using SoloMuteCache = QVector<SoloMuteCacheEntry>;

//! Capture the transient pre-solo latch by stable channel id for journal undo.
SoloMuteCache muteBeforeSoloCache(Mixer* mixer)
{
	SoloMuteCache cache;
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		cache.append({control::channelIdOf(channel), channel->m_muteBeforeSolo});
	}
	return cache;
}

//! Restore the cache values after the journal has restored model checkpoints.
void restoreMuteBeforeSoloCache(Mixer* mixer, const SoloMuteCache& cache)
{
	for (const SoloMuteCacheEntry& entry : cache)
	{
		if (MixerChannel* channel = channelById(mixer, entry.channel))
		{
			channel->m_muteBeforeSolo = entry.muted;
		}
	}
}

//! The structural action budget includes both captured caches and their ids.
QJsonArray muteBeforeSoloJson(const SoloMuteCache& cache)
{
	QJsonArray values;
	for (const SoloMuteCacheEntry& entry : cache)
	{
		values.append(QJsonObject{{QStringLiteral("channel"), entry.channel},
			{QStringLiteral("muted"), entry.muted}});
	}
	return values;
}

//! Budget the structural action by the exact serialized before/after cache.
qint64 soloMuteCacheBytes(const SoloMuteCache& before, const SoloMuteCache& after)
{
	return QJsonDocument(QJsonObject{{QStringLiteral("before"), muteBeforeSoloJson(before)},
		{QStringLiteral("after"), muteBeforeSoloJson(after)}})
		.toJson(QJsonDocument::Compact).size();
}

VcaGroup* groupFromSoloState(Mixer* mixer, const QJsonObject& record)
{
	return mixer->vcaGroup(control::idToIndex(record.value(QStringLiteral("group")).toString(),
		QStringLiteral("vca-")));
}

VcaGroup* activeSoloFromState(Mixer* mixer, const QJsonArray& groupStates)
{
	VcaGroup* activeSolo = nullptr;
	for (const QJsonValue& value : groupStates)
	{
		const QJsonObject record = value.toObject();
		if (!record.value(QStringLiteral("soloed")).toBool()) { continue; }
		VcaGroup* group = groupFromSoloState(mixer, record);
		if (group != nullptr && group->linksSolo() && !group->isSuspended()) { activeSolo = group; }
	}
	return activeSolo;
}

void restoreInertSoloFlags(Mixer* mixer, const QJsonArray& groupStates, VcaGroup* activeSolo)
{
	for (const QJsonValue& value : groupStates)
	{
		const QJsonObject record = value.toObject();
		if (!record.value(QStringLiteral("soloed")).toBool()) { continue; }
		VcaGroup* group = groupFromSoloState(mixer, record);
		if (group != nullptr && group != activeSolo
			&& (group->isSuspended() || !group->linksSolo()))
		{
			group->soloModel()->setValue(true);
		}
	}
}

void restoreSoloFlags(Mixer* mixer, const QJsonArray& groupStates)
{
	for (VcaGroup* group : mixer->vcaGroups()) { group->soloModel()->setValue(false); }
	VcaGroup* activeSolo = activeSoloFromState(mixer, groupStates);
	if (activeSolo != nullptr) { activeSolo->soloModel()->setValue(true); }
	restoreInertSoloFlags(mixer, groupStates, activeSolo);
}

void restoreSoloChannels(Mixer* mixer, const QJsonArray& channels)
{
	for (const QJsonValue& value : channels)
	{
		const QJsonObject record = value.toObject();
		if (MixerChannel* channel = channelById(mixer, record.value(QStringLiteral("channel")).toString()))
		{
			channel->m_muteModel.setValue(record.value(QStringLiteral("muted")).toBool());
			channel->m_muteBeforeSolo = record.value(QStringLiteral("mute_before_solo")).toBool();
		}
	}
}

//! Restore solo flags before channel mute models so the model hooks cannot
//! replace the checkpointed mutes or pre-solo cache.
void restoreSoloState(Mixer* mixer, const QJsonObject& state)
{
	restoreSoloFlags(mixer, state.value(QStringLiteral("groups")).toArray());
	restoreSoloChannels(mixer, state.value(QStringLiteral("channels")).toArray());
}

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
	if (!std::isfinite(gain) || gain < 0.0 || gain > kMaxGain)
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
	}
	const int channels = static_cast<int>(Engine::mixer()->numChannels());
	for (int ch = 0; ch < channels; ++ch)
	{
		step.append(&Engine::mixer()->mixerChannel(ch)->m_muteModel);
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
	Mixer* mixer = Engine::mixer();
	const QJsonObject previousState = soloState(mixer);
	const SoloMuteCache cacheBefore = muteBeforeSoloCache(mixer);

	// One step for the whole action, taken BEFORE it: see soloStepObjects.
	control::addUndoStep(soloStepObjects());
	// The model write is the whole action: VcaGroup's constructor connects the
	// solo model's dataChanged straight to Mixer::applyGroupSolo (a direct
	// connection), so the member/solo/mute pass happens here whether or not
	// there is a display. A GUI instance needs no second call - unlike
	// track.set_solo, where the cross-track mute lives in a VIEW and therefore
	// has to be applied by hand when there is none.
	if (args.contains(QStringLiteral("restore")))
	{
		restoreSoloState(mixer, args.value(QStringLiteral("restore")).toObject());
	}
	else
	{
		group->soloModel()->setValue(soloed);
	}
	const SoloMuteCache cacheAfter = muteBeforeSoloCache(mixer);
	control::addStructuralUndoStep(
		[mixer, cacheBefore] { restoreMuteBeforeSoloCache(mixer, cacheBefore); },
		[mixer, cacheAfter] { restoreMuteBeforeSoloCache(mixer, cacheAfter); },
		soloMuteCacheBytes(cacheBefore, cacheAfter));

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("previous_soloed"), previous);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			previousState,
			QStringLiteral("vca.set_solo"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(group->id())},
				{QStringLiteral("soloed"), previous},
				{QStringLiteral("restore"), previousState}},
			true,
			QStringLiteral("composite checkpoint: the group's own solo flag, every other "
				"group's solo flag, every mixer channel's mute model and each transient "
				"m_muteBeforeSolo cache value are captured. The advertised inverse carries "
				"that full state, including other groups cleared by Mixer::applyGroupSolo")));
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
				.arg(control::channelIdOf(channel), vcaGroupId(group->id())));
	}

	const int id = group->id();
	auto mixerRef = Engine::mixer();
	const mix_ch_t member = static_cast<mix_ch_t>(index);
	const bool also = args.value(QStringLiteral("also")).toBool();
	if (!group->addMember(member, also))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 could not be added to %2")
				.arg(control::channelIdOf(channel), vcaGroupId(id)));
	}
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
		[mixerRef, id, member, also]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->addMember(member, also); }
		});

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("assigned"), control::channelIdOf(channel));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelIdOf(channel)},
				{QStringLiteral("assigned"), false}},
			QStringLiteral("vca.unassign"),
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelIdOf(channel)}},
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
				.arg(control::channelIdOf(channel), vcaGroupId(group->id())));
	}

	const int id = group->id();
	auto mixerRef = Engine::mixer();
	const mix_ch_t member = static_cast<mix_ch_t>(index);
	// Whether the channel was SHARED is part of the state this call removes, so
	// it has to be read BEFORE the removal: `also:true` membership puts a channel
	// in two groups at once (0.5.0 L09), and VcaGroup::addMember refuses to take a
	// channel another group already holds. Re-adding single-valued would therefore
	// be refused and the undo would silently lose a membership that existed
	// before the undo - so the undo restores the SAME membership mode.
	const bool also = sharedElsewhere(mixerRef, group, member);
	control::addUndoStep(
		[mixerRef, id, member, also]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->addMember(member, also); }
		},
		[mixerRef, id, member]()
		{
			if (VcaGroup* g = mixerRef->vcaGroup(id)) { g->removeMember(member); }
		});
	group->removeMember(member);

	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("unassigned"), control::channelIdOf(channel));
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelIdOf(channel)},
				{QStringLiteral("assigned"), true}},
			QStringLiteral("vca.assign"),
			// 'also' is part of the inverse: a shared membership is only re-added with it, and
			// vca.assign refuses a channel another group already holds unless it says also:true.
			QJsonObject{{QStringLiteral("group"), vcaGroupId(id)},
				{QStringLiteral("channel"), control::channelIdOf(channel)},
				{QStringLiteral("also"), also}},
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
	cmd.resultSchema = vcacontrol::groupStateSchema({
		{QStringLiteral("previous_gain"), control::numberProperty()},
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
	cmd.resultSchema = vcacontrol::groupStateSchema();
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
		"channel. Reversible: one undo step restores every group's solo flag, channel mute and "
		"transient pre-solo cache. The transaction inverse carries that full state for replay "
		"even after its journal step is evicted.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("soloed"), control::booleanProperty()},
		{QStringLiteral("restore"), control::objectSchema({
			{QStringLiteral("groups"), control::arrayProperty()},
			{QStringLiteral("channels"), control::arrayProperty()},
		}, {QStringLiteral("groups"), QStringLiteral("channels")})},
	}, {QStringLiteral("group"), QStringLiteral("soloed")});
	cmd.resultSchema = vcacontrol::groupStateSchema({
		{QStringLiteral("previous_soloed"), control::booleanProperty()},
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
		"what keeps 'soloing a member' single-valued; the refusal names the group holding it. With "
		"'also' (0.5.0) the channel joins this group as well, and the groups' gains multiply. "
		"Reversible through the ProjectJournal (a recorded undo step).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("channel"), control::stringProperty()},
		{QStringLiteral("also"), control::booleanProperty()},
	}, {QStringLiteral("group"), QStringLiteral("channel")});
	cmd.resultSchema = vcacontrol::groupStateSchema({
		{QStringLiteral("assigned"), control::stringProperty()},
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
	cmd.resultSchema = vcacontrol::groupStateSchema({
		{QStringLiteral("unassigned"), control::stringProperty()},
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
