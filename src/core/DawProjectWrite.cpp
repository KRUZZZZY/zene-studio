/*
 * DawProjectWrite.cpp - the model as a DAWproject document.
 *
 * The writer half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7).
 * Every element name, attribute name and enumeration value this file emits is
 * one Project.xsd declares (version="1.0"); the quotations are collected in
 * docs/DAWPROJECT-INTERCHANGE.md, which is the document the release contract
 * asks for when it asks which version was implemented.
 *
 * THE DOCUMENT'S SHAPE is the schema's own sequence - Application, Transport,
 * Structure, Arrangement, Scenes - with every name this file emits reproduced
 * against its Project.xsd line in docs/DAWPROJECT-INTERCHANGE.md (section 2).
 * Two facts from it belong here: the mixer's strips are BARE <Channel> elements
 * inside <Structure>, before the <Track>s, and inside <Arrangement> the two
 * automation timelines come AFTER the lanes.
 *
 * IDS. A model that names its own id keeps it (elementId below); every other id
 * is `id<n>` in document order, assigned before the elements that point at it.
 *
 * WHAT IT REFUSES RATHER THAN ROUNDS: a tempo outside the engine's own bounds, a
 * metre whose denominator is not a power of two, or a clip whose lane has no
 * track id - the format could express all three, this engine cannot read them
 * back - plus the id rules in elementId below. See the doc, section 3.
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

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QXmlStreamWriter>

#include <cmath>
#include <vector>

#include "DawProjectInterchange.h"

namespace lmms
{
namespace interchange
{

//! The decimal places every time value is written with. Six is the format's
//! own example ("0.250000") and is three orders of magnitude finer than LMMS'
//! grid: 48 ticks per beat * 5e-7 beat = 2.4e-5 ticks of maximum error, so the
//! reader's rounding to the tick cannot land on a neighbour.
constexpr int TimeDecimals = 6;
//! The decimal places a parameter value is written with (the format's example
//! writes six: "0.659140").
constexpr int ParameterDecimals = 6;

namespace
{

QString number(double value, int decimals)
{
	QString text = QString::number(value, 'f', decimals);
	// "-0.000000" is not a value the format has any use for and compares
	// badly against "0.000000" on a round trip.
	if (text.startsWith(QLatin1Char('-')))
	{
		bool allZero = true;
		for (int index = 1; index < text.size(); index++)
		{
			const QChar character = text.at(index);
			if (character != QLatin1Char('0') && character != QLatin1Char('.')) { allZero = false; break; }
		}
		if (allZero) { text = text.mid(1); }
	}
	return text;
}

//! "<index>" -> "id<index>". One counter for the whole document, in document order.
QString idFor(int* counter) { return QStringLiteral("id%1").arg((*counter)++); }

//! A generated id, reserved so nothing after it can be given the same one.
QString freshId(int* counter, QSet<QString>& used)
{
	QString id;
	do { id = idFor(counter); } while (used.contains(id));
	used.insert(id);
	return id;
}

//! The id an element gets: the MODEL's own when the schema allows it and it is
//! not taken, else a generated one - reserved either way, so this writer can
//! never produce a document with a duplicate xs:ID.
QString elementId(int* counter, const QString& candidate, QSet<QString>& used)
{
	const QString id = dawProjectUsableDocumentId(candidate) && !used.contains(candidate)
		? candidate : freshId(counter, used);
	used.insert(id);
	return id;
}

QString beatsText(double beats) { return number(beats, TimeDecimals); }

bool isPowerOfTwo(int value)
{
	return value > 0 && (value & (value - 1)) == 0;
}

//! The document's own reference to a parameter: automationTarget/parameter is an
//! xs:IDREF, so it names the id of the parameter the timeline drives.
void writeTarget(QXmlStreamWriter& writer, const QString& parameterId)
{
	writer.writeStartElement(QStringLiteral("Target"));
	writer.writeAttribute(QStringLiteral("parameter"), parameterId);
	writer.writeEndElement();
}

void writeValueParameter(QXmlStreamWriter& writer, const QString& element, const QString& name,
	const QString& unit, double value, double minimum, double maximum, const QString& id)
{
	writer.writeStartElement(element);
	writer.writeAttribute(QStringLiteral("max"), number(maximum, ParameterDecimals));
	writer.writeAttribute(QStringLiteral("min"), number(minimum, ParameterDecimals));
	writer.writeAttribute(QStringLiteral("unit"), unit);
	writer.writeAttribute(QStringLiteral("value"), number(value, ParameterDecimals));
	writer.writeAttribute(QStringLiteral("id"), id);
	writer.writeAttribute(QStringLiteral("name"), name);
	writer.writeEndElement();
}

void writeBoolParameter(QXmlStreamWriter& writer, const QString& element, const QString& name,
	bool value, const QString& id)
{
	writer.writeStartElement(element);
	writer.writeAttribute(QStringLiteral("value"),
		value ? QStringLiteral("true") : QStringLiteral("false"));
	writer.writeAttribute(QStringLiteral("id"), id);
	writer.writeAttribute(QStringLiteral("name"), name);
	writer.writeEndElement();
}

} // namespace

bool dawProjectXmlFromModel(const DawProjectModel& model, QByteArray* xml, QString* error)
{
	if (model.tempo < DawProjectMinTempo || model.tempo > DawProjectMaxTempo)
	{
		if (error)
		{
			*error = QStringLiteral("the session tempo %1 is outside the engine's own %2..%3, so "
				"the file would say something this engine cannot read back")
				.arg(model.tempo).arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
		}
		return false;
	}
	if (!isPowerOfTwo(model.denominator))
	{
		if (error)
		{
			*error = QStringLiteral("the time signature's denominator %1 is not a power of two")
				.arg(model.denominator);
		}
		return false;
	}
	for (const DawProjectPoint& point : model.tempoPoints)
	{
		if (point.value < DawProjectMinTempo || point.value > DawProjectMaxTempo)
		{
			if (error)
			{
				*error = QStringLiteral("a tempo-map point at beat %1 carries %2 bpm, outside the "
					"engine's own %3..%4").arg(point.time).arg(point.value)
					.arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
			}
			return false;
		}
	}
	for (const DawProjectPoint& point : model.meterPoints)
	{
		if (!isPowerOfTwo(point.denominator))
		{
			if (error)
			{
				*error = QStringLiteral("a metre point at beat %1 has denominator %2, which is not a "
					"power of two").arg(point.time).arg(point.denominator);
			}
			return false;
		}
	}

	QByteArray bytes;
	QXmlStreamWriter writer(&bytes);
	writer.setAutoFormatting(false);
	writer.writeStartDocument(QStringLiteral("1.0"), true);

	int counter = 0;
	QSet<QString> used;
	writer.writeStartElement(QStringLiteral("Project"));
	writer.writeAttribute(QStringLiteral("version"), DawProjectVersionAttribute);

	writer.writeStartElement(QStringLiteral("Application"));
	writer.writeAttribute(QStringLiteral("name"), model.applicationName);
	writer.writeAttribute(QStringLiteral("version"), model.applicationVersion);
	writer.writeEndElement();

	// The Transport comes before the Structure, and its two parameters' ids are
	// what the two automation timelines' <Target> elements refer to.
	writer.writeStartElement(QStringLiteral("Transport"));
	const QString tempoId = freshId(&counter, used);
	writeValueParameter(writer, QStringLiteral("Tempo"), QStringLiteral("Tempo"),
		QStringLiteral("bpm"), model.tempo, DawProjectMinTempo, DawProjectMaxTempo, tempoId);
	const QString meterId = freshId(&counter, used);
	writer.writeStartElement(QStringLiteral("TimeSignature"));
	writer.writeAttribute(QStringLiteral("denominator"), QString::number(model.denominator));
	writer.writeAttribute(QStringLiteral("numerator"), QString::number(model.numerator));
	writer.writeAttribute(QStringLiteral("id"), meterId);
	writer.writeEndElement();
	writer.writeEndElement();

	writer.writeStartElement(QStringLiteral("Structure"));
	// The mixer's own strips come FIRST as bare <Channel> elements (<Structure>
	// is a choice of Track | Channel): an LMMS MixerChannel is a summing strip
	// several tracks may feed, and writing it bare is what makes the IDREF below
	// mean what the format says it means.
	QVector<QString> mixerIds;
	mixerIds.reserve(model.mixerChannels.size());
	for (const DawProjectMixerChannel& channel : model.mixerChannels)
	{
		// The model's own strip id (the session path names them `mixer<n>`), so
		// `destination` below points at the id the model carries.
		const QString channelId = elementId(&counter, channel.id, used);
		mixerIds.append(channelId);
		writer.writeStartElement(QStringLiteral("Channel"));
		writer.writeAttribute(QStringLiteral("audioChannels"),
			QString::number(channel.audioChannels));
		if (!channel.role.isEmpty())
		{
			writer.writeAttribute(QStringLiteral("role"), channel.role);
		}
		writer.writeAttribute(QStringLiteral("solo"),
			channel.solo ? QStringLiteral("true") : QStringLiteral("false"));
		writer.writeAttribute(QStringLiteral("id"), channelId);
		if (!channel.name.isEmpty())
		{
			writer.writeAttribute(QStringLiteral("name"), channel.name);
		}
		writeValueParameter(writer, QStringLiteral("Volume"), QStringLiteral("Volume"),
			QStringLiteral("linear"), channel.volume, 0.0, 2.0, freshId(&counter, used));
		writeBoolParameter(writer, QStringLiteral("Mute"), QStringLiteral("Mute"), channel.mute,
			freshId(&counter, used));
		writer.writeEndElement();  // Channel (bare, a mixer strip)
	}

	//! The IDREF joining a track to its strip: by conversion-time mixer index
	//! when there is one (the session path), else by the id the file used.
	const auto destinationFor = [&model, &mixerIds](const DawProjectTrack& track) -> QString {
		if (track.mixerChannelIndex >= 0)
		{
			for (int index = 0; index < model.mixerChannels.size(); index++)
			{
				if (model.mixerChannels[index].index == track.mixerChannelIndex)
				{
					return mixerIds[index];
				}
			}
		}
		if (!track.destinationChannelId.isEmpty())
		{
			for (int index = 0; index < model.mixerChannels.size(); index++)
			{
				if (model.mixerChannels[index].id == track.destinationChannelId)
				{
					return mixerIds[index];
				}
			}
		}
		return QString();
	};

	QVector<QString> trackIds;
	trackIds.reserve(model.tracks.size());
	for (const DawProjectTrack& track : model.tracks)
	{
		const QString trackId = elementId(&counter, track.id, used);
		trackIds.append(trackId);

		writer.writeStartElement(QStringLiteral("Track"));
		if (!track.contentType.isEmpty())
		{
			writer.writeAttribute(QStringLiteral("contentType"), track.contentType);
		}
		writer.writeAttribute(QStringLiteral("loaded"), QStringLiteral("true"));
		writer.writeAttribute(QStringLiteral("id"), trackId);
		if (!track.name.isEmpty()) { writer.writeAttribute(QStringLiteral("name"), track.name); }
		if (!track.color.isEmpty()) { writer.writeAttribute(QStringLiteral("color"), track.color); }

		writer.writeStartElement(QStringLiteral("Channel"));
		writer.writeAttribute(QStringLiteral("solo"),
			track.solo ? QStringLiteral("true") : QStringLiteral("false"));
		if (!track.channelId.isEmpty())
		{
			writer.writeAttribute(QStringLiteral("id"),
				elementId(&counter, track.channelId, used));
		}
		if (!track.name.isEmpty()) { writer.writeAttribute(QStringLiteral("name"), track.name); }
		const QString destination = destinationFor(track);
		if (!destination.isEmpty())
		{
			writer.writeAttribute(QStringLiteral("destination"), destination);
		}
		// The TRACK's own volume (LMMS' 0..200 scale, unity 100 - scaled by the
		// caller). The mixer strips above carry the faders.
		if (track.hasVolume)
		{
			writeValueParameter(writer, QStringLiteral("Volume"), QStringLiteral("Volume"),
				QStringLiteral("linear"), track.volume, 0.0, 2.0, freshId(&counter, used));
		}
		writeBoolParameter(writer, QStringLiteral("Mute"), QStringLiteral("Mute"), track.mute,
			freshId(&counter, used));
		// LOSSY #6: written only where the track type HAS a panning model. A
		// channel with none leaves the attribute out, and a reader sees the
		// format's own default (0.5, centre) - which is the pan such a track
		// plays at anyway, because DefaultPanning is PanningCenter.
		if (track.hasPan)
		{
			writeValueParameter(writer, QStringLiteral("Pan"), QStringLiteral("Pan"),
				QStringLiteral("normalized"), track.pan, 0.0, 1.0, freshId(&counter, used));
		}
		writer.writeEndElement();  // Channel (the track's own strip)
		writer.writeEndElement();  // Track
	}
	writer.writeEndElement();  // Structure

	writer.writeStartElement(QStringLiteral("Arrangement"));
	writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));

	writer.writeStartElement(QStringLiteral("Lanes"));
	writer.writeAttribute(QStringLiteral("timeUnit"), QString::fromLatin1(DawProjectTimeUnit));
	writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));
	for (int index = 0; index < model.tracks.size(); index++)
	{
		const DawProjectTrack& track = model.tracks[index];
		writer.writeStartElement(QStringLiteral("Lanes"));
		writer.writeAttribute(QStringLiteral("track"), trackIds[index]);
		writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));
		writer.writeStartElement(QStringLiteral("Clips"));
		writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));
		for (const DawProjectClip& clip : track.clips)
		{
			writer.writeStartElement(QStringLiteral("Clip"));
			writer.writeAttribute(QStringLiteral("time"), beatsText(clip.time));
			writer.writeAttribute(QStringLiteral("duration"), beatsText(clip.duration));
			writer.writeAttribute(QStringLiteral("playStart"), beatsText(clip.playStart));
			if (!clip.name.isEmpty()) { writer.writeAttribute(QStringLiteral("name"), clip.name); }
			if (!clip.color.isEmpty())
			{
				writer.writeAttribute(QStringLiteral("color"), clip.color);
			}
			writer.writeStartElement(QStringLiteral("Notes"));
			writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));
			for (const DawProjectNote& note : clip.notes)
			{
				writer.writeStartElement(QStringLiteral("Note"));
				writer.writeAttribute(QStringLiteral("time"), beatsText(note.time));
				writer.writeAttribute(QStringLiteral("duration"), beatsText(note.duration));
				writer.writeAttribute(QStringLiteral("channel"), QString::number(note.channel));
				writer.writeAttribute(QStringLiteral("key"), QString::number(note.key));
				writer.writeAttribute(QStringLiteral("vel"), number(note.vel, ParameterDecimals));
				writer.writeAttribute(QStringLiteral("rel"), number(note.rel, ParameterDecimals));
				writer.writeEndElement();
			}
			writer.writeEndElement();  // Notes
			writer.writeEndElement();  // Clip
		}
		writer.writeEndElement();  // Clips
		writer.writeEndElement();  // Lanes (per track)
	}
	writer.writeEndElement();  // Lanes

	// The map's two arms, AFTER the lanes (the schema's own sequence). An
	// interpolation of "hold" is the honest value: LMMS' tempo map holds steps
	// and has no curve to write.
	if (!model.tempoPoints.isEmpty())
	{
		writer.writeStartElement(QStringLiteral("TempoAutomation"));
		writer.writeAttribute(QStringLiteral("timeUnit"), QString::fromLatin1(DawProjectTimeUnit));
		writer.writeAttribute(QStringLiteral("unit"), QStringLiteral("bpm"));
		writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));
		writeTarget(writer, tempoId);
		for (const DawProjectPoint& point : model.tempoPoints)
		{
			writer.writeStartElement(QStringLiteral("RealPoint"));
			writer.writeAttribute(QStringLiteral("time"), beatsText(point.time));
			writer.writeAttribute(QStringLiteral("value"), number(point.value, ParameterDecimals));
			writer.writeAttribute(QStringLiteral("interpolation"), QStringLiteral("hold"));
			writer.writeEndElement();
		}
		writer.writeEndElement();
	}
	if (!model.meterPoints.isEmpty())
	{
		writer.writeStartElement(QStringLiteral("TimeSignatureAutomation"));
		writer.writeAttribute(QStringLiteral("timeUnit"), QString::fromLatin1(DawProjectTimeUnit));
		writer.writeAttribute(QStringLiteral("id"), freshId(&counter, used));
		writeTarget(writer, meterId);
		for (const DawProjectPoint& point : model.meterPoints)
		{
			writer.writeStartElement(QStringLiteral("TimeSignaturePoint"));
			writer.writeAttribute(QStringLiteral("time"), beatsText(point.time));
			writer.writeAttribute(QStringLiteral("numerator"), QString::number(point.numerator));
			writer.writeAttribute(QStringLiteral("denominator"),
				QString::number(point.denominator));
			writer.writeEndElement();
		}
		writer.writeEndElement();
	}
	writer.writeEndElement();  // Arrangement

	writer.writeStartElement(QStringLiteral("Scenes"));
	writer.writeEndElement();
	writer.writeEndElement();  // Project
	writer.writeEndDocument();

	*xml = bytes;
	return true;
}

bool writeDawProject(const QString& path, const DawProjectModel& model,
	DawProjectWriteReport* report, QString* error)
{
	if (!QFileInfo(path).isAbsolute())
	{
		if (error) { *error = QStringLiteral("the container path must be absolute: %1").arg(path); }
		return false;
	}

	QByteArray projectXml;
	if (!dawProjectXmlFromModel(model, &projectXml, error)) { return false; }

	// The optional second entry. MetaData.xsd's root is <MetaData> with every
	// field optional; the one field this module can honestly fill is the
	// comment, because LMMS' session carries no title/artist/album metadata.
	QByteArray metaData;
	{
		QXmlStreamWriter writer(&metaData);
		writer.writeStartDocument(QStringLiteral("1.0"), true);
		writer.writeStartElement(QStringLiteral("MetaData"));
		writer.writeStartElement(QStringLiteral("Comment"));
		writer.writeCharacters(QStringLiteral("Exported by %1 %2 as DAWproject %3.")
			.arg(model.applicationName, model.applicationVersion,
				QString::fromLatin1(DawProjectVersionAttribute)));
		writer.writeEndElement();
		writer.writeEndElement();
		writer.writeEndDocument();
	}

	std::vector<DawProjectEntry> entries;
	entries.push_back({QString::fromLatin1(DawProjectProjectEntry), projectXml});
	entries.push_back({QString::fromLatin1(DawProjectMetaDataEntry), metaData});

	if (!dawProjectZipWrite(path, entries, error)) { return false; }

	QFile written(path);
	qint64 bytes = 0;
	if (written.open(QIODevice::ReadOnly))
	{
		bytes = written.size();
		written.close();
	}
	if (report != nullptr)
	{
		report->path = path;
		report->bytes = bytes;
		report->sha256 = dawProjectSha256(projectXml);
		report->trackCount = model.trackCount();
		report->mixerChannelCount = model.mixerChannelCount();
		report->clipCount = model.clipCount();
		report->noteCount = model.noteCount();
		report->tempoPointCount = model.tempoPoints.size();
		report->meterPointCount = model.meterPoints.size();
		report->modelDigest = dawProjectModelDigest(model);
		report->loss = DawProjectLossReport();
	}
	return true;
}

} // namespace interchange
} // namespace lmms
