/*
 * DrumPadState.cpp - the state contract of one drum-rack pad (Q050-066)
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

#include "DrumPadState.h"

#include <QDomDocument>
#include <QJsonArray>

#include "ControlVocabulary.h"
#include "DummyEffect.h"
#include "Effect.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PathUtil.h"
#include "Song.h"
#include "TrackFolder.h"

namespace lmms::control
{

namespace
{

/*! A model's value as AutomatableModel::saveSettings wrote it under @a name: the attribute, or - for an
 *  automated or controlled model - the "value" of the child element of that name. */
double savedValue(const QDomElement& element, const QString& name, double fallback)
{
	if (element.hasAttribute(name)) { return element.attribute(name).toDouble(); }
	const QDomElement child = element.firstChildElement(name);
	return child.isNull() ? fallback : child.attribute(QStringLiteral("value"), QString::number(fallback)).toDouble();
}

//! The sampler's settings as its project file stores them: the one serialisation both sides already share.
void readSampler(Instrument* sampler, DrumPadState* out)
{
	QDomDocument doc;
	QDomElement parent = doc.createElement(QStringLiteral("pad"));
	doc.appendChild(parent);
	const QDomElement saved = sampler->saveState(doc, parent);
	const QString source = saved.attribute(QStringLiteral("src"));
	out->sample = source.isEmpty() ? QString() : PathUtil::toAbsolute(source);
	DrumPadPlayback& p = out->playback;
	p.amp = savedValue(saved, QStringLiteral("amp"), p.amp);
	p.start = savedValue(saved, QStringLiteral("sframe"), p.start);
	p.end = savedValue(saved, QStringLiteral("eframe"), p.end);
	p.loopStart = savedValue(saved, QStringLiteral("lframe"), p.loopStart);
	p.loop = static_cast<int>(savedValue(saved, QStringLiteral("looped"), p.loop));
	p.reversed = savedValue(saved, QStringLiteral("reversed"), 0.0) != 0.0;
	p.stutter = savedValue(saved, QStringLiteral("stutter"), 0.0) != 0.0;
	p.interpolation = static_cast<int>(savedValue(saved, QStringLiteral("interp"), p.interpolation));
	p.attack = savedValue(saved, QStringLiteral("atk"), p.attack);
	p.release = savedValue(saved, QStringLiteral("rel"), p.release);
	p.voiceLimit = static_cast<int>(savedValue(saved, QStringLiteral("voices"), p.voiceLimit));
}

MixerChannel* channelAt(int index)
{
	Mixer* mixer = Engine::mixer();
	return index >= 0 && index < mixer->numChannels() ? mixer->mixerChannel(index) : nullptr;
}

//! The pad's chain: the effects of its channel's chain 0, in processing order.
void readChain(MixerChannel* channel, DrumPadState* out)
{
	for (const Effect* effect : channel->m_fxChain.effects())
	{
		DrumPadEffect entry;
		entry.id = effectIdOf(effect);
		if (const auto* missing = dynamic_cast<const DummyEffect*>(effect))
		{
			entry.plugin = missing->originalPluginData().attribute(QStringLiteral("name"));
			entry.displayName = entry.plugin.isEmpty() ? QStringLiteral("Missing effect")
				: entry.plugin + QStringLiteral(" (missing)");
		}
		else if (const Plugin::Descriptor* descriptor = effect->descriptor())
		{
			entry.plugin = QString::fromUtf8(descriptor->name);
			entry.displayName = QString::fromUtf8(descriptor->displayName);
		}
		entry.enabled = effect->isEnabled();
		out->chain.push_back(entry);
	}
}

//! Whether anything but @a pad plays through @a channel: the master, another track on it, another channel sending in.
bool channelShared(const MixerChannel* channel, const Track* pad)
{
	if (channel->index() == 0 || !channel->m_receives.empty()) { return true; }
	for (Track* other : Engine::getSong()->tracks())
	{
		IntModel* model = other != pad ? other->mixerChannelModel() : nullptr;
		if (model != nullptr && model->value() == channel->index()) { return true; }
	}
	return false;
}

} // namespace

