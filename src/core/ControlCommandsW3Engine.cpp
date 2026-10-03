/*
 * ControlCommandsW3Engine.cpp - latency-compensated automation, post-fader inserts, receives, pre-fades
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
 *
 */
/*! 0.5.0 W3 engine lane:
 *  - automation.latency_compensation (MX-160): automation read where the latent audio is (AutomationLatency.h);
 *  - mixer.post_inserts (MX-082): a channel's post-fader devices and the target ("ch-<n>/post") that plugin.* and
 *    chain.* address them through (MixerPostInserts.h);
 *  - chain.receive (MX-063): another channel's audio mixed in at a slot of a channel's chain - a sidechain send
 *    plus a Receiver device, one undo step;
 *  - clip.pre_fade (ED-179): an audio clip's fade-in starting before it and fade-out ending after it (ClipPreFades.h). */

#include <algorithm>
#include <memory>

#include <QJsonArray>

#include "AutomationLatency.h"
#include "ClipPreFades.h"
#include "ControlCommandDef.h"
#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleClip.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

void setLatencyCompensation(bool on) { AutomationLatency::instance().setEnabled(on); }

ControlResult latencyCompensation(const QJsonObject& args)
{
	const bool before = AutomationLatency::instance().enabled();
	const bool on = args.value(QStringLiteral("enabled")).toBool(before);
	if (on != before)
	{
		setLatencyCompensation(on);
		addUndoStep([before]() { setLatencyCompensation(before); }, [on]() { setLatencyCompensation(on); });
		Engine::getSong()->setModified();
	}
	AutomationLatency::instance().refresh();
	int largest = 0;
	const int clips = AutomationLatency::compensatedClips(&largest);
	return ControlResult::success({{QStringLiteral("enabled"), on}, {QStringLiteral("compensated_clips"), clips},
		{QStringLiteral("largest_latency_frames"), largest}});
}

ControlResult postInserts(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	QJsonArray devices;
	for (const Effect* effect : channel->m_postInserts.chain().effects())
	{
		devices.append(QJsonObject{{QStringLiteral("plugin"), effectIdOf(effect)}, {QStringLiteral("name"), effect->displayName()},
			{QStringLiteral("enabled"), effect->isEnabled()}});
	}
	return ControlResult::success({{QStringLiteral("channel"), channelIdOf(channel)},
		{QStringLiteral("target"), postinserts::targetOf(channelIdOf(channel))}, {QStringLiteral("devices"), devices},
		{QStringLiteral("latency_frames"), channel->m_postInserts.chain().latencyFrames()}});
}

//! The build's Receiver device, or a typed refusal.
bool receiverEntry(ControlDeviceEntry* entry, ControlResult* error)
{
	for (const ControlDeviceEntry& candidate : controlDeviceCatalogue())
	{
		if (candidate.format == QLatin1String("builtin") && candidate.name == QLatin1String("receiver") && candidate.loadable)
		{
			*entry = candidate;
			return true;
		}
	}
	*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("this build has no Receiver device (plug-in module 'receiver')"));
	return false;
}

//! What chain.receive made, found again by ids on every undo and redo (a step may have rebuilt them).
struct Receive
{
	QString to;
	QString from;
	SidechainTapPoint tap = SidechainTapPoint::PostFader;
	bool routeExisted = false;
	ControlDeviceEntry entry;
	int position = -1;
	double level = 1.0;
	std::shared_ptr<int> fxId = std::make_shared<int>(-1);
};

//! Make the receive: the sidechain send (unless one was there) and the device at its slot; the device or nullptr.
Effect* makeReceive(const Receive& receive, ControlResult* error)
{
	MixerChannel* to = resolveMixerChannel(receive.to, error);
	MixerChannel* from = to != nullptr ? resolveMixerChannel(receive.from, error) : nullptr;
	if (from == nullptr) { return nullptr; }
	Mixer* mixer = Engine::mixer();
	const auto fromIndex = static_cast<mix_ch_t>(from->index());
	const auto toIndex = static_cast<mix_ch_t>(to->index());
	if (!receive.routeExisted && mixer->createSidechainSend(fromIndex, toIndex, 1.f, receive.tap) == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the mixer refused the send %1 -> %2: the "
			"master cannot send, or it would close a cycle").arg(receive.from, receive.to));
		return nullptr;
	}
	Effect* effect = controlInstantiateDevice(receive.entry, &to->m_fxChain, error);
	if (effect == nullptr)
	{
		if (!receive.routeExisted) { mixer->deleteSidechainSend(fromIndex, toIndex); }
		return nullptr;
	}
	placeEffect(&to->m_fxChain, effect, receive.position);
	ControlResult ignored;
	if (AutomatableModel* level = controlResolveParameter(effect, QStringLiteral("Level"), 0, false, &ignored))
	{
		level->setValue(static_cast<float>(receive.level));
	}
	*receive.fxId = effect->id();
	return effect;
}

