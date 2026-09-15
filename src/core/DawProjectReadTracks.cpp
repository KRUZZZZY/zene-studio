/*
 * DawProjectRead.cpp - a DAWproject document as the model.
 *
 * The reader half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7). It
 * reads a file a FOREIGN program wrote as readily as one this module wrote, so
 * everything it does is a decision about a document it did not produce:
 *
 *   - The version is checked, not assumed. Project.xsd declares version="1.0"
 *     on the root and the README says the format "is version 1.0 and is
 *     stable"; a document declaring another MAJOR version is REFUSED with the
 *     version named. Reading a 2.0 document with 1.0's assumptions is how a
 *     format dialect starts.
 *   - A document with no <Project> root, no <Application>, or a <Tempo> whose
 *     value the engine will not accept is refused. <Application> and the root's
 *     version attribute are `use="required"` in Project.xsd, so their absence
 *     is a malformed document rather than a default.
 *   - A timeline that declares seconds AND carries notes is refused. LMMS
 *     places notes on a beat grid; converting a seconds timeline needs the
 *     tempo curve in force, and a silent misplacement is worse than a named
 *     refusal. (Audio timelines are seconds in the format's own example and are
 *     skipped whole: this module carries notes and not audio - LOSSY #1.)
 *   - Track elements NEST in the format and LMMS' container is FLAT, so a
 *     nested track is read at the container root and the relation is counted as
 *     lost. A document that nests is not refused - its tracks are all there.
 *   - Times are beats, and LMMS' grid is 48 ticks per beat, so a foreign time
 *     that is not on that grid is ROUNDED and counted (`rounded_times`). The
 *     count is reported rather than hidden, the same rule the SMF reader
 *     follows for a foreign division.
 *
 * WHAT IT DOES NOT READ, and says so: <Devices> (plugin and built-in device
 * state), <Channel>/<Sends>, fades, loop points, <Audio>, <Video>, <Markers>,
 * <Scenes> and <ClipSlot>. A document carrying any of them reads fine; the
 * parts this module does not carry are counted in the loss report instead of
 * being silently dropped.
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
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include "DawProjectInterchange.h"
#include "DawProjectReadShared.h"

namespace lmms
{
namespace interchange
{
namespace readdetail
{


void parseNotes(QXmlStreamReader& reader, ReadState& state, DawProjectClip* clip)
{
	// The unit that matters is the one in force where the NOTES are, which the
	// format's own example leaves to the enclosing lanes.
	if (state.timeUnit == QLatin1String("seconds"))
	{
		if (state.error.isEmpty())
		{
			state.error = QStringLiteral("a <Notes> timeline declares timeUnit=\"seconds\"; this "
				"module places notes on a beat grid and will not guess the tempo curve that would "
				"convert them");
		}
		skipElement(reader);
		return;
	}
	while (!reader.atEnd())
	{
		reader.readNext();
		if (reader.isEndElement() && reader.name() == QLatin1String("Notes")) { break; }
		if (!reader.isStartElement()) { continue; }
		if (reader.name() != QLatin1String("Note")) { skipElement(reader); continue; }
		const QXmlStreamAttributes attributes = reader.attributes();
		DawProjectNote note;
		note.time = static_cast<double>(ticksFromBeats(state,
			doubleAttribute(attributes, QStringLiteral("time"), 0.0)))
			/ DawProjectTicksPerQuarterNote;
		note.duration = static_cast<double>(ticksFromBeats(state,
			doubleAttribute(attributes, QStringLiteral("duration"), 0.0)))
			/ DawProjectTicksPerQuarterNote;
		note.channel = intAttribute(attributes, QStringLiteral("channel"), 0);
		note.key = intAttribute(attributes, QStringLiteral("key"), 60);
		note.vel = doubleAttribute(attributes, QStringLiteral("vel"), 0.5);
		note.rel = doubleAttribute(attributes, QStringLiteral("rel"), note.vel);
		clip->notes.append(note);
	}
}

void parseClips(QXmlStreamReader& reader, ReadState& state)
{
	while (!reader.atEnd())
	{
		reader.readNext();
		if (reader.isEndElement() && reader.name() == QLatin1String("Clips")) { break; }
		if (!reader.isStartElement()) { continue; }
		if (reader.name() != QLatin1String("Clip")) { skipElement(reader); continue; }
		if (state.currentTrack < 0)
		{
			// A clip in a lane that names no track has nowhere to go; skipping it
			// would lose it silently, so it is a refusal.
			if (state.error.isEmpty())
			{
				state.error = QStringLiteral("a <Clip> sits in a lane that names no track, so it has "
					"no track to belong to");
			}
			skipElement(reader);
			continue;
		}
		const QXmlStreamAttributes attributes = reader.attributes();
		DawProjectClip clip;
		clip.time = static_cast<double>(ticksFromBeats(state,
			doubleAttribute(attributes, QStringLiteral("time"), 0.0)))
			/ DawProjectTicksPerQuarterNote;
		clip.duration = static_cast<double>(ticksFromBeats(state,
			doubleAttribute(attributes, QStringLiteral("duration"), 0.0)))
			/ DawProjectTicksPerQuarterNote;
		clip.playStart = static_cast<double>(ticksFromBeats(state,
			doubleAttribute(attributes, QStringLiteral("playStart"), 0.0)))
			/ DawProjectTicksPerQuarterNote;
		clip.name = attribute(attributes, QStringLiteral("name"));
		clip.color = attribute(attributes, QStringLiteral("color"));

		// The clip's own children: <Notes> is what this module carries, and the
		// rest (a nested <Clips> for an audio clip's events, <Audio>, <Warps>,
		// <Video>, <Points>, a <ClipSlot>) is counted and skipped.
		bool hasNotes = false;
		while (!reader.atEnd())
		{
			reader.readNext();
			if (reader.isEndElement() && reader.name() == QLatin1String("Clip")) { break; }
			if (!reader.isStartElement()) { continue; }
			const QStringView child = reader.name();
			if (child == QLatin1String("Notes"))
			{
				hasNotes = true;
				parseNotes(reader, state, &clip);
			}
			else if (child == QLatin1String("Clips") || child == QLatin1String("Audio")
				|| child == QLatin1String("Warps") || child == QLatin1String("Video")
				|| child == QLatin1String("Points") || child == QLatin1String("Lanes")
				|| child == QLatin1String("Timeline"))
			{
				state.report->loss.audioClipsSkipped++;
				skipElement(reader);
			}
			else { skipElement(reader); }
		}
		if (hasNotes)
		{
			state.model->tracks[state.currentTrack].clips.append(clip);
		}
		else if (clip.duration > 0.0)
		{
			// A clip with no <Notes> is a clip whose content this module does not
			// carry (audio, automation). It is counted, not silently dropped, and
			// the count distinguishes the two shapes the writer also names.
			if (!clip.name.isEmpty()) { state.report->loss.audioClipsSkipped++; }
			else { state.report->loss.automationClipsSkipped++; }
		}
	}
}

//! Walk the <Arrangement>/<Lanes> tree. `Lanes` nests, and a nested `Lanes`
//! carries the `track` IDREF of the track whose clips it holds - the shape the
//! format's own example uses.
void parseLanes(QXmlStreamReader& reader, ReadState& state)
{
	const int savedTrack = state.currentTrack;
	const QString savedUnit = state.timeUnit;
	while (!reader.atEnd())
	{
		reader.readNext();
		if (reader.isEndElement() && reader.name() == QLatin1String("Lanes")) { break; }
		if (!reader.isStartElement()) { continue; }
		const QStringView element = reader.name();
		if (element == QLatin1String("Lanes"))
		{
			const QXmlStreamAttributes attributes = reader.attributes();
			const QString unit = attribute(attributes, QStringLiteral("timeUnit"));
			if (!unit.isEmpty()) { state.timeUnit = unit; }
			const QString trackId = attribute(attributes, QStringLiteral("track"));
			if (!trackId.isEmpty())
			{
				state.currentTrack = state.trackIndexById.value(trackId, -1);
			}
			parseLanes(reader, state);
			state.currentTrack = savedTrack;
			state.timeUnit = savedUnit;
		}
		else if (element == QLatin1String("Clips")) { parseClips(reader, state); }
		else if (element == QLatin1String("Notes") || element == QLatin1String("ClipSlot")
			|| element == QLatin1String("markers") || element == QLatin1String("Points")
			|| element == QLatin1String("Warps") || element == QLatin1String("Audio")
			|| element == QLatin1String("Video") || element == QLatin1String("Timeline"))
		{
			skipElement(reader);
		}
		else { skipElement(reader); }
	}
	state.currentTrack = savedTrack;
	state.timeUnit = savedUnit;
}

//! One <Track>, at any depth. NESTED TRACKS ARE READ AT THE ROOT (LOSSY #4):
//! the format nests and LMMS' container is flat, so the relation is counted
//! rather than invented.
void parseTrack(QXmlStreamReader& reader, ReadState& state, int depth)
{
	DawProjectTrack track;
	const QXmlStreamAttributes attributes = reader.attributes();
	track.id = attribute(attributes, QStringLiteral("id"));
	track.name = attribute(attributes, QStringLiteral("name"));
	track.color = attribute(attributes, QStringLiteral("color"));
	track.contentType = attribute(attributes, QStringLiteral("contentType"));
	track.typeName = dawProjectTypeForContentType(track.contentType, &track.lostContentType);
	if (track.lostContentType) { state.report->loss.unmappedTrackTypes++; }
	if (depth > 0) { state.report->loss.folderChildrenLost++; }

	const int index = state.model->tracks.size();
	state.model->tracks.append(track);
	if (!track.id.isEmpty()) { state.trackIndexById.insert(track.id, index); }

	DawProjectTrack& target = state.model->tracks[index];
	while (!reader.atEnd())
	{
		reader.readNext();
		if (reader.isEndElement() && reader.name() == QLatin1String("Track")) { break; }
		if (!reader.isStartElement()) { continue; }
		const QStringView element = reader.name();
		if (element == QLatin1String("Channel"))
		{
			const QXmlStreamAttributes channelAttributes = reader.attributes();
			target.channelId = attribute(channelAttributes, QStringLiteral("id"));
			target.audioChannels =
				intAttribute(channelAttributes, QStringLiteral("audioChannels"), 2);
			target.solo = flagAttribute(channelAttributes, QStringLiteral("solo"));
			const QString role = attribute(channelAttributes, QStringLiteral("role"));
			if (!role.isEmpty()) { target.channelRole = role; }
			while (!reader.atEnd())
			{
				reader.readNext();
				if (reader.isEndElement() && reader.name() == QLatin1String("Channel")) { break; }
				if (!reader.isStartElement()) { continue; }
				const QStringView child = reader.name();
				const QXmlStreamAttributes childAttributes = reader.attributes();
				if (child == QLatin1String("Volume"))
				{
					target.volume = doubleAttribute(childAttributes, QStringLiteral("value"), 1.0);
				}
				else if (child == QLatin1String("Mute"))
				{
					target.mute = flagAttribute(childAttributes, QStringLiteral("value"));
				}
				else if (child == QLatin1String("Pan"))
				{
					target.hasPan = true;
					target.pan = doubleAttribute(childAttributes, QStringLiteral("value"), 0.5);
				}
				else if (child == QLatin1String("Sends"))
				{
					state.report->loss.sendsNotWritten++;
					skipElement(reader);
				}
				else if (child == QLatin1String("Devices"))
				{
					state.report->loss.devicesNotWritten++;
					skipElement(reader);
				}
				else { skipElement(reader); }
			}
		}
		else if (element == QLatin1String("Track")) { parseTrack(reader, state, depth + 1); }
		else { skipElement(reader); }
	}
}

} // namespace readdetail
} // namespace interchange
} // namespace lmms