QJsonObject DrumPadState::toJson() const
{
	const QJsonObject play{{QStringLiteral("amp"), playback.amp}, {QStringLiteral("start"), playback.start},
		{QStringLiteral("end"), playback.end}, {QStringLiteral("loop_start"), playback.loopStart},
		{QStringLiteral("loop"), playback.loop}, {QStringLiteral("reversed"), playback.reversed},
		{QStringLiteral("stutter"), playback.stutter}, {QStringLiteral("interpolation"), playback.interpolation},
		{QStringLiteral("attack"), playback.attack}, {QStringLiteral("release"), playback.release},
		{QStringLiteral("voice_limit"), playback.voiceLimit}};
	QJsonArray effects;
	for (const DrumPadEffect& effect : chain)
	{
		effects.append(QJsonObject{{QStringLiteral("id"), effect.id}, {QStringLiteral("plugin"), effect.plugin},
			{QStringLiteral("display_name"), effect.displayName}, {QStringLiteral("enabled"), effect.enabled}});
	}
	return {{QStringLiteral("track"), track}, {QStringLiteral("name"), name}, {QStringLiteral("sample"), sample},
		{QStringLiteral("key"), key}, {QStringLiteral("channel"), channel}, {QStringLiteral("sends_to"), sendsTo},
		{QStringLiteral("playback"), play}, {QStringLiteral("chain"), effects}, {QStringLiteral("chain_shared"), chainShared}};
}

QString drumPadSampler()
{
	return QStringLiteral("audiofileprocessor");
}

bool isDrumPad(const Track* track)
{
	const auto* instrumentTrack = dynamic_cast<const InstrumentTrack*>(track);
	const Instrument* instrument = instrumentTrack != nullptr ? instrumentTrack->instrument() : nullptr;
	return instrument != nullptr && instrument->descriptor() != nullptr
		&& QLatin1String(instrument->descriptor()->name) == drumPadSampler();
}

bool readDrumPad(InstrumentTrack* track, DrumPadState* out)
{
	if (!isDrumPad(track)) { return false; }
	DrumPadState pad;
	pad.track = trackIdOf(track);
	pad.name = track->name();
	pad.key = track->baseNoteModel()->value();
	if (MixerChannel* channel = channelAt(track->mixerChannelModel()->value()))
	{
		pad.channel = channelIdOf(channel);
		if (channel->m_sends.size() == 1) { pad.sendsTo = channelIdOf(channel->m_sends.front()->receiver()); }
		readChain(channel, &pad);
		pad.chainShared = channelShared(channel, track);
	}
	else { pad.chainShared = true; }  // no channel the mixer has: not a chain of its own
	readSampler(track->instrument(), &pad);
	*out = pad;
	return true;
}

std::vector<DrumPadState> drumPadsOf(const TrackFolder* folder)
{
	std::vector<DrumPadState> pads;
	if (folder == nullptr) { return pads; }
	for (Track* child : folder->children())
	{
		DrumPadState pad;
		if (readDrumPad(dynamic_cast<InstrumentTrack*>(child), &pad)) { pads.push_back(pad); }
	}
	return pads;
}

QJsonObject drumRackState(const TrackFolder* folder)
{
	const std::vector<DrumPadState> pads = drumPadsOf(folder);
	QJsonArray list;
	QString bus = pads.empty() ? QString() : pads.front().sendsTo;
	for (const DrumPadState& pad : pads)
	{
		list.append(pad.toJson());
		if (pad.sendsTo != bus) { bus.clear(); }
	}
	return {{QStringLiteral("folder"), trackIdOf(folder)}, {QStringLiteral("name"), folder != nullptr ? folder->name() : QString()},
		{QStringLiteral("bus"), bus}, {QStringLiteral("pads"), list}};
}

} // namespace lmms::control
