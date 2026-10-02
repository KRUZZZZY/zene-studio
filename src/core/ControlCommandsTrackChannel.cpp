/*
 * ControlCommandsTrackChannel.cpp - track.set_mixer_channel: which mixer channel a track plays into
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

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult setMixerChannel(const QJsonObject& args)
{
	ControlResult error;
	const QString trackId = args.value(QStringLiteral("track")).toString();
	Track* track = resolveTrack(trackId, &error);
	if (track == nullptr) { return error; }
	IntModel* model = track->mixerChannelModel();
	if (model == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 plays into no mixer channel (only instrument and sample tracks do)").arg(trackId));
	}
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const int previous = model->value();
	// The track's channel is an IntModel - a JournallingObject - so the checkpoint is an exact
	// inverse of the one value this changes, as track.set_mute's is.
	model->addJournalCheckPoint();
	model->setValue(static_cast<float>(channel->index()));

	const QString previousId = channelIdOf(Engine::mixer()->mixerChannel(previous));
	QJsonObject result{{QStringLiteral("track"), trackId}, {QStringLiteral("channel"), channelIdOf(channel)},
		{QStringLiteral("index"), channel->index()}, {QStringLiteral("previous"), previousId}};
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(QJsonObject{{QStringLiteral("track"), trackId}, {QStringLiteral("channel"), previousId}},
			QStringLiteral("track.set_mixer_channel"),
			QJsonObject{{QStringLiteral("track"), trackId}, {QStringLiteral("channel"), previousId}}, true,
			QStringLiteral("ProjectJournal (the track's mixer-channel IntModel checkpoint)")));
	return ControlResult::success(result);
}

} // namespace

void registerTrackChannelCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_mixer_channel");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_mixer_channel");
	cmd.description = QStringLiteral("Route a track into a mixer channel: `channel` is a ch-<n> id "
		"(mixer.get_state lists them; the master is a channel too). What the track header's channel "
		"number and the Inspector's Mixer field set. Only instrument and sample tracks have a channel. "
		"Reversible: one control.undo puts the previous channel back. The reply names the previous "
		"channel.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("channel"), stringProperty()}}, {QStringLiteral("track"), QStringLiteral("channel")});
	cmd.resultSchema = objectSchema({{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("index"), integerProperty()},
		{QStringLiteral("previous"), stringProperty()}});
	cmd.mutating = true;
	cmd.handler = setMixerChannel;
	registry.registerCommand(cmd);
}

} // namespace lmms
