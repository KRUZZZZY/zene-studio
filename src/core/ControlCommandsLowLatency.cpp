/*
 * ControlCommandsLowLatency.cpp - transport.low_latency: bypass the devices that delay what is monitored
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
/*! 0.5.0 L01 (MX-159): a low-latency monitoring mode. While it is on, every device whose reported
 *  latency is over 'max_ms' stops processing (Effect::setDontRun - the bypass delay compensation
 *  already leaves out, not the On/Off switch, so nothing is journalled or saved) on the chains that
 *  matter for playing in: with 'scope' armed (the default) the chains of record-armed tracks, the
 *  mixer channels they play into (and those send on to) and the master; with 'scope' all, every chain. Turning it off puts
 *  back exactly the devices it took out (a device another feature had stopped is left alone).
 *  Q050-174: a latent INSTRUMENT is not an Effect, so it is not stopped - it is what is played. Instead
 *  an instrument track in scope whose instrument reports more than 'max_ms' is monitored unaligned
 *  (AudioBusHandle::setMonitorUnaligned): its handle drops the alignment delay PDC adds to land it on a
 *  later path, so it is heard after its own latency only, while its latency still counts in the graph
 *  and every other path stays aligned as before. Off clears exactly the tracks it set.
 *  Session state, not project state. */

#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <algorithm>
#include <vector>

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

struct LowLatency
{
	bool enabled = false;
	int maxMs = 3;
	QString scope = QStringLiteral("armed");
	std::vector<QPointer<Effect>> bypassed;
	std::vector<QPointer<InstrumentTrack>> unaligned;  //!< Q050-174: the instrument paths monitored unaligned
};

LowLatency& state()
{
	static LowLatency s;
	return s;
}

EffectChain* trackChain(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->audioBusHandle()->effects(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->audioBusHandle()->effects(); }
	return nullptr;
}

//! Channel @a index's chains - its own and its rack's - and those of every channel it sends into: a
//! device on a channel downstream delays the monitored signal as much as one on the channel itself.
void addChannel(int index, QSet<EffectChain*>* out)
{
	if (index < 0 || index >= Engine::mixer()->numChannels()) { return; }
	MixerChannel* channel = Engine::mixer()->mixerChannel(index);
	if (out->contains(&channel->m_fxChain)) { return; }  // reached already (the mixer refuses send loops besides)
	out->insert(&channel->m_fxChain);
	for (int c = 1; c < channel->m_rack.chainCount(); ++c) { out->insert(channel->m_rack.chain(c)); }
	for (const MixerRoute* send : channel->m_sends) { addChannel(send->receiverIndex(), out); }
}

//! The chains the scope covers.
QSet<EffectChain*> chainsInScope(bool all)
{
	QSet<EffectChain*> out;
	addChannel(0, &out);  // the master is on every monitored path
	for (Track* track : Engine::getSong()->tracks())
	{
		if (!all && !trackRecordArmed(track)) { continue; }
		if (EffectChain* chain = trackChain(track)) { out.insert(chain); }
		if (IntModel* channel = track->mixerChannelModel()) { addChannel(channel->value(), &out); }
	}
	for (int i = 1; all && i < Engine::mixer()->numChannels(); ++i) { addChannel(i, &out); }
	out.remove(nullptr);
	return out;
}

void restoreAll()
{
	for (const QPointer<Effect>& effect : state().bypassed)
	{
		if (effect) { effect->setDontRun(false); }
	}
	state().bypassed.clear();
	for (const QPointer<InstrumentTrack>& track : state().unaligned)
	{
		if (track) { track->audioBusHandle()->setMonitorUnaligned(false); }
	}
	state().unaligned.clear();
}

