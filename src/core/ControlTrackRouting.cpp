/*
 * ControlTrackRouting.cpp - where a new track plays: an output, or a mixer channel of its own (Q050-212)
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

#include "ControlTrackRouting.h"

#include <memory>

#include <QDomDocument>

#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlReversibility.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "Song.h"

namespace lmms::control
{

namespace
{

bool hasChannelModel(Track::Type type)
{
	return type == Track::Type::Instrument || type == Track::Type::Sample;
}

//! Makes @a track's own channel, named after it, and links the two.
void giveOwnChannel(Track* track, int* channelId)
{
	const MixerChannel* channel = makeNamedChannel(track->name(), channelId);
	routeNewTrack(track, channel);
	track->setLinkedChannelId(*channelId);
}

} // namespace

bool autoTrackChannelEnabled()
{
	return ConfigManager::inst()->value(QStringLiteral("app"), QStringLiteral("autotrackchannel"), QStringLiteral("0")).toInt() != 0;
}

bool readNewTrackSpec(const QJsonObject& args, Track::Type type, NewTrackSpec* spec, ControlResult* error)
{
	spec->name = args.value(QStringLiteral("name")).toString();
	spec->instrument = args.value(QStringLiteral("instrument")).toString();
	spec->output = args.value(QStringLiteral("output")).toString();
	if (!spec->instrument.isEmpty() && (type != Track::Type::Instrument
		|| getPluginFactory()->pluginInfo(spec->instrument.toUtf8().constData()).isNull()))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'instrument' is a plug-in of this build, on an instrument track; '%1' is not one here").arg(spec->instrument));
		return false;
	}
	spec->ownChannel = spec->output.isEmpty() && hasChannelModel(type) && autoTrackChannelEnabled();
	if (spec->output.isEmpty()) { return true; }
	if (!hasChannelModel(type))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'output' routes an instrument or sample track; this type plays into no mixer channel"));
		return false;
	}
	const MixerChannel* channel = resolveMixerChannel(spec->output, error);
	if (channel == nullptr) { return false; }
	if (channel->isBus())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is a parallel bus, which takes no track input: the track would be silent. "
				"Name a regular channel, or use track.add_to_new_bus").arg(spec->output));
		return false;
	}
	return true;
}

void finishNewTrack(Track* track, const NewTrackSpec& spec, int* channelId)
{
	if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track); instrumentTrack != nullptr && !spec.instrument.isEmpty())
	{
		instrumentTrack->loadInstrument(spec.instrument);
	}
	if (!spec.name.isEmpty()) { track->setName(spec.name); }
	if (!spec.output.isEmpty())
	{
		ControlResult ignored;
		if (const MixerChannel* channel = resolveMixerChannel(spec.output, &ignored)) { routeNewTrack(track, channel); }
	}
	else if (spec.ownChannel)
	{
		giveOwnChannel(track, channelId);
	}
}

MixerChannel* makeNamedChannel(const QString& name, int* channelId)
{
	Mixer* mixer = Engine::mixer();
	MixerChannel* channel = mixer->mixerChannel(mixer->createChannel());
	if (*channelId >= 0) { channel->setId(*channelId); }
	else { *channelId = channel->id(); }
	channel->m_name = name;
	emit mixer->dataChanged();
	return channel;
}

void deleteChannelWithId(int channelId)
{
	if (channelId < 0) { return; }
	Mixer* mixer = Engine::mixer();
	for (int i = static_cast<int>(mixer->numChannels()) - 1; i > 0; --i)
	{
		if (mixer->mixerChannel(i)->id() == channelId) { mixer->deleteChannel(i); return; }
	}
}

void routeNewTrack(Track* track, const MixerChannel* channel)
{
	IntModel* model = track->mixerChannelModel();
	if (model == nullptr) { return; }
	// The track was built with the channel count of its day, and only an open mixer window widens the range after
	// that: a headless run would clamp the new channel away.
	model->setRange(0, static_cast<float>(Engine::mixer()->numChannels() - 1));
	model->setValue(static_cast<float>(channel->index()), false, false);
}

void insertChannelFields(QJsonObject* result, Track* track)
{
	const IntModel* model = track->mixerChannelModel();
	if (model == nullptr) { return; }
	const MixerChannel* channel = Engine::mixer()->mixerChannel(model->value());
	result->insert(QStringLiteral("channel"), channelIdOf(channel));
	result->insert(QStringLiteral("channel_name"), channel->m_name);
	result->insert(QStringLiteral("channel_linked"), track->linkedChannel() != nullptr);
}

void assignNewTrackChannel(Track* track)
{
	if (track == nullptr || track->trackContainer() != Engine::getSong() || !hasChannelModel(track->type())
		|| !autoTrackChannelEnabled()) { return; }
	int channelId = -1;
	giveOwnChannel(track, &channelId);
}

void recordCreatedRoutedTrack(Track* track)
{
	assignNewTrackChannel(track);
	if (track->linkedChannel() == nullptr) { recordCreatedTrack(track); return; }
	const CreatedTrack created = captureCreatedTrack(track, Engine::getSong());
	auto trackHolder = std::make_shared<int>(track->id());
	auto channelHolder = std::make_shared<int>(track->linkedChannelId());
	const QString channelName = track->name();
	addUndoStep(
		[trackHolder, channelHolder]() {
			if (Track* live = trackWithId(*trackHolder)) { removeTrack(live); }
			deleteChannelWithId(*channelHolder);
		},
		[trackHolder, channelHolder, channelName, saved = created.saved]() {
			const MixerChannel* channel = makeNamedChannel(channelName, channelHolder.get());
			QDomDocument again;
			again.setContent(saved);
			if (Track* made = Track::create(again.documentElement().firstChildElement(), Engine::getSong()))
			{
				routeNewTrack(made, channel);
				*trackHolder = made->id();
			}
		});
}

} // namespace lmms::control
