/*
 * DawProjectModel.cpp - the DAWproject model: its own arithmetic, its own
 *                       comparison, and the convention as data.
 *
 * The model half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7). What
 * lives here is everything about the model that does NOT touch the engine: the
 * tick/beat arithmetic, the contentType vocabulary, equality, the printable
 * digest a round trip compares, the JSON view `dawproject.read` returns, and
 * `dawproject.convention`. The session coupling is its own translation unit
 * (src/core/DawProjectSession.cpp), for the same reason the SMF module's reader
 * is: gate 7 measures a file.
 *
 * THE DIGEST IS NOT A HASH, and that is the point. `dawProjectModelDigest()`
 * returns one printable line per entity - track, clip, note, point - joined, so
 * a failed round trip prints the two lines that differ rather than "the hashes
 * disagree". The release contract for this row asks for exactly that: "compare
 * the model, not the bytes".
 *
 * THE CONVENTION is reported as data (`dawproject.convention`), the shape
 * `interchange.smf_convention` established: the format's name and version, the
 * container's entry names and encoding, the time unit, LMMS' tick domain and
 * the exact rule between the two, and the stated losses. A client that wants to
 * know which version this build speaks reads it off the wire instead of out of
 * a comment.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <cmath>

#include "DawProjectInterchange.h"

namespace lmms
{
namespace interchange
{

//! Six decimals, the precision the writer emits. A digest built from the same
//! precision the file carries is what makes "the model did not change" a claim
//! about what a round trip can preserve.
constexpr int DigestDecimals = 6;

double dawProjectBeatsFromTicks(qint64 ticks)
{
	return static_cast<double>(ticks) / DawProjectTicksPerQuarterNote;
}

qint64 dawProjectTicksFromBeats(double beats)
{
	return static_cast<qint64>(std::floor(beats * DawProjectTicksPerQuarterNote + 0.5));
}

QString dawProjectContentTypeForType(const QString& typeName, bool* lost)
{
	if (lost != nullptr) { *lost = false; }
	if (typeName == QLatin1String("instrument") || typeName == QLatin1String("pattern"))
	{
		return QStringLiteral("notes");
	}
	if (typeName == QLatin1String("sample")) { return QStringLiteral("audio"); }
	if (typeName == QLatin1String("automation") || typeName == QLatin1String("hiddenautomation"))
	{
		return QStringLiteral("automation");
	}
	if (typeName == QLatin1String("video")) { return QStringLiteral("video"); }
	if (typeName == QLatin1String("folder")) { return QStringLiteral("tracks"); }
	if (lost != nullptr) { *lost = true; }
	return QString();
}

QString dawProjectTypeForContentType(const QString& contentType, bool* lost)
{
	if (lost != nullptr) { *lost = false; }
	const QStringList values = contentType.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (const QString& value : values)
	{
		if (value == QLatin1String("audio")) { return QStringLiteral("sample"); }
		if (value == QLatin1String("automation")) { return QStringLiteral("automation"); }
		if (value == QLatin1String("video")) { return QStringLiteral("video"); }
		if (value == QLatin1String("tracks")) { return QStringLiteral("folder"); }
	}
	for (const QString& value : values)
	{
		if (value == QLatin1String("notes")) { return QStringLiteral("instrument"); }
	}
	// A document that declares nothing, or only "markers", is read as an
	// instrument track: it carries notes if it carries anything this module
	// understands, and Instrument is the type that can hold them.
	return QStringLiteral("instrument");
}

int DawProjectTrack::noteCount() const
{
	int total = 0;
	for (const DawProjectClip& clip : clips) { total += clip.notes.size(); }
	return total;
}

bool DawProjectTrack::operator==(const DawProjectTrack& other) const
{
	return id == other.id && name == other.name && color == other.color
		&& contentType == other.contentType && typeName == other.typeName
		&& lostContentType == other.lostContentType && channelId == other.channelId
		&& solo == other.solo && mute == other.mute && volume == other.volume
		&& hasVolume == other.hasVolume && pan == other.pan && hasPan == other.hasPan
		&& destinationChannelId == other.destinationChannelId && clips == other.clips;
}

int DawProjectModel::clipCount() const
{
	int total = 0;
	for (const DawProjectTrack& track : tracks) { total += track.clips.size(); }
	return total;
}

int DawProjectModel::noteCount() const
{
	int total = 0;
	for (const DawProjectTrack& track : tracks) { total += track.noteCount(); }
	return total;
}

bool DawProjectModel::operator==(const DawProjectModel& other) const
{
	return formatVersion == other.formatVersion
		&& applicationName == other.applicationName
		&& applicationVersion == other.applicationVersion && tempo == other.tempo
		&& numerator == other.numerator && denominator == other.denominator
		&& mixerChannels == other.mixerChannels && tracks == other.tracks
		&& tempoPoints == other.tempoPoints && meterPoints == other.meterPoints;
}

namespace
{

QString value(double number, int decimals = DigestDecimals)
{
	return QString::number(number, 'f', decimals);
}

} // namespace

QString dawProjectModelDigest(const DawProjectModel& model)
{
	QStringList lines;
	lines << QStringLiteral("project version=%1 by=%2 %3")
		.arg(model.formatVersion, model.applicationName, model.applicationVersion);
	lines << QStringLiteral("transport tempo=%1 time=%2/%3")
		.arg(value(model.tempo)).arg(model.numerator).arg(model.denominator);
	for (int index = 0; index < model.mixerChannels.size(); index++)
	{
		const DawProjectMixerChannel& channel = model.mixerChannels[index];
		lines << QStringLiteral("mixer %1 id=%2 name=%3 role=%4 solo=%5 volume=%6 mute=%7")
			.arg(index).arg(channel.id, channel.name, channel.role)
			.arg(channel.solo ? 1 : 0).arg(value(channel.volume))
			.arg(channel.mute ? 1 : 0);
	}
	for (int index = 0; index < model.tracks.size(); index++)
	{
		const DawProjectTrack& track = model.tracks[index];
		lines << QStringLiteral("track %1 id=%2 name=%3 type=%4 content=%5 color=%6")
			.arg(index).arg(track.id, track.name, track.typeName, track.contentType,
				track.color.isEmpty() ? QStringLiteral("-") : track.color);
		lines << QStringLiteral("  channel id=%1 solo=%2 volume=%3 mute=%4 pan=%5 dest=%6")
			.arg(track.channelId).arg(track.solo ? 1 : 0)
			.arg(track.hasVolume ? value(track.volume) : QStringLiteral("absent"))
			.arg(track.mute ? 1 : 0)
			.arg(track.hasPan ? value(track.pan) : QStringLiteral("absent"))
			.arg(track.destinationChannelId.isEmpty() ? QStringLiteral("-")
				: track.destinationChannelId);
		for (int clipIndex = 0; clipIndex < track.clips.size(); clipIndex++)
		{
			const DawProjectClip& clip = track.clips[clipIndex];
			lines << QStringLiteral("  clip %1 time=%2 duration=%3 playStart=%4 notes=%5 name=%6")
				.arg(clipIndex).arg(value(clip.time), value(clip.duration),
					value(clip.playStart)).arg(clip.notes.size()).arg(clip.name);
			for (int noteIndex = 0; noteIndex < clip.notes.size(); noteIndex++)
			{
				const DawProjectNote& note = clip.notes[noteIndex];
				lines << QStringLiteral("    note %1 time=%2 duration=%3 channel=%4 key=%5 "
					"vel=%6 rel=%7").arg(noteIndex).arg(value(note.time), value(note.duration))
					.arg(note.channel).arg(note.key).arg(value(note.vel), value(note.rel));
			}
		}
	}
	for (int index = 0; index < model.tempoPoints.size(); index++)
	{
		const DawProjectPoint& point = model.tempoPoints[index];
		lines << QStringLiteral("tempo point %1 time=%2 value=%3")
			.arg(index).arg(value(point.time), value(point.value));
	}
	for (int index = 0; index < model.meterPoints.size(); index++)
	{
		const DawProjectPoint& point = model.meterPoints[index];
		lines << QStringLiteral("metre point %1 time=%2 value=%3/%4")
			.arg(index).arg(value(point.time)).arg(point.numerator).arg(point.denominator);
	}
	return lines.join(QLatin1Char('\n'));
}

QJsonObject DawProjectLossReport::toJson() const
{
	QJsonObject loss;
	loss.insert(QStringLiteral("audio_clips_skipped"), audioClipsSkipped);
	loss.insert(QStringLiteral("automation_clips_skipped"), automationClipsSkipped);
	loss.insert(QStringLiteral("unmapped_track_types"), unmappedTrackTypes);
	loss.insert(QStringLiteral("folder_children_lost"), folderChildrenLost);
	loss.insert(QStringLiteral("pan_not_written"), panNotWritten);
	loss.insert(QStringLiteral("routing_lost"), routingLost);
	loss.insert(QStringLiteral("mixer_sharing_lost"), mixerSharingLost);
	loss.insert(QStringLiteral("rounded_times"), roundedTimes);
	loss.insert(QStringLiteral("devices_not_written"), devicesNotWritten);
	loss.insert(QStringLiteral("sends_not_written"), sendsNotWritten);
	loss.insert(QStringLiteral("fades_not_written"), fadesNotWritten);
	loss.insert(QStringLiteral("split_map_events"), splitMapEvents);
	return loss;
}

bool DawProjectLossReport::clean() const
{
	return audioClipsSkipped == 0 && automationClipsSkipped == 0 && unmappedTrackTypes == 0
		&& folderChildrenLost == 0 && panNotWritten == 0 && routingLost == 0
		&& mixerSharingLost == 0 && roundedTimes == 0 && devicesNotWritten == 0
		&& sendsNotWritten == 0 && fadesNotWritten == 0 && splitMapEvents == 0;
}

QJsonObject dawProjectModelJson(const DawProjectModel& model)
{
	QJsonObject root;
	root.insert(QStringLiteral("format_version"), model.formatVersion);
	root.insert(QStringLiteral("application_name"), model.applicationName);
	root.insert(QStringLiteral("application_version"), model.applicationVersion);
	root.insert(QStringLiteral("tempo"), model.tempo);
	root.insert(QStringLiteral("numerator"), model.numerator);
	root.insert(QStringLiteral("denominator"), model.denominator);
	root.insert(QStringLiteral("track_count"), model.trackCount());
	root.insert(QStringLiteral("mixer_channel_count"), model.mixerChannelCount());
	root.insert(QStringLiteral("clip_count"), model.clipCount());
	root.insert(QStringLiteral("note_count"), model.noteCount());
	root.insert(QStringLiteral("tempo_point_count"), model.tempoPoints.size());
	root.insert(QStringLiteral("meter_point_count"), model.meterPoints.size());
	root.insert(QStringLiteral("split_events"), model.splitEvents);
	root.insert(QStringLiteral("digest"), dawProjectModelDigest(model));

	QJsonArray mixers;
	for (const DawProjectMixerChannel& channel : model.mixerChannels)
	{
		mixers.append(QJsonObject{{QStringLiteral("id"), channel.id},
			{QStringLiteral("name"), channel.name},
			{QStringLiteral("role"), channel.role},
			{QStringLiteral("solo"), channel.solo},
			{QStringLiteral("mute"), channel.mute},
			{QStringLiteral("volume"), channel.volume},
			{QStringLiteral("audio_channels"), channel.audioChannels}});
	}
	root.insert(QStringLiteral("mixer_channels"), mixers);

	QJsonArray tracks;
	for (const DawProjectTrack& track : model.tracks)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), track.id);
		entry.insert(QStringLiteral("name"), track.name);
		entry.insert(QStringLiteral("color"), track.color);
		entry.insert(QStringLiteral("type"), track.typeName);
		entry.insert(QStringLiteral("content_type"), track.contentType);
		entry.insert(QStringLiteral("lost_content_type"), track.lostContentType);
		entry.insert(QStringLiteral("channel_id"), track.channelId);
		entry.insert(QStringLiteral("destination_channel_id"), track.destinationChannelId);
		entry.insert(QStringLiteral("solo"), track.solo);
		entry.insert(QStringLiteral("mute"), track.mute);
		entry.insert(QStringLiteral("has_volume"), track.hasVolume);
		entry.insert(QStringLiteral("volume"), track.volume);
		entry.insert(QStringLiteral("has_pan"), track.hasPan);
		entry.insert(QStringLiteral("pan"), track.pan);
		entry.insert(QStringLiteral("clip_count"), track.clipCount());
		entry.insert(QStringLiteral("note_count"), track.noteCount());
		QJsonArray clips;
		for (const DawProjectClip& clip : track.clips)
		{
			QJsonObject clipEntry;
			clipEntry.insert(QStringLiteral("time"), clip.time);
			clipEntry.insert(QStringLiteral("duration"), clip.duration);
			clipEntry.insert(QStringLiteral("play_start"), clip.playStart);
			clipEntry.insert(QStringLiteral("name"), clip.name);
			clipEntry.insert(QStringLiteral("note_count"), clip.notes.size());
			clips.append(clipEntry);
		}
		entry.insert(QStringLiteral("clips"), clips);
		tracks.append(entry);
	}
	root.insert(QStringLiteral("tracks"), tracks);

	QJsonArray tempoPoints;
	for (const DawProjectPoint& point : model.tempoPoints)
	{
		tempoPoints.append(QJsonObject{{QStringLiteral("time"), point.time},
			{QStringLiteral("bpm"), point.value}});
	}
	root.insert(QStringLiteral("tempo_points"), tempoPoints);
	QJsonArray meterPoints;
	for (const DawProjectPoint& point : model.meterPoints)
	{
		meterPoints.append(QJsonObject{{QStringLiteral("time"), point.time},
			{QStringLiteral("numerator"), point.numerator},
			{QStringLiteral("denominator"), point.denominator}});
	}
	root.insert(QStringLiteral("meter_points"), meterPoints);
	return root;
}

const DawProjectConvention& dawProjectConvention()
{
	static const DawProjectConvention convention = [] {
		DawProjectConvention value;
		value.formatName = QStringLiteral("DAWproject");
		value.formatVersion = QString::fromLatin1(DawProjectVersionAttribute);
		value.container = QStringLiteral("ZIP (RFC 1951-free: every entry is stored, not "
			"compressed; a stored entry is valid in every ZIP reader)");
		value.projectEntry = QString::fromLatin1(DawProjectProjectEntry);
		value.metaDataEntry = QString::fromLatin1(DawProjectMetaDataEntry);
		value.textEncoding = QStringLiteral("UTF-8");
		value.timeUnit = QString::fromLatin1(DawProjectTimeUnit);
		value.tickRule = QStringLiteral("one beat IS one quarter note and LMMS' grid is %1 ticks "
			"per quarter note, so a tick is exactly 1/%1 of a beat and a time value is written "
			"with six decimals (maximum error 2.4e-5 ticks, so reading back cannot land on a "
			"neighbouring tick); a FOREIGN time off that grid is rounded and reported in "
			"rounded_times").arg(DawProjectTicksPerQuarterNote);
		value.zipMethod = QStringLiteral("store (method 0)");
		value.contentTypeVocabulary = QStringLiteral("audio|automation|notes|video|markers|tracks "
			"(a space-separated list); LMMS maps instrument and pattern to notes, sample to "
			"audio, automation and hiddenautomation to automation, folder to tracks, and a type "
			"with no counterpart is written with an empty contentType and counted");
		value.statedLosses = QStringLiteral("NOT carried: audio clips and their media (no media is "
			"copied), automation clips and automation content, device/plugin state and "
			"parameters, sends, fades and crossfades, loop points, scenes and clip slots, folder "
			"nesting (tracks are flat in LMMS), the mixer's ROUTE graph and its pre/post-fader "
			"flags, and the sharing of one mixer channel by several tracks. NOT in the format: "
			"LMMS' take lanes, clip link groups, slide notes, note probability and detune. A "
			"tempo-map event carrying both a tempo and a metre half becomes TWO points, because "
			"the format has one timeline per half");
		return value;
	}();
	return convention;
}

} // namespace interchange
} // namespace lmms
