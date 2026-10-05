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
#include "TempoMap.h"

namespace lmms
{
namespace interchange
{

// The reader's own state and its attribute readers live in
// src/core/DawProjectReadShared.h, beside the walk over the track elements.
using namespace readdetail;

namespace
{

//! True when @a tempo is one the engine accepts (NaN is not: both comparisons fail); otherwise
//! false with state.error naming it - a <Tempo> and a tempo automation point alike.
bool tempoAccepted(double tempo, ReadState& state)
{
	if (tempo >= DawProjectMinTempo && tempo <= DawProjectMaxTempo) { return true; }
	state.error = QStringLiteral("the document's tempo %1 is outside the "
		"engine's own %2..%3").arg(tempo).arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
	return false;
}

//! True when the engine will accept this metre (the same closed rule
//! TempoMap::validEvent applies: a numerator inside [1, 32] and a denominator
//! that is a power of two). Otherwise false, with state.error naming the value:
//! a metre the engine cannot hold is carried into the session as an unusable
//! signature, and the fallback of 4/4 would report a document's own silence.
bool meterAccepted(int numerator, int denominator, ReadState& state)
{
	if (numerator < TempoMapMinNumerator || numerator > TempoMapMaxNumerator)
	{
		state.error = QStringLiteral("the document's time signature numerator %1 is outside the "
			"engine's own %2..%3").arg(numerator).arg(TempoMapMinNumerator)
			.arg(TempoMapMaxNumerator);
		return false;
	}
	if (!isSupportedTempoMapDenominator(denominator))
	{
		state.error = QStringLiteral("the document's time signature denominator %1 is not one the "
			"engine's meter uses (a power of two up to %2)").arg(denominator)
			.arg(TempoMapLargestDenominator);
		return false;
	}
	return true;
}

//! The point's `time` attribute as BEATS on LMMS' 48-tick grid - the same
//! conversion the notes and clips go through, so an automation point off the
//! grid is normalised and counted (`rounded_times`) rather than kept as raw
//! beats. False - with state.error set - for a time the conversion refuses.
bool pointTime(const QXmlStreamAttributes& attributes, ReadState& state, double* beats)
{
	const double raw = doubleAttribute(attributes, state, QStringLiteral("time"), 0.0);
	if (!dawProjectBeatRepresentable(raw))
	{
		state.error = QStringLiteral("a map point at time %1 is not a time this engine's "
			"48-tick-per-beat grid can express").arg(raw, 0, 'g', 17);
		return false;
	}
	*beats = static_cast<double>(ticksFromBeats(state, raw)) / DawProjectTicksPerQuarterNote;
	return true;
}

//! The unit in force for a WHOLE <TempoAutomation> element: its own `timeUnit`
//! when it declares one, else the one the enclosing lanes left in state. False -
//! with state.error set - for a seconds timeline, refused the way a seconds
//! <Notes> lane is: converting it needs the tempo curve, which this module will
//! not guess.
bool automationUnitAccepted(QXmlStreamReader& reader, ReadState& state, const QString& element)
{
	QString unit = attribute(reader.attributes(), QStringLiteral("timeUnit"));
	if (unit.isEmpty()) { unit = state.timeUnit; }
	if (unit != QLatin1String("seconds")) { return true; }
	state.error = QStringLiteral("a <%1> timeline declares timeUnit=\"seconds\"; this module "
		"places its points on a beat grid and will not guess the tempo curve that would "
		"convert them").arg(element);
	return false;
}

//! One point of the map, read and checked: its time on the 48-tick grid, then
//! its value - a tempo or a metre - against the engine's own rule. False - with
//! state.error set - when either refuses.
bool parseAutomationPoint(const QXmlStreamAttributes& attributes, ReadState& state, bool isMeter)
{
	DawProjectPoint entry;
	entry.isMeter = isMeter;
	if (!pointTime(attributes, state, &entry.time)) { return false; }
	if (isMeter)
	{
		entry.numerator = intAttribute(attributes, state, QStringLiteral("numerator"), 4);
		entry.denominator = intAttribute(attributes, state, QStringLiteral("denominator"), 4);
		if (!meterAccepted(entry.numerator, entry.denominator, state)) { return false; }
		state.model->meterPoints.append(entry);
		return true;
	}
	entry.value = doubleAttribute(attributes, state, QStringLiteral("value"), 120.0);
	if (!tempoAccepted(entry.value, state)) { return false; }
	state.model->tempoPoints.append(entry);
	return true;
}

//! The points of one <TempoAutomation> (or, @a isMeter, <TimeSignatureAutomation>). False - with
//! state.error set - for a tempo point the engine will not accept, as for the global <Tempo>.
bool parseAutomationPoints(QXmlStreamReader& reader, ReadState& state, bool isMeter)
{
	const QString element = isMeter ? QStringLiteral("TimeSignatureAutomation") : QStringLiteral("TempoAutomation");
	if (!automationUnitAccepted(reader, state, element)) { return false; }
	while (!reader.atEnd())
	{
		reader.readNext();
		if (reader.isEndElement() && reader.name() == element) { break; }
		if (!reader.isStartElement()) { continue; }
		if (reader.name() == QLatin1String("Target")) { skipElement(reader); continue; }
		if (!parseAutomationPoint(reader.attributes(), state, isMeter)) { return false; }
		skipElement(reader);
	}
	return true;
}

bool parseDocument(QXmlStreamReader& reader, ReadState& state)
{
	bool sawProject = false;
	while (!reader.atEnd())
	{
		reader.readNext();
		if (!reader.isStartElement()) { continue; }
		const QStringView element = reader.name();
		if (element == QLatin1String("Project"))
		{
			sawProject = true;
			const QXmlStreamAttributes attributes = reader.attributes();
			state.model->formatVersion = attribute(attributes, QStringLiteral("version"));
			if (!state.model->formatVersion.startsWith(
					QString::number(DawProjectMajorVersion) + QLatin1Char('.')))
			{
				state.error = QStringLiteral("the document declares DAWproject version %1; this "
					"module implements %2 exactly and will not read another MAJOR version with its "
					"own assumptions").arg(state.model->formatVersion,
						QString::fromLatin1(DawProjectVersionAttribute));
				return false;
			}
		}
		else if (element == QLatin1String("Application"))
		{
			const QXmlStreamAttributes attributes = reader.attributes();
			state.model->applicationName = attribute(attributes, QStringLiteral("name"));
			state.model->applicationVersion = attribute(attributes, QStringLiteral("version"));
			if (state.model->applicationName.isEmpty())
			{
				state.error = QStringLiteral("<Application> declares no name, which Project.xsd "
					"makes required");
				return false;
			}
			skipElement(reader);
		}
		else if (element == QLatin1String("Transport"))
		{
			while (!reader.atEnd())
			{
				reader.readNext();
				if (reader.isEndElement() && reader.name() == QLatin1String("Transport")) { break; }
				if (!reader.isStartElement()) { continue; }
				const QXmlStreamAttributes attributes = reader.attributes();
				if (reader.name() == QLatin1String("Tempo"))
				{
					state.model->tempo =
						doubleAttribute(attributes, state, QStringLiteral("value"), 120.0);
					if (!tempoAccepted(state.model->tempo, state)) { return false; }
				}
				else if (reader.name() == QLatin1String("TimeSignature"))
				{
					state.model->numerator =
						intAttribute(attributes, state, QStringLiteral("numerator"), 4);
					state.model->denominator =
						intAttribute(attributes, state, QStringLiteral("denominator"), 4);
					if (!meterAccepted(state.model->numerator, state.model->denominator, state))
					{
						return false;
					}
				}
				else { skipElement(reader); }
			}
		}
		else if (element == QLatin1String("Structure"))
		{
			while (!reader.atEnd())
			{
				reader.readNext();
				if (reader.isEndElement() && reader.name() == QLatin1String("Structure")) { break; }
				if (!reader.isStartElement()) { continue; }
				if (reader.name() == QLatin1String("Track")) { parseTrack(reader, state, 0); }
				else if (reader.name() == QLatin1String("Channel"))
				{
					parseMixerChannel(reader, state);
				}
				else { skipElement(reader); }
			}
		}
		else if (element == QLatin1String("Arrangement"))
		{
			while (!reader.atEnd())
			{
				reader.readNext();
				if (reader.isEndElement() && reader.name() == QLatin1String("Arrangement")) { break; }
				if (!reader.isStartElement()) { continue; }
				const QStringView child = reader.name();
				if (child == QLatin1String("Lanes"))
				{
					const QString unit = attribute(reader.attributes(), QStringLiteral("timeUnit"));
					if (!unit.isEmpty()) { state.timeUnit = unit; }
					parseLanes(reader, state);
				}
				else if (child == QLatin1String("TempoAutomation")
					|| child == QLatin1String("TimeSignatureAutomation"))
				{
					if (!parseAutomationPoints(reader, state, child == QLatin1String("TimeSignatureAutomation")))
					{
						return false;
					}
				}
				else { skipElement(reader); }
			}
		}
		else if (element == QLatin1String("Scenes") || element == QLatin1String("MetaData"))
		{
			skipElement(reader);
		}
		else { skipElement(reader); }
	}
	if (!sawProject)
	{
		state.error = QStringLiteral("the project.xml entry has no <Project> root element");
		return false;
	}
	// A refusal the nested walks recorded (a seconds <Notes> timeline, a <Clip> with no track, a
	// nesting past MaxNesting) fails the read: the walks are void and only leave state.error.
	return state.error.isEmpty();
}

} // namespace


bool dawProjectModelFromXml(const QByteArray& xml, DawProjectModel* model,
	DawProjectReadReport* report, QString* error)
{
	DawProjectModel parsed;
	DawProjectReadReport local;
	ReadState state;
	state.model = &parsed;
	state.report = &local;

	QXmlStreamReader reader(xml);
	if (!parseDocument(reader, state))
	{
		if (error)
		{
			*error = !state.error.isEmpty()
				? state.error
				: QStringLiteral("the document could not be parsed: %1").arg(reader.errorString());
		}
		return false;
	}
	if (reader.hasError())
	{
		if (error)
		{
			*error = QStringLiteral("the document could not be parsed: %1")
				.arg(reader.errorString());
		}
		return false;
	}

	local.ok = true;
	local.formatVersion = parsed.formatVersion;
	local.applicationName = parsed.applicationName;
	local.applicationVersion = parsed.applicationVersion;
	local.trackCount = parsed.trackCount();
	local.mixerChannelCount = parsed.mixerChannelCount();
	local.clipCount = parsed.clipCount();
	local.noteCount = parsed.noteCount();
	local.tempoPointCount = parsed.tempoPoints.size();
	local.meterPointCount = parsed.meterPoints.size();
	*model = parsed;
	if (report != nullptr) { *report = local; }
	return true;
}

bool readDawProject(const QString& path, DawProjectModel* model, DawProjectReadReport* report,
	QString* error)
{
	std::vector<DawProjectEntry> entries;
	if (!dawProjectZipRead(path, &entries, error)) { return false; }

	QByteArray projectXml;
	QStringList unused;
	bool hasMetaData = false;
	QString title;
	for (const DawProjectEntry& entry : entries)
	{
		if (entry.first == QLatin1String(DawProjectProjectEntry)) { projectXml = entry.second; }
		else if (entry.first == QLatin1String(DawProjectMetaDataEntry))
		{
			hasMetaData = true;
			QXmlStreamReader meta(entry.second);
			while (!meta.atEnd())
			{
				meta.readNext();
				if (meta.isStartElement() && meta.name() == QLatin1String("Title"))
				{
					title = meta.readElementText();
				}
			}
		}
		else { unused.append(entry.first); }
	}
	if (projectXml.isEmpty())
	{
		if (error)
		{
			*error = QStringLiteral("%1 has no '%2' entry, so it is not a DAWproject container")
				.arg(path, QString::fromLatin1(DawProjectProjectEntry));
		}
		return false;
	}

	DawProjectReadReport local;
	if (!dawProjectModelFromXml(projectXml, model, &local, error)) { return false; }
	local.hasMetaData = hasMetaData;
	local.title = title;
	local.unusedEntries = unused;
	if (report != nullptr) { *report = local; }
	return true;
}

} // namespace interchange
} // namespace lmms