//! Take the receive out again: the device, and the send when chain.receive made it.
void removeReceive(const Receive& receive)
{
	ControlResult ignored;
	MixerChannel* to = resolveMixerChannel(receive.to, &ignored);
	MixerChannel* from = resolveMixerChannel(receive.from, &ignored);
	if (to == nullptr || from == nullptr) { return; }
	for (Effect* effect : to->m_fxChain.effects())
	{
		if (effect->id() != *receive.fxId) { continue; }
		to->m_fxChain.removeEffect(effect);
		effect->deleteLater();
		break;
	}
	if (!receive.routeExisted)
	{
		Engine::mixer()->deleteSidechainSend(static_cast<mix_ch_t>(from->index()), static_cast<mix_ch_t>(to->index()));
	}
}

//! The two channels of a receive, or a typed refusal (a track's own chain has no sidechain input).
bool receiveEnds(const QJsonObject& args, Receive* receive, ControlResult* error)
{
	receive->to = args.value(QStringLiteral("target")).toString();
	receive->from = args.value(QStringLiteral("from")).toString();
	if (!receive->to.startsWith(QLatin1String("ch-")) || postinserts::isTarget(receive->to))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'target' is a mixer channel (ch-<n>): a "
			"track's own chain has no sidechain input - receive on the channel the track plays into"));
		return false;
	}
	MixerChannel* to = resolveMixerChannel(receive->to, error);
	MixerChannel* from = to != nullptr ? resolveMixerChannel(receive->from, error) : nullptr;
	if (from == nullptr) { return false; }
	if (from == to)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a channel cannot receive itself"));
		return false;
	}
	const MixerSidechainRoute* route = Engine::mixer()->channelSidechainSend(static_cast<mix_ch_t>(from->index()),
		static_cast<mix_ch_t>(to->index()));
	receive->routeExisted = route != nullptr;
	if (route != nullptr) { receive->tap = route->mode(); }
	return true;
}

ControlResult chainReceive(const QJsonObject& args)
{
	Receive receive;
	ControlResult error;
	if (!receiveEnds(args, &receive, &error) || !receiverEntry(&receive.entry, &error)) { return error; }
	const QString tapName = args.value(QStringLiteral("tap_point")).toString(QStringLiteral("post_fader"));
	SidechainTapPoint requested = SidechainTapPoint::PostFader;
	if (!sidechainTapPointFromName(tapName, &requested))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a tap point").arg(tapName));
	}
	// An existing send is kept as it is, so the reply names the tap it really uses.
	if (!receive.routeExisted) { receive.tap = requested; }
	receive.position = args.value(QStringLiteral("position")).toInt(-1);
	receive.level = std::clamp(args.value(QStringLiteral("level")).toDouble(1.0), 0.0, 2.0);
	Effect* effect = makeReceive(receive, &error);
	if (effect == nullptr) { return error; }
	addUndoStep([receive]() { removeReceive(receive); }, [receive]() { ControlResult ignored; makeReceive(receive, &ignored); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("target"), receive.to}, {QStringLiteral("from"), receive.from},
		{QStringLiteral("plugin"), effectIdOf(effect)}, {QStringLiteral("tap_point"), sidechainTapPointName(receive.tap)},
		{QStringLiteral("send_created"), !receive.routeExisted}});
}

//! The longest pre-fade a command sets: twenty bars of 4/4.
constexpr int MaxPreFadeTicks = 3840;

