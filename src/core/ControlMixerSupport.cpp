/*
 * ControlMixerSupport.cpp - shared helpers for the pdc.* / bus.* / routing.*
 *                           and mixer routing command groups (SPEC A11-A16).
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

#include "ControlMixerSupport.h"

#include "ControlReversibility.h"

#include <QJsonArray>
#include <QStringList>

#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

using namespace control;

namespace control
{

MixerChannel* resolveMixerChannel(const QString& id, ControlResult* error)
{
	const int wanted = idToIndex(id, QStringLiteral("ch-"));
	if (wanted < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a channel id of the form ch-<n>").arg(id));
		return nullptr;
	}
	Mixer* mixer = Engine::mixer();
	// By id, not by position (SPEC-stable-ids.md slice 2): the number names the
	// channel OBJECT (MixerChannel::id(), written into the project as the
	// <mixerchannel> element's `id` attribute and read back on load), so a
	// cached ch-<n> still names the same channel after a sibling channel is
	// added, removed or moved. There is deliberately NO positional fallback:
	// every channel carries an id from construction, so a fallback could only
	// ever resolve a stale position. A well-formed id naming no live channel is
	// the typed not_found below.
	MixerChannel* channel = nullptr;
	if (mixer != nullptr)
	{
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			MixerChannel* candidate = mixer->mixerChannel(i);
			if (candidate != nullptr && candidate->id() == wanted)
			{
				channel = candidate;
				break;
			}
		}
	}
	if (channel == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no mixer channel %1 (the mixer has %2)")
				.arg(id).arg(mixer == nullptr ? 0 : static_cast<int>(mixer->numChannels())));
		return nullptr;
	}
	return channel;
}

QString sidechainTapPointName(SidechainTapPoint point)
{
	switch (point)
	{
		case SidechainTapPoint::PostFader: return QStringLiteral("post_fader");
		case SidechainTapPoint::PreFx: return QStringLiteral("pre_fx");
		case SidechainTapPoint::PreFader: return QStringLiteral("pre_fader");
		case SidechainTapPoint::PostFaderNoGain: return QStringLiteral("post_fader_no_gain");
	}
	return QStringLiteral("unknown");
}

QStringList sidechainTapPointNames()
{
	return {QStringLiteral("post_fader"), QStringLiteral("pre_fx"),
		QStringLiteral("pre_fader"), QStringLiteral("post_fader_no_gain")};
}

bool sidechainTapPointFromName(const QString& name, SidechainTapPoint* point)
{
	if (name == QLatin1String("post_fader")) { *point = SidechainTapPoint::PostFader; return true; }
	if (name == QLatin1String("pre_fx")) { *point = SidechainTapPoint::PreFx; return true; }
	if (name == QLatin1String("pre_fader")) { *point = SidechainTapPoint::PreFader; return true; }
	if (name == QLatin1String("post_fader_no_gain"))
	{
		*point = SidechainTapPoint::PostFaderNoGain;
		return true;
	}
	return false;
}

QJsonObject routeJson(MixerRoute& route)
{
	QJsonObject out;
	out.insert(QStringLiteral("from"), channelIdOf(route.sender()));
	out.insert(QStringLiteral("to"), channelIdOf(route.receiver()));
	out.insert(QStringLiteral("amount"), static_cast<double>(route.amount()->value()));
	out.insert(QStringLiteral("pre_fader"), route.preFader());
	// The delay this edge applies at the receiver so the sender's path lands on
	// the receiver's alignment point (Mixer::updateLatencyCompensation).
	out.insert(QStringLiteral("compensation_frames"), route.compensationFrames());
	return out;
}

QJsonObject sidechainRouteJson(MixerSidechainRoute& route)
{
	QJsonObject out;
	out.insert(QStringLiteral("from"), channelIdOf(route.sender()));
	out.insert(QStringLiteral("to"), channelIdOf(route.receiver()));
	out.insert(QStringLiteral("amount"), static_cast<double>(route.amount()->value()));
	out.insert(QStringLiteral("tap_point"), sidechainTapPointName(route.mode()));
	// A deferred route closes a cycle through at least one regular send, so it
	// reads the previous period's committed tap and never gates its receiver.
	out.insert(QStringLiteral("deferred"), route.deferred());
	out.insert(QStringLiteral("compensation_frames"), route.compensationFrames());
	return out;
}

QJsonObject channelLatencyJson(MixerChannel& channel)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), channelIdOf(&channel));
	out.insert(QStringLiteral("index"), channel.index());
	out.insert(QStringLiteral("name"), channel.m_name);
	out.insert(QStringLiteral("is_master"), channel.isMaster());
	out.insert(QStringLiteral("is_bus"), channel.isBus());
	out.insert(QStringLiteral("volume"), static_cast<double>(channel.m_volumeModel.value()));
	out.insert(QStringLiteral("muted"), channel.m_muteModel.value());
	// Published by Mixer::updateLatencyCompensation() once per period.
	out.insert(QStringLiteral("input_latency_frames"), channel.inputLatencyFrames());
	// What this channel's own effect chain adds to its output
	// (EffectChain::latencyFrames, refreshed by EffectChain::refreshLatency).
	out.insert(QStringLiteral("chain_latency_frames"), channel.m_fxChain.latencyFrames());

	QJsonArray sends;
	for (MixerRoute* route : channel.m_sends) { sends.append(routeJson(*route)); }
	out.insert(QStringLiteral("sends"), sends);
	out.insert(QStringLiteral("send_count"), sends.size());

	QJsonArray sidechainSends;
	for (MixerSidechainRoute* route : channel.m_sidechainSends)
	{
		sidechainSends.append(sidechainRouteJson(*route));
	}
	out.insert(QStringLiteral("sidechain_sends"), sidechainSends);
	out.insert(QStringLiteral("sidechain_send_count"), sidechainSends.size());
	out.insert(QStringLiteral("sidechain_receive_count"),
		static_cast<int>(channel.m_sidechainReceives.size()));
	return out;
}

// ---------------------------------------------------------------------------
// The mixer's routing verbs - the engine-side half, shared by the four
// command handlers in ControlCommandsMixerRoutes.cpp.
// ---------------------------------------------------------------------------

MixerRoute* findMixerRoute(mix_ch_t fromIndex, mix_ch_t toIndex)
{
	MixerChannel* from = Engine::mixer()->mixerChannel(fromIndex);
	for (MixerRoute* route : from->m_sends)
	{
		if (route->receiverIndex() == toIndex) { return route; }
	}
	return nullptr;
}

bool resolveRoutingEnds(const QJsonObject& args, RoutingEnds* ends, ControlResult* error)
{
	ends->from = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), error);
	if (ends->from == nullptr) { return false; }
	ends->to = resolveMixerChannel(args.value(QStringLiteral("to")).toString(), error);
	if (ends->to == nullptr) { return false; }
	if (ends->from->index() == ends->to->index())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a channel cannot route to itself"));
		return false;
	}
	// The engine refuses a cycle at scheduling time (Mixer::checkInfiniteLoop);
	// asking it FIRST means the refusal is the engine's judgement and nothing has
	// been written when it comes back.
	Mixer* mixer = Engine::mixer();
	if (mixer->isInfiniteLoop(ends->from->index(), ends->to->index()))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("routing %1 to %2 would close a feedback path (the mixer refuses it)")
				.arg(channelIdOf(ends->from), channelIdOf(ends->to)));
		return false;
	}
	return true;
}

QJsonObject mixerRouteBeforeState(MixerRoute* route)
{
	QJsonObject before;
	before.insert(QStringLiteral("existed"), route != nullptr);
	if (route != nullptr)
	{
		before.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
		before.insert(QStringLiteral("pre_fader"), route->preFader());
	}
	return before;
}

QJsonObject mixerSidechainBeforeState(MixerSidechainRoute* route)
{
	QJsonObject before;
	before.insert(QStringLiteral("existed"), route != nullptr);
	if (route != nullptr)
	{
		before.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
		before.insert(QStringLiteral("tap_point"), sidechainTapPointName(route->mode()));
	}
	return before;
}

void recordMixerRouteStep(mix_ch_t fromIndex, mix_ch_t toIndex, const QJsonObject& before,
	float wantedAmount, bool wantedPreFader)
{
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	const float amount = static_cast<float>(before.value(QStringLiteral("amount")).toDouble(1.0));
	const bool preFader = before.value(QStringLiteral("pre_fader")).toBool();
	Mixer* mixer = Engine::mixer();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, existed, amount, preFader]() {
			if (!existed) { mixer->deleteChannelSend(fromIndex, toIndex); return; }
			MixerRoute* route = findMixerRoute(fromIndex, toIndex);
			if (route != nullptr)
			{
				route->amount()->setValue(amount);
				route->setPreFader(preFader);
			}
		},
		[mixer, fromIndex, toIndex, wantedAmount, wantedPreFader]() {
			mixer->createChannelSend(fromIndex, toIndex, wantedAmount, wantedPreFader);
		});
}

void recordMixerSidechainStep(mix_ch_t fromIndex, mix_ch_t toIndex, const QJsonObject& before,
	float wantedAmount, SidechainTapPoint wantedMode)
{
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	const float amount = static_cast<float>(before.value(QStringLiteral("amount")).toDouble(1.0));
	SidechainTapPoint mode = SidechainTapPoint::PostFader;
	sidechainTapPointFromName(before.value(QStringLiteral("tap_point")).toString(), &mode);
	Mixer* mixer = Engine::mixer();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, existed, amount, mode]() {
			if (!existed) { mixer->deleteSidechainSend(fromIndex, toIndex); return; }
			MixerSidechainRoute* route = mixer->channelSidechainSend(fromIndex, toIndex);
			if (route != nullptr)
			{
				route->amount()->setValue(amount);
				route->setMode(mode);
			}
		},
		[mixer, fromIndex, toIndex, wantedAmount, wantedMode]() {
			mixer->createSidechainSend(fromIndex, toIndex, wantedAmount, wantedMode);
		});
}

QJsonObject mixerRouteResult(const RoutingEnds& ends, MixerRoute* route,
	const QJsonObject& before)
{
	QJsonObject result;
	result.insert(QStringLiteral("from"), channelIdOf(ends.from));
	result.insert(QStringLiteral("to"), channelIdOf(ends.to));
	result.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
	result.insert(QStringLiteral("pre_fader"), route->preFader());
	result.insert(QStringLiteral("created"), !before.value(QStringLiteral("existed")).toBool());
	result.insert(QStringLiteral("route"), routeJson(*route));
	return result;
}

} // namespace control

void registerRoutingSurfaceCommands(ControlRegistry& registry)
{
	// The group registrations are declared in namespace lmms
	// (include/ControlRegistryGroups.h), whose block the caller is in.
	registerMixerRouteCommands(registry);
	registerPdcCommands(registry);
	registerRoutingCommands(registry);
	registerBusCommands(registry);
	registerPortCommands(registry);
}

} // namespace lmms