void bypassOverThreshold()
{
	const double frames = state().maxMs * 0.001 * Engine::audioEngine()->outputSampleRate();
	for (EffectChain* chain : chainsInScope(state().scope == QLatin1String("all")))
	{
		for (Effect* effect : chain->effects())
		{
			if (effect->dontRun() || effect->latencyFrames() <= frames) { continue; }
			effect->setDontRun(true);
			state().bypassed.emplace_back(effect);
		}
	}
	// Q050-174: the latent instruments on the monitored paths (armed tracks, or every track for 'all').
	const bool all = state().scope == QLatin1String("all");
	for (Track* track : Engine::getSong()->tracks())
	{
		auto* instrument = dynamic_cast<InstrumentTrack*>(track);
		if (instrument == nullptr || (!all && !trackRecordArmed(track))) { continue; }
		if (instrument->audioBusHandle()->sourceLatencyFrames() <= frames) { continue; }
		instrument->audioBusHandle()->setMonitorUnaligned(true);
		state().unaligned.emplace_back(instrument);
	}
}

//! Q050-174: one instrument path monitored unaligned - the track, the channel it plays into, the
//! instrument's own latency (still heard) and the alignment delay it no longer waits for.
QJsonObject instrumentPathJson(InstrumentTrack* track)
{
	const AudioBusHandle* handle = track->audioBusHandle();
	const int channel = track->mixerChannelModel()->value();
	const QString channelName = channel >= 0 && channel < Engine::mixer()->numChannels()
		? channelIdOf(Engine::mixer()->mixerChannel(channel)) : QString();
	const int skipped = std::max(0, Engine::mixer()->channelInputLatency(static_cast<mix_ch_t>(std::max(0, channel)))
		- handle->alignedLatencyFrames());
	const double rate = Engine::audioEngine()->outputSampleRate();
	return {{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("name"), track->name()},
		{QStringLiteral("channel"), channelName},
		{QStringLiteral("path"), QStringLiteral("%1 -> %2").arg(trackIdOf(track), channelName)},
		{QStringLiteral("latency_frames"), handle->sourceLatencyFrames()},
		{QStringLiteral("latency_ms"), handle->sourceLatencyFrames() * 1000.0 / rate},
		{QStringLiteral("alignment_skipped_frames"), skipped}};
}

QJsonObject stateJson()
{
	QJsonArray names;
	for (const QPointer<Effect>& effect : state().bypassed)
	{
		if (effect) { names.append(QStringLiteral("%1 (%2)").arg(effect->displayName(), effectIdOf(effect))); }
	}
	QJsonArray instruments;
	for (const QPointer<InstrumentTrack>& track : state().unaligned)
	{
		if (track) { instruments.append(instrumentPathJson(track)); }
	}
	return {{QStringLiteral("enabled"), state().enabled}, {QStringLiteral("max_ms"), state().maxMs},
		{QStringLiteral("scope"), state().scope}, {QStringLiteral("bypassed"), names},
		{QStringLiteral("instruments"), instruments}};
}

ControlResult lowLatency(const QJsonObject& args)
{
	LowLatency& s = state();
	s.maxMs = args.value(QStringLiteral("max_ms")).toInt(s.maxMs);
	s.scope = args.value(QStringLiteral("scope")).toString(s.scope);
	s.enabled = args.value(QStringLiteral("enabled")).toBool(s.enabled);
	// Re-evaluated on every call: a track armed since, or a device added, is taken into account.
	restoreAll();
	if (s.enabled) { bypassOverThreshold(); }
	return ControlResult::success(stateJson());
}

} // namespace

void registerLowLatencyCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.low_latency");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("low_latency");
	cmd.description = QStringLiteral("Low-latency monitoring: with 'enabled', the devices whose latency is over 'max_ms' (0..100, "
		"3 by default) stop processing on the chains of record-armed tracks, the channels they play into and the master "
		"('scope' armed) - or on every chain ('scope' all) - so what you play is heard without their delay; off puts them "
		"back. A latent instrument on those tracks (over 'max_ms') keeps playing but is monitored without the alignment "
		"delay PDC adds to line it up with later paths ('instruments': track, channel, path, latency, the frames skipped); "
		"every other path stays aligned. Each call re-evaluates (arm a track, call again). Answers the devices it took out. "
		"Session state: nothing is saved or journalled.");
	cmd.argsSchema = objectSchema({{QStringLiteral("enabled"), booleanProperty()}, {QStringLiteral("max_ms"), integerProperty(0, 100)},
		{QStringLiteral("scope"), enumProperty({QStringLiteral("armed"), QStringLiteral("all")})}});
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = lowLatency;
	registry.registerCommand(cmd);
}

} // namespace lmms