//! Why @a clip cannot play pre-fades, or empty (prefades::engaged's conditions a command can name).
QString preFadeRefusal(const SampleClip* clip)
{
	if (clip == nullptr) { return QStringLiteral("'clip' is an audio clip"); }
	if (!clip->rendersLinearly()) { return QStringLiteral("a warped clip (markers or its own tempo) has no pre-fades"); }
	if (clip->clipEdits().hasSpeed() || clip->clipEdits().transpose != 0)
	{
		return QStringLiteral("a clip with a speed change or a transpose has no pre-fades");
	}
	return QString();
}

ControlResult preFade(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (const QString why = preFadeRefusal(clip); !why.isEmpty())
	{
		return ControlResult::failure(clip == nullptr ? ControlErrorKind::InvalidArgs : ControlErrorKind::Refused, why);
	}
	ClipEdits after = clip->clipEdits();
	after.preFadeInTicks = std::clamp(args.value(QStringLiteral("fade_in")).toInt(after.preFadeInTicks), 0, MaxPreFadeTicks);
	after.preFadeOutTicks = std::clamp(args.value(QStringLiteral("fade_out")).toInt(after.preFadeOutTicks), 0, MaxPreFadeTicks);
	if (after != clip->clipEdits())
	{
		clip->addJournalCheckPoint();
		clip->setClipEdits(after);
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("fade_in"), after.preFadeInTicks},
		{QStringLiteral("fade_out"), after.preFadeOutTicks}, {QStringLiteral("lead_ticks"), prefades::leadTicks(*clip)}});
}

} // namespace

void registerW3EngineCommands(ControlRegistry& registry)
{
	AutomationLatency::instance();  // registers its project section before a project loads
	defineCommand(registry, QStringLiteral("automation.latency_compensation"), QStringLiteral("Latency-compensated automation: "
		"with 'enabled' true every automation clip is read where the audio it moves is - its curve shifted later by the "
		"latency upstream of its parameter (the devices before it in its chain, plus its mixer channel's PDC alignment), so "
		"a move lands on the sound it was drawn on. Saved with the project only while on (older projects render as before). "
		"With no arguments it reports it, with how many clips read with a lag. One undo step."),
		{{QStringLiteral("enabled"), booleanProperty()}}, {}, true, latencyCompensation);
	defineCommand(registry, QStringLiteral("mixer.post_inserts"), QStringLiteral("A mixer channel's post-fader inserts: the "
		"devices that run after its fader (a limiter on the channel's output, a meter that sees the fader), and the target "
		"id ('ch-<n>/post') that plugin.load, plugin.unload, plugin.bypass, plugin.param_set and chain.* address them "
		"through. Empty until a device is loaded there; their latency is in PDC. Read only."),
		{{QStringLiteral("channel"), stringProperty()}}, {QStringLiteral("channel")}, false, postInserts);
	defineCommand(registry, QStringLiteral("chain.receive"), QStringLiteral("Receive another channel's audio inside a "
		"channel's chain: a sidechain send from 'from' (tapped at 'tap_point', post_fader when left out; an existing one is "
		"kept) and a Receiver device at slot 'position' of 'target''s chain (the end when left out) that mixes it in at "
		"'level' (0..2, 1 when left out). The mixer refuses a cycle; PDC aligns the send to the channel's input. 'target' "
		"is a mixer channel (a track's own chain has no sidechain input). One undo step removes both."),
		{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("from"), stringProperty()},
			{QStringLiteral("tap_point"), enumProperty(sidechainTapPointNames())}, {QStringLiteral("position"), integerProperty(0, 64)},
			{QStringLiteral("level"), numberProperty()}},
		{QStringLiteral("target"), QStringLiteral("from")}, true, chainReceive);
	defineCommand(registry, QStringLiteral("clip.pre_fade"), QStringLiteral("An audio clip's pre-fades: its fade-in starts "
		"'fade_in' ticks BEFORE the clip and its fade-out ends 'fade_out' ticks AFTER it (0..3840 each, 0 for none; each is "
		"one ramp with the clip's own fade, in the same shape), and the clip plays the source audio its trim hides there - "
		"lead_ticks reports how much hidden audio the fade-in finds before the in-point. A warped, sped-up or transposed "
		"clip has none; a reversed clip or one on a comped track keeps its own span. Saved only when set. One undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("fade_in"), integerProperty(0, MaxPreFadeTicks)},
			{QStringLiteral("fade_out"), integerProperty(0, MaxPreFadeTicks)}},
		{QStringLiteral("clip")}, true, preFade);
}

} // namespace lmms
