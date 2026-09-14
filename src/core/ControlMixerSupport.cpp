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
	const int index = idToIndex(id, QStringLiteral("ch-"));
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a channel id of the form ch-<n>").arg(id));
		return nullptr;
	}
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr || index >= static_cast<int>(mixer->numChannels()))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no mixer channel %1 (the mixer has %2)")
				.arg(id).arg(mixer == nullptr ? 0 : static_cast<int>(mixer->numChannels())));
		return nullptr;
	}
	return mixer->mixerChannel(index);
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
	out.insert(QStringLiteral("from"), channelId(route.senderIndex()));
	out.insert(QStringLiteral("to"), channelId(route.receiverIndex()));
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
	out.insert(QStringLiteral("from"), channelId(route.senderIndex()));
	out.insert(QStringLiteral("to"), channelId(route.receiverIndex()));
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
	out.insert(QStringLiteral("id"), channelId(channel.index()));
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

} // namespace control

} // namespace lmms
