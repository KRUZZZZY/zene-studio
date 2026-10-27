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
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PathUtil.h"
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
}

MixerChannel* channelAt(int index)
{
	Mixer* mixer = Engine::mixer();
	return index >= 0 && index < mixer->numChannels() ? mixer->mixerChannel(index) : nullptr;
}

} // namespace

QJsonObject DrumPadState::toJson() const
{
	const QJsonObject play{{QStringLiteral("amp"), playback.amp}, {QStringLiteral("start"), playback.start},
		{QStringLiteral("end"), playback.end}, {QStringLiteral("loop_start"), playback.loopStart},
		{QStringLiteral("loop"), playback.loop}, {QStringLiteral("reversed"), playback.reversed},
		{QStringLiteral("stutter"), playback.stutter}, {QStringLiteral("interpolation"), playback.interpolation}};
	return {{QStringLiteral("track"), track}, {QStringLiteral("name"), name}, {QStringLiteral("sample"), sample},
		{QStringLiteral("key"), key}, {QStringLiteral("channel"), channel}, {QStringLiteral("sends_to"), sendsTo},
		{QStringLiteral("playback"), play}};
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
	}
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
