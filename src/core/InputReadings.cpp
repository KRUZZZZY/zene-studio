/*
 * InputReadings.cpp - the capture channels' levels and the inputs of the record-armed tracks, read together
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

#include "InputReadings.h"

#include <cmath>

#include <QJsonArray>

#include "AudioEngine.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "Track.h"

namespace lmms::inputreadings
{

Readings take(inputmeter::Reader reader)
{
	Readings readings;
	readings.channelCount = inputmeter::channelCount();
	readings.peaks.assign(inputmeter::MaxChannels, 0.f);
	inputmeter::takeChannels(reader, readings.peaks.data(), inputmeter::MaxChannels);
	if (Engine::audioEngine() == nullptr) { return readings; }
	const MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
	const TrackContainer::TrackList* tracks = Engine::getSong() != nullptr ? &Engine::getSong()->tracks() : nullptr;
	for (int route = 0; route < recorder.trackCount(); ++route)
	{
		const TrackRecorder& recording = recorder.track(route);
		if (!recording.isArmed()) { continue; }
		Armed armed;
		armed.route = route;
		armed.track = tracks != nullptr && route < static_cast<int>(tracks->size()) ? tracks->at(static_cast<std::size_t>(route)) : nullptr;
		armed.inputChannel = recording.inputChannel();
		const bool delivered = armed.inputChannel >= 0 && armed.inputChannel < readings.channelCount;
		armed.peak = delivered ? readings.peaks[static_cast<std::size_t>(armed.inputChannel)] : 0.f;
		readings.armed.push_back(armed);
	}
	readings.peaks.resize(static_cast<std::size_t>(readings.channelCount));
	return readings;
}

double peakDb(float peak)
{
	return peak > 0.f ? 20.0 * std::log10(static_cast<double>(peak)) : -120.0;
}

QJsonObject toJson(const Readings& readings)
{
	QJsonArray channels;
	for (int c = 0; c < readings.channelCount; ++c)
	{
		const float peak = readings.peaks[static_cast<std::size_t>(c)];
		channels.append(QJsonObject{{QStringLiteral("input_channel"), c}, {QStringLiteral("peak"), static_cast<double>(peak)},
			{QStringLiteral("peak_db"), peakDb(peak)}, {QStringLiteral("clipped"), peak >= 1.f}});
	}
	QJsonArray armed;
	for (const Armed& entry : readings.armed)
	{
		armed.append(QJsonObject{{QStringLiteral("route"), entry.route},
			{QStringLiteral("track"), entry.track != nullptr ? control::trackIdOf(entry.track) : QString()},
			{QStringLiteral("track_name"), entry.track != nullptr ? entry.track->name() : QString()},
			{QStringLiteral("input_channel"), entry.inputChannel}, {QStringLiteral("peak"), static_cast<double>(entry.peak)},
			{QStringLiteral("peak_db"), peakDb(entry.peak)}, {QStringLiteral("clipped"), entry.peak >= 1.f}});
	}
	return {{QStringLiteral("channel_count"), readings.channelCount}, {QStringLiteral("channels"), channels},
		{QStringLiteral("armed"), armed}};
}

} // namespace lmms::inputreadings
