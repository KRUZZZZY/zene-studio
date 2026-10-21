/*
 * AafImportModel.cpp - the AAF import's plain half: the time rule, the model's
 *                      queries and its JSON, and the loss report.
 *
 * Nothing here needs LibAAF or a song, which is what lets the builder's test
 * run in CI on a machine without the library (include/AafImport.h).
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

#include "AafImport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

#include "volume.h"

namespace lmms::interchange
{

int AafModel::clipCount() const
{
	int count = 0;
	for (const AafTrack& track : tracks) { count += track.clips.size(); }
	return count;
}

QStringList AafModel::missingMedia() const
{
	QStringList missing;
	for (const AafMedia& entry : media)
	{
		if (entry.missing) { missing.append(entry.originalUri.isEmpty() ? entry.name : entry.originalUri); }
	}
	missing.removeDuplicates();  // several essences (one per channel) can name one file
	return missing;
}

qint64 AafModel::firstClipFrame() const
{
	qint64 first = std::numeric_limits<qint64>::max();
	for (const AafTrack& track : tracks)
	{
		for (const AafClip& clip : track.clips) { first = std::min(first, clip.positionFrames); }
	}
	return first == std::numeric_limits<qint64>::max() ? 0 : first;
}

void aafPopulateTrackGainClampLoss(const AafModel& model, AafLossReport& loss)
{
	loss.trackGainClamps = static_cast<int>(std::count_if(model.tracks.cbegin(), model.tracks.cend(), [](const AafTrack& track) {
		const float requestedVolume = static_cast<float>(track.gain * 100.0);
		return requestedVolume < static_cast<float>(MinVolume) || requestedVolume > static_cast<float>(MaxVolume);
	}));
}

QJsonObject AafLossReport::toJson() const
{
	QJsonObject result{
		{QStringLiteral("video_tracks"), videoTracks},
		{QStringLiteral("video_clips"), videoClips},
		{QStringLiteral("crossfades_as_fade_pairs"), crossfades},
		{QStringLiteral("track_gain_automation"), trackGainAutomation},
		{QStringLiteral("track_pan_automation"), trackPanAutomation},
		{QStringLiteral("solo_tracks"), soloTracks},
		{QStringLiteral("multi_essence_clips"), multiEssenceClips},
		{QStringLiteral("channel_selections"), channelSelections},
		{QStringLiteral("marker_lengths_and_comments"), markerExtras},
		{QStringLiteral("envelope_points_dropped"), envelopePointsDropped},
		{QStringLiteral("missing_media_clips"), missingMediaClips},
		{QStringLiteral("beyond_song_length"), beyondSongLength},
		{QStringLiteral("markers_beyond_song_length"), markersBeyondSongLength},
		{QStringLiteral("markers_refused"), markersRefused},
		{QStringLiteral("track_gain_clamps"), trackGainClamps},
		{QStringLiteral("not_in_libaaf"), QStringLiteral("effects and plug-in state, sends and buses")},
	};
	result.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
	return result;
}

bool aafIsOmfPath(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	return suffix == QLatin1String("omf") || suffix == QLatin1String("omfi");
}

qint64 aafFramesFromEditUnits(qint64 units, int rateNum, int rateDen, int sampleRate)
{
	if (rateNum <= 0 || rateDen <= 0 || sampleRate <= 0) { return 0; }
	const long double frames = static_cast<long double>(units) * sampleRate * rateDen / rateNum;
	return static_cast<qint64>(std::llround(frames));
}

qint64 aafTicksFromFrames(qint64 frames, int sampleRate, double tempo)
{
	if (sampleRate <= 0 || tempo <= 0.0) { return 0; }
	const long double beats = static_cast<long double>(frames) / sampleRate * tempo / 60.0L;
	return static_cast<qint64>(std::llround(beats * AafTicksPerQuarterNote));
}

QString aafDefaultMediaDir(const QString& aafPath, const QString& projectFile)
{
	const QString aafName = QFileInfo(aafPath).completeBaseName();
	if (projectFile.isEmpty())
	{
		return QFileInfo(aafPath).dir().filePath(aafName + QStringLiteral("-aaf-media"));
	}
	const QFileInfo project(projectFile);
	const QDir media(project.dir().filePath(project.completeBaseName() + QStringLiteral("-aaf-media")));
	return media.filePath(aafName);
}

namespace
{

QJsonObject fadeJson(const AafFade& fade)
{
	return {{QStringLiteral("frames"), fade.frames}, {QStringLiteral("shape"), fadeShapeName(fade.shape)}};
}

QJsonObject clipJson(const AafClip& clip)
{
	QJsonArray envelope;
	for (const AafGainPoint& point : clip.envelope)
	{
		envelope.append(QJsonObject{{QStringLiteral("at"), point.at}, {QStringLiteral("gain_db"), point.gainDb}});
	}
	return {{QStringLiteral("name"), clip.name}, {QStringLiteral("media"), clip.mediaName},
		{QStringLiteral("source"), clip.sourcePath},
		{QStringLiteral("position_frames"), clip.positionFrames}, {QStringLiteral("length_frames"), clip.lengthFrames},
		{QStringLiteral("source_offset_frames"), clip.sourceOffsetFrames},
		{QStringLiteral("gain_db"), gainLinearToDb(static_cast<float>(clip.gain))},
		{QStringLiteral("envelope"), envelope}, {QStringLiteral("fade_in"), fadeJson(clip.fadeIn)},
		{QStringLiteral("fade_out"), fadeJson(clip.fadeOut)}, {QStringLiteral("muted"), clip.muted}};
}

QJsonObject trackJson(const AafTrack& track)
{
	QJsonArray clips;
	for (const AafClip& clip : track.clips) { clips.append(clipJson(clip)); }
	QJsonObject result{{QStringLiteral("number"), track.number}, {QStringLiteral("name"), track.name},
		{QStringLiteral("gain_db"), gainLinearToDb(static_cast<float>(track.gain))},
		{QStringLiteral("muted"), track.muted}, {QStringLiteral("clips"), clips}};
	if (track.hasPan) { result.insert(QStringLiteral("pan"), track.pan); }
	return result;
}

QJsonObject mediaJson(const AafMedia& entry)
{
	return {{QStringLiteral("name"), entry.name}, {QStringLiteral("original_uri"), entry.originalUri},
		{QStringLiteral("path"), entry.path}, {QStringLiteral("embedded"), entry.embedded},
		{QStringLiteral("missing"), entry.missing}, {QStringLiteral("channels"), entry.channels},
		{QStringLiteral("sample_rate"), entry.sampleRate}};
}

} // namespace

QJsonObject aafModelJson(const AafModel& model)
{
	QJsonArray tracks;
	for (const AafTrack& track : model.tracks) { tracks.append(trackJson(track)); }
	QJsonArray markers;
	for (const AafMarker& marker : model.markers)
	{
		markers.append(QJsonObject{{QStringLiteral("position_frames"), marker.positionFrames},
			{QStringLiteral("name"), marker.name}, {QStringLiteral("color"), marker.color}});
	}
	QJsonArray media;
	for (const AafMedia& entry : model.media) { media.append(mediaJson(entry)); }
	return {{QStringLiteral("composition_name"), model.compositionName},
		{QStringLiteral("product_name"), model.productName}, {QStringLiteral("sample_rate"), model.sampleRate},
		{QStringLiteral("edit_rate"), model.editRate}, {QStringLiteral("tracks"), tracks},
		{QStringLiteral("markers"), markers}, {QStringLiteral("media"), media}};
}

} // namespace lmms::interchange
