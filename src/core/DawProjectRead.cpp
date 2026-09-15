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

// The reader's own state and its attribute readers live in
// src/core/DawProjectReadShared.h, beside the walk over the track elements.
using namespace readdetail;

namespace
{

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
						doubleAttribute(attributes, QStringLiteral("value"), 120.0);
					if (state.model->tempo < DawProjectMinTempo
						|| state.model->tempo > DawProjectMaxTempo)
					{
						state.error = QStringLiteral("the document's tempo %1 is outside the "
							"engine's own %2..%3").arg(state.model->tempo)
							.arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
						return false;
					}
				}
				else if (reader.name() == QLatin1String("TimeSignature"))
				{
					state.model->numerator =
						intAttribute(attributes, QStringLiteral("numerator"), 4);
					state.model->denominator =
						intAttribute(attributes, QStringLiteral("denominator"), 4);
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
					const bool isMeter = child == QLatin1String("TimeSignatureAutomation");
					while (!reader.atEnd())
					{
						reader.readNext();
						if (reader.isEndElement() && reader.name() == child) { break; }
						if (!reader.isStartElement()) { continue; }
						const QStringView point = reader.name();
						if (point == QLatin1String("Target")) { skipElement(reader); continue; }
						const QXmlStreamAttributes attributes = reader.attributes();
						DawProjectPoint entry;
						entry.isMeter = isMeter;
						entry.time = doubleAttribute(attributes, QStringLiteral("time"), 0.0);
						if (isMeter)
						{
							entry.numerator =
								intAttribute(attributes, QStringLiteral("numerator"), 4);
							entry.denominator =
								intAttribute(attributes, QStringLiteral("denominator"), 4);
							state.model->meterPoints.append(entry);
						}
						else
						{
							entry.value = doubleAttribute(attributes, QStringLiteral("value"),
								120.0);
							state.model->tempoPoints.append(entry);
						}
						skipElement(reader);
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
	return true;
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
