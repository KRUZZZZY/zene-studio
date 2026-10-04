/*
 * ControlCommandsDrumRack.cpp - track.drum_rack: a drum rack of pads, each a sampler with its own channel
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
/*! 0.5.0 L10 (MX-111): a drum rack with a chain per pad, made of the parts the product has. Each
 *  pad is a sampler track (AudioFileProcessor) playing its sample at its key, inside one folder
 *  track; each pad plays into a mixer channel of its own - the pad's chain, where its inserts go -
 *  and every pad channel sends to one bus channel, the rack's output. One undo step takes the whole
 *  rack away; its redo builds it again from the same pads. */

#include <memory>
#include <vector>

#include <QFileInfo>
#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "SampleDecoder.h"
#include "Song.h"
#include "TrackFolder.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxPads = 64;
constexpr int FirstPadKey = 36;  // C2: General MIDI's kick, where drum maps start

struct Pad
{
	QString sample;
	QString name;
	int key = FirstPadKey;
};

//! What one build made, so its undo can take exactly that away (and a redo fill it again).
struct Built
{
	std::vector<int> trackIds;  //!< the pads first, the folder last, by stable id (a removed track's undo frees it)
	std::vector<int> channelIds;  //!< the pad channels and the bus, by persistent id
};

MixerChannel* channelById(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return mixer->mixerChannel(i); }
	}
	return nullptr;
}

//! A new channel named @a name sending to @a to instead of the master.
int addChannel(const QString& name, int to, Built* built)
{
	Mixer* mixer = Engine::mixer();
	const int index = mixer->createChannel();
	MixerChannel* channel = mixer->mixerChannel(index);
	channel->m_name = name;
	if (to > 0)
	{
		mixer->deleteChannelSend(static_cast<mix_ch_t>(index), 0);
		mixer->createChannelSend(static_cast<mix_ch_t>(index), static_cast<mix_ch_t>(to));
	}
	built->channelIds.push_back(channel->id());
	return index;
}

void build(const QString& name, const std::vector<Pad>& pads, Built* built)
{
	Song* song = Engine::getSong();
	const int bus = addChannel(name, 0, built);
	auto* folder = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, song));
	for (const Pad& pad : pads)
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		if (track == nullptr) { continue; }
		track->setName(pad.name);
		if (Instrument* sampler = track->loadInstrument(QStringLiteral("audiofileprocessor"))) { sampler->loadFile(pad.sample); }
		track->baseNoteModel()->setValue(pad.key);
		track->mixerChannelModel()->setValue(addChannel(pad.name, bus, built));
		if (folder != nullptr) { track->setParentFolder(folder); }
		built->trackIds.push_back(track->id());
	}
	if (folder != nullptr)
	{
		folder->setName(name);
		built->trackIds.push_back(folder->id());
	}
}

void unbuild(Built* built)
{
	for (auto it = built->trackIds.rbegin(); it != built->trackIds.rend(); ++it)
	{
		if (Track* live = trackWithId(*it)) { removeTrack(live); }
	}
	built->trackIds.clear();
	// The highest-numbered channel first, so the others keep their numbers while they go.
	for (auto it = built->channelIds.rbegin(); it != built->channelIds.rend(); ++it)
	{
		if (MixerChannel* channel = channelById(*it)) { Engine::mixer()->deleteChannel(channel->index()); }
	}
	built->channelIds.clear();
}

bool padsOf(const QJsonArray& list, std::vector<Pad>* out, ControlResult* error)
{
	for (int i = 0; i < list.size(); ++i)
	{
		const QJsonObject object = list.at(i).toObject();
		Pad pad;
		pad.sample = object.value(QStringLiteral("sample")).toString();
		pad.key = object.value(QStringLiteral("key")).toInt(FirstPadKey + i);
		pad.name = object.value(QStringLiteral("name")).toString(QFileInfo(pad.sample).completeBaseName());
		if (!QFileInfo(pad.sample).isFile())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("pad %1: no sample file '%2'").arg(i).arg(pad.sample));
			return false;
		}
		if (!SampleDecoder::decode(pad.sample))
		{
			// The sampler would load nothing and the pad stay silent.
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("pad %1: '%2' is not an audio file this build reads")
				.arg(i).arg(pad.sample));
			return false;
		}
		if (pad.key < 0 || pad.key > 127)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("pad %1: 'key' is 0..127").arg(i));
			return false;
		}
		out->push_back(pad);
	}
	return true;
}

ControlResult drumRack(const QJsonObject& args)
{
	const QJsonArray list = args.value(QStringLiteral("pads")).toArray();
	if (list.isEmpty() || list.size() > MaxPads)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'pads' is 1..%1 pads").arg(MaxPads));
	}
	std::vector<Pad> pads;
	ControlResult error;
	if (!padsOf(list, &pads, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString(QStringLiteral("Drum Rack"));
	auto built = std::make_shared<Built>();
	addUndoStep([built]() { unbuild(built.get()); }, [built, name, pads]() { build(name, pads, built.get()); });
	build(name, pads, built.get());
	Engine::getSong()->setModified();
	QJsonArray tracks;
	for (const int id : built->trackIds) { tracks.append(trackId(id)); }
	QJsonArray channels;
	for (const int id : built->channelIds) { channels.append(channelIdOf(channelById(id))); }
	QJsonObject result{{QStringLiteral("folder"), tracks.isEmpty() ? QString() : tracks.last().toString()},
		{QStringLiteral("tracks"), tracks}, {QStringLiteral("bus"), channels.isEmpty() ? QString() : channels.first().toString()},
		{QStringLiteral("channels"), channels}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("pads"), list.size()}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step removes the rack's tracks and channels; its redo builds "
			"the rack again from the same pads")));
	return ControlResult::success(result);
}

} // namespace

void registerDrumRackCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.drum_rack");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("drum_rack");
	cmd.description = QStringLiteral("Build a drum rack: 'pads' [{sample (a file), key (0..127; 36 upward when left out), "
		"name}] - each pad a sampler track playing its sample at its key, inside one folder named 'name', and each "
		"playing into a mixer channel of its own (the pad's chain, for its inserts) that sends to one bus channel, the "
		"rack's output. Answers the folder, the pad tracks, the bus and the channels. One undo step.");
	cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("pads"), arrayProperty()}},
		{QStringLiteral("pads")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = drumRack;
	registry.registerCommand(cmd);
}

} // namespace lmms
