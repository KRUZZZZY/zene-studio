/*
 * ControlCommandsTrackOffset.cpp - pdc.set_offset: a track's or a mixer channel's manual delay
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

/*! Q050-339: pdc.set_offset - the manual delay of an audio (sample) track, an instrument track or a mixer
 *  channel, in samples at the engine's rate. It is added to the automatic delay compensation (the offset is
 *  part of the path's latency as Mixer::updateLatencyCompensation sees it), so a positive offset plays the
 *  target that much later and a negative one earlier while every other path stays aligned. The instrument
 *  track's note delay (track.set_delay, notes started later) is a different setting and is untouched. Saved
 *  with the track or channel; one undo step. */

#include <QJsonObject>

#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LatencyCompensation.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! The offset @a target names lives in: a track's bus handle or a channel. One of the two is set.
struct OffsetHolder
{
	AudioBusHandle* handle = nullptr;
	MixerChannel* channel = nullptr;
	QString kind;
	int get() const { return handle != nullptr ? handle->offsetFrames() : channel->offsetFrames(); }
	void set(int frames) const
	{
		if (handle != nullptr) { handle->setOffsetFrames(frames); }
		else { channel->setOffsetFrames(frames); }
	}
};

AudioBusHandle* busOf(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track)) { return instrument->audioBusHandle(); }
	if (auto* sample = dynamic_cast<SampleTrack*>(track)) { return sample->audioBusHandle(); }
	return nullptr;
}

bool resolveHolder(const QString& id, OffsetHolder* holder, ControlResult* error)
{
	if (id.startsWith(QStringLiteral("ch-")))
	{
		holder->channel = resolveMixerChannel(id, error);
		if (holder->channel == nullptr) { return false; }
		if (holder->channel->index() == 0)
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the master has no later summing point to move it against - offset the channels or tracks feeding it"));
			return false;
		}
		holder->kind = QStringLiteral("channel");
		return true;
	}
	Track* track = resolveTrack(id, error);
	if (track == nullptr) { return false; }
	holder->handle = busOf(track);
	if (holder->handle == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 plays no audio of its own: an offset applies to an audio track, an instrument track or a mixer channel").arg(id));
		return false;
	}
	holder->kind = QStringLiteral("track");
	return true;
}

QJsonObject answer(const QString& id, const OffsetHolder& holder)
{
	const double rate = Engine::audioEngine()->outputSampleRate();
	return {{QStringLiteral("target"), id}, {QStringLiteral("kind"), holder.kind}, {QStringLiteral("offset_samples"), holder.get()},
		{QStringLiteral("offset_ms"), 1000.0 * holder.get() / rate}};
}

void applyOffset(const QString& id, int frames)
{
	OffsetHolder holder;
	ControlResult ignored;
	if (resolveHolder(id, &holder, &ignored)) { holder.set(frames); }
}

ControlResult setOffset(const QJsonObject& args)
{
	const QString id = args.value(QStringLiteral("target")).toString();
	OffsetHolder holder;
	ControlResult error;
	if (!resolveHolder(id, &holder, &error)) { return error; }
	if (!args.contains(QStringLiteral("offset_samples"))) { return ControlResult::success(answer(id, holder)); }
	const QJsonValue value = args.value(QStringLiteral("offset_samples"));
	const int bound = LatencyCompensation::MaxManualOffsetFrames;
	const double requested = value.toDouble(1e12);
	if (!value.isDouble() || requested < -bound || requested > bound || requested != static_cast<int>(requested))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'offset_samples' is a whole number of samples from %1 to %2").arg(-bound).arg(bound));
	}
	const int before = holder.get();
	const int after = static_cast<int>(requested);
	if (after == before) { return ControlResult::success(answer(id, holder)); }
	addUndoStep([id, before]() { applyOffset(id, before); }, [id, after]() { applyOffset(id, after); });
	holder.set(after);
	Engine::getSong()->setModified();
	QJsonObject result = answer(id, holder);
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("target"), id}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step puts the previous offset back")));
	return ControlResult::success(result);
}

} // namespace

void registerTrackOffsetCommand(ControlRegistry& registry)
{
	control::defineCommandWithSchema(registry, QStringLiteral("pdc.set_offset"), QStringLiteral(
		"Q050-339: the manual delay of 'target' - an audio track or an instrument track (trk-<n>), or a mixer channel "
		"(ch-<n>, not the master) - as 'offset_samples', a whole number of samples at the engine's rate (-8192..8192): "
		"positive plays it later, negative earlier. It is added to the automatic delay compensation, so every other "
		"path stays aligned (pdc.report shows the result). A track's offset moves its audio output (not a hosted "
		"instrument's extra routed output buses); an instrument "
		"track's note delay (track.set_delay) is a separate setting. With only 'target' it reports; the answer adds "
		"'offset_ms' and 'kind' (track or channel). Saved with the track or channel; one undo step."),
		objectSchema({{QStringLiteral("target"), stringProperty()}, {QStringLiteral("offset_samples"), integerProperty(-8192, 8192)}},
			{QStringLiteral("target")}), true, setOffset);
}

} // namespace lmms
