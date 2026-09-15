/*
 * ControlCommandsDawProject.cpp - the `dawproject.*` command group: import and
 *                                  export against the published format.
 *
 * Feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7: "DAWproject import /
 * export"). The engine half is include/DawProjectInterchange.h and its four
 * translations units (the container, the writer, the reader, the session
 * coupling); what this file holds to account is the SURFACE the release
 * contract (section 3.1) requires of it: registered commands with argument and
 * result schemas, typed refusals, and a reversibility class whose inverse
 * actually works.
 *
 * FOUR IDS, the same four the `interchange.*` group has, because it is the same
 * question asked of a different format:
 *
 *   dawproject.convention   the format's name and VERSION, the container's
 *                           entries, the time unit and LMMS' tick resolution,
 *                           and the stated losses - as data on the wire
 *   dawproject.export       write the session as a .dawproject container
 *   dawproject.read         read a container's model back (no session change:
 *                           what makes the round trip checkable against the
 *                           FILE and the MODEL rather than a hash)
 *   dawproject.import       apply a container's model to the session, one undo
 *
 * REVERSIBILITY. export and read do not touch the session - they read it or
 * write a file outside it - so they are not_mutating, the class project.save
 * and interchange.smf_export carry. import DOES mutate: it replaces the
 * session's tracks, tempo map, global tempo and metre and each track's mixer
 * strip. ONE undo restores all of it through a recorded ACTION checkpoint
 * carrying the captured document (include/ControlStructuralSupport.h's shape
 * for a deleted track, extended to the whole session).
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

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "DawProjectInterchange.h"
#include "Engine.h"
#include "MeterModel.h"
#include "Mixer.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

namespace dwp = interchange;

//! The typed refusal every id gives a bad path argument with.
ControlResult missingFile(const QString& path)
{
	return ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no such file: %1").arg(path));
}

QJsonObject conventionState()
{
	const dwp::DawProjectConvention& convention = dwp::dawProjectConvention();
	QJsonObject result;
	result.insert(QStringLiteral("format_name"), convention.formatName);
	result.insert(QStringLiteral("format_version"), convention.formatVersion);
	result.insert(QStringLiteral("major_version"), convention.majorVersion);
	result.insert(QStringLiteral("minor_version"), convention.minorVersion);
	result.insert(QStringLiteral("container"), convention.container);
	result.insert(QStringLiteral("project_entry"), convention.projectEntry);
	result.insert(QStringLiteral("metadata_entry"), convention.metaDataEntry);
	result.insert(QStringLiteral("text_encoding"), convention.textEncoding);
	result.insert(QStringLiteral("time_unit"), convention.timeUnit);
	result.insert(QStringLiteral("ticks_per_quarter"), convention.ticksPerQuarterNote);
	result.insert(QStringLiteral("tick_rule"), convention.tickRule);
	result.insert(QStringLiteral("zip_method"), convention.zipMethod);
	result.insert(QStringLiteral("content_type_vocabulary"), convention.contentTypeVocabulary);
	result.insert(QStringLiteral("stated_losses"), convention.statedLosses);
	return result;
}

void insertModelSummary(QJsonObject* result, const dwp::DawProjectModel& model)
{
	result->insert(QStringLiteral("format_version"), model.formatVersion);
	result->insert(QStringLiteral("application_name"), model.applicationName);
	result->insert(QStringLiteral("application_version"), model.applicationVersion);
	result->insert(QStringLiteral("track_count"), model.trackCount());
	result->insert(QStringLiteral("clip_count"), model.clipCount());
	result->insert(QStringLiteral("note_count"), model.noteCount());
	result->insert(QStringLiteral("tempo_point_count"), model.tempoPoints.size());
	result->insert(QStringLiteral("meter_point_count"), model.meterPoints.size());
	result->insert(QStringLiteral("split_map_events"), model.splitEvents);
	result->insert(QStringLiteral("model_digest"), dwp::dawProjectModelDigest(model));
}

//! The session state an import replaces, captured while it still exists.
struct SessionSnapshot
{
	std::vector<QString> trackXml;
	QVector<QJsonObject> channels;
	TempoMap map;
	int globalTempo = 120;
	int numerator = 4;
	int denominator = 4;
	//! The measured size of the captured document, counted against the undo
	//! stack's byte budget (addStructuralUndoStep's contract: a step carrying a
	//! document while counting as zero bytes would make the declared budget a
	//! lie).
	qint64 payloadBytes = 0;
	bool ok = false;
};

QJsonObject channelState(MixerChannel* channel)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("name"), channel->m_name);
	entry.insert(QStringLiteral("volume"), static_cast<double>(channel->m_volumeModel.value()));
	entry.insert(QStringLiteral("mute"), channel->m_muteModel.value());
	entry.insert(QStringLiteral("solo"), channel->m_soloModel.value());
	return entry;
}

/*! Capture the session. A track whose own XML exceeds the structural limit
 *  cannot be captured, so the snapshot REFUSES rather than recording a
 *  truncated track (a truncated track is a corrupt track) - the rule
 *  ControlStructuralSupport.h states. */
SessionSnapshot captureSession(Song* song)
{
	SessionSnapshot snapshot;
	for (Track* track : song->tracks())
	{
		QString xml;
		if (!captureTrackXml(track, &xml, StructuralSnapshotLimit))
		{
			snapshot.ok = false;
			return snapshot;
		}
		snapshot.trackXml.push_back(xml);
		snapshot.payloadBytes += xml.size() * sizeof(QChar);
	}
	Mixer* mixer = Engine::mixer();
	for (int index = 0; index < static_cast<int>(mixer->numChannels()); index++)
	{
		snapshot.channels.append(channelState(mixer->mixerChannel(index)));
	}
	snapshot.map = song->tempoMap().map();
	snapshot.globalTempo = static_cast<int>(song->getTempo());
	snapshot.numerator = song->getTimeSigModel().numeratorModel().value();
	snapshot.denominator = song->getTimeSigModel().denominatorModel().value();
	snapshot.ok = true;
	return snapshot;
}

//! Put a captured session back: the tracks first (through the same
//! Track::create(element, container) the project loader uses), then the mixer
//! strips, then the map and the two globals.
void restoreSession(Song* song, const SessionSnapshot& snapshot)
{
	song->clearAllTracks();
	for (const QString& xml : snapshot.trackXml)
	{
		restoreTrackFromXml(xml, song);
	}
	Mixer* mixer = Engine::mixer();
	for (int index = 0; index < snapshot.channels.size(); index++)
	{
		while (static_cast<int>(mixer->numChannels()) <= index) { mixer->createChannel(); }
		MixerChannel* channel = mixer->mixerChannel(index);
		const QJsonObject entry = snapshot.channels[index];
		channel->m_name = entry.value(QStringLiteral("name")).toString();
		channel->m_volumeModel.setValue(
			static_cast<float>(entry.value(QStringLiteral("volume")).toDouble()));
		channel->m_muteModel.setValue(entry.value(QStringLiteral("mute")).toBool());
		channel->m_soloModel.setValue(entry.value(QStringLiteral("solo")).toBool());
	}
	song->setTempo(static_cast<bpm_t>(snapshot.globalTempo));
	song->getTimeSigModel().numeratorModel().setValue(snapshot.numerator);
	song->getTimeSigModel().denominatorModel().setValue(snapshot.denominator);
	song->tempoMap().edit([&snapshot](TempoMap& map) { map = snapshot.map; return true; });
}

//! Why dawproject.import is reversible, in the words the transaction record uses.
QString importMechanism()
{
	return QStringLiteral("action checkpoint: an import replaces the whole session, so the inverse "
		"is a recorded step carrying the captured document - every track's own XML taken while the "
		"track was still alive, the mixer strips, the tempo map and the global tempo and metre - "
		"recreated through the same Track::create(element, container) and "
		"control::restoreTrackFromXml the project loader uses when the undo stack unwinds");
}

void registerConventionCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("dawproject.convention");
	cmd.group = QStringLiteral("dawproject");
	cmd.verb = QStringLiteral("convention");
	cmd.description = QStringLiteral("The DAWproject version and convention this build "
		"implements, as data: the format's name and version (README.md: \"The format is version "
		"1.0 and is stable\"; Project.xsd declares version=\"1.0\"), the container's two entry "
		"names and its text encoding, the time unit every time value is written in, LMMS' own "
		"ticks per quarter note and the exact rule between a tick and a beat, the ZIP method used, "
		"the contentType vocabulary and how LMMS' nine track types map onto it, and the STATED "
		"LOSSES - everything this module does not carry. A file declaring another MAJOR version is "
		"refused by dawproject.read/dawproject.import rather than read with this build's "
		"assumptions.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("format_name"), stringProperty()},
		{QStringLiteral("format_version"), stringProperty()},
		{QStringLiteral("major_version"), integerProperty()},
		{QStringLiteral("minor_version"), integerProperty()},
		{QStringLiteral("container"), stringProperty()},
		{QStringLiteral("project_entry"), stringProperty()},
		{QStringLiteral("metadata_entry"), stringProperty()},
		{QStringLiteral("text_encoding"), stringProperty()},
		{QStringLiteral("time_unit"), stringProperty()},
		{QStringLiteral("ticks_per_quarter"), integerProperty()},
		{QStringLiteral("tick_rule"), stringProperty()},
		{QStringLiteral("zip_method"), stringProperty()},
		{QStringLiteral("content_type_vocabulary"), stringProperty()},
		{QStringLiteral("stated_losses"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(conventionState()); };
	registry.registerCommand(cmd);
}

void registerExportCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("dawproject.export");
	cmd.group = QStringLiteral("dawproject");
	cmd.verb = QStringLiteral("export");
	cmd.description = QStringLiteral("Write the session as a .dawproject container: a ZIP whose "
		"project.xml carries the tracks, their clips and notes, the tempo map and each track's "
		"mixer strip, in the published format's own vocabulary at version 1.0. The file is written "
		"with STORE entries so it needs no compression dependency and every ZIP reader opens it. "
		"`loss` in the reply reports EXACTLY what the format could not carry from this session. "
		"Refuses an existing file unless `overwrite` is true, and refuses a relative path. Writes a "
		"file; changes nothing in the session.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("overwrite"), booleanProperty()},
	}, {QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("format_version"), stringProperty()},
		{QStringLiteral("application_name"), stringProperty()},
		{QStringLiteral("application_version"), stringProperty()},
		{QStringLiteral("track_count"), integerProperty()},
		{QStringLiteral("clip_count"), integerProperty()},
		{QStringLiteral("note_count"), integerProperty()},
		{QStringLiteral("tempo_point_count"), integerProperty()},
		{QStringLiteral("meter_point_count"), integerProperty()},
		{QStringLiteral("split_map_events"), integerProperty()},
		{QStringLiteral("model_digest"), stringProperty()},
		{QStringLiteral("loss"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo(path).isAbsolute())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'path' must be an absolute path"));
		}
		const bool overwrite = args.value(QStringLiteral("overwrite")).toBool();
		if (QFileInfo::exists(path) && !overwrite)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 already exists; pass overwrite true to replace it").arg(path));
		}

		Song* song = Engine::getSong();
		dwp::DawProjectLossReport loss;
		const dwp::DawProjectModel model = dwp::dawProjectModelFromSong(song, &loss);
		dwp::DawProjectWriteReport report;
		QString error;
		if (!dwp::writeDawProject(path, model, &report, &error))
		{
			return ControlResult::failure(ControlErrorKind::Refused, error);
		}

		QJsonObject result;
		insertModelSummary(&result, model);
		result.insert(QStringLiteral("path"), report.path);
		result.insert(QStringLiteral("bytes"), report.bytes);
		result.insert(QStringLiteral("sha256"), report.sha256);
		result.insert(QStringLiteral("loss"), loss.toJson());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerReadCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("dawproject.read");
	cmd.group = QStringLiteral("dawproject");
	cmd.verb = QStringLiteral("read");
	cmd.description = QStringLiteral("Read a .dawproject container's model WITHOUT touching the "
		"session: the format version the file declares, the application that wrote it, every "
		"track with its clips and notes, the tempo and time-signature timelines, each track's "
		"mixer strip, the entries the container carries that this module does not use, and the "
		"model's printable digest. This is what makes a round trip checkable against the FILE and "
		"the MODEL rather than a hash. A file that is not a ZIP, has no project.xml entry, "
		"declares another major format version, or carries a value the engine will not accept is a "
		"typed refusal.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}},
		{QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("format_version"), stringProperty()},
		{QStringLiteral("application_name"), stringProperty()},
		{QStringLiteral("application_version"), stringProperty()},
		{QStringLiteral("has_metadata"), booleanProperty()},
		{QStringLiteral("title"), stringProperty()},
		{QStringLiteral("track_count"), integerProperty()},
		{QStringLiteral("clip_count"), integerProperty()},
		{QStringLiteral("note_count"), integerProperty()},
		{QStringLiteral("tempo_point_count"), integerProperty()},
		{QStringLiteral("meter_point_count"), integerProperty()},
		{QStringLiteral("split_map_events"), integerProperty()},
		{QStringLiteral("model_digest"), stringProperty()},
		{QStringLiteral("unused_entries"), arrayProperty()},
		{QStringLiteral("loss"), objectProperty()},
		{QStringLiteral("model"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo::exists(path)) { return missingFile(path); }
		dwp::DawProjectModel model;
		dwp::DawProjectReadReport report;
		QString error;
		if (!dwp::readDawProject(path, &model, &report, &error))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
		}

		QJsonObject result;
		insertModelSummary(&result, model);
		result.insert(QStringLiteral("path"), path);
		result.insert(QStringLiteral("has_metadata"), report.hasMetaData);
		result.insert(QStringLiteral("title"), report.title);
		QJsonArray unused;
		for (const QString& entry : report.unusedEntries) { unused.append(entry); }
		result.insert(QStringLiteral("unused_entries"), unused);
		result.insert(QStringLiteral("loss"), report.loss.toJson());
		result.insert(QStringLiteral("model"), dwp::dawProjectModelJson(model));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerImportCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("dawproject.import");
	cmd.group = QStringLiteral("dawproject");
	cmd.verb = QStringLiteral("import");
	cmd.description = QStringLiteral("Apply a .dawproject container's model to the session: "
		"REPLACE the tracks, write the global tempo and time signature, replace the tempo map with "
		"the file's two automation timelines and set each created track's mixer strip. One "
		"command, one undo: the whole session captured before the import - every track's own XML, "
		"the mixer strips, the tempo map and the two globals - is put back through the project "
		"loader's own paths when the stack unwinds. `loss` in the reply reports what the file "
		"carried that this build does not apply (device state, sends, audio clips, folder nesting, "
		"a routing graph). Refused, typed, when the file cannot be read, when a tempo or metre is "
		"outside the engine's own bounds, or when the file needs more tempo-map events than the "
		"map holds - a refusal writes nothing.");
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}},
		{QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("format_version"), stringProperty()},
		{QStringLiteral("track_count"), integerProperty()},
		{QStringLiteral("clip_count"), integerProperty()},
		{QStringLiteral("note_count"), integerProperty()},
		{QStringLiteral("tempo"), numberProperty()},
		{QStringLiteral("numerator"), integerProperty()},
		{QStringLiteral("denominator"), integerProperty()},
		{QStringLiteral("tempo_point_count"), integerProperty()},
		{QStringLiteral("meter_point_count"), integerProperty()},
		{QStringLiteral("map_active"), booleanProperty()},
		{QStringLiteral("model_digest"), stringProperty()},
		{QStringLiteral("loss"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo::exists(path)) { return missingFile(path); }
		dwp::DawProjectModel model;
		dwp::DawProjectReadReport report;
		QString error;
		if (!dwp::readDawProject(path, &model, &report, &error))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, error);
		}

		Song* song = Engine::getSong();
		// The before-state, captured BEFORE anything is replaced. A session too
		// large to capture is refused rather than imported unreversibly - a
		// caller that undid a step which was never recorded would take back the
		// WRONG edit.
		const SessionSnapshot before = captureSession(song);
		if (!before.ok)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the session could not be captured for undo (a track is larger than "
					"the %1-character structural limit), so the import was not performed")
					.arg(StructuralSnapshotLimit));
		}

		if (!dwp::applyDawProjectModel(song, model, &error))
		{
			return ControlResult::failure(ControlErrorKind::Refused, error);
		}

		const dwp::DawProjectModel after = dwp::dawProjectModelFromSong(song, nullptr);
		const dwp::DawProjectModel wanted = model;
		addStructuralUndoStep(
			[song, before]() { restoreSession(song, before); },
			[song, wanted]() {
				QString ignored;
				dwp::applyDawProjectModel(song, wanted, &ignored);
			},
			before.payloadBytes);

		QJsonObject result;
		result.insert(QStringLiteral("path"), path);
		result.insert(QStringLiteral("format_version"), model.formatVersion);
		result.insert(QStringLiteral("track_count"), after.trackCount());
		result.insert(QStringLiteral("clip_count"), after.clipCount());
		result.insert(QStringLiteral("note_count"), after.noteCount());
		result.insert(QStringLiteral("tempo"), after.tempo);
		result.insert(QStringLiteral("numerator"), after.numerator);
		result.insert(QStringLiteral("denominator"), after.denominator);
		result.insert(QStringLiteral("tempo_point_count"), after.tempoPoints.size());
		result.insert(QStringLiteral("meter_point_count"), after.meterPoints.size());
		result.insert(QStringLiteral("map_active"), song->tempoMap().map().active());
		result.insert(QStringLiteral("model_digest"), dwp::dawProjectModelDigest(after));
		const QJsonObject loss = report.loss.toJson();
		result.insert(QStringLiteral("loss"), loss);
		// The inverse is a whole captured session, so no single command is the
		// honest inverse and the record says so (the interchange.smf_import and
		// transport.tempo_map_clear shape).
		result.insert(QStringLiteral("__transaction"), transactionPayload(
			QJsonObject{{QStringLiteral("path"), path},
				{QStringLiteral("track_count"), static_cast<int>(song->tracks().size())},
				{QStringLiteral("loss"), loss}},
			QString(), QJsonObject(), true, importMechanism()));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerDawProjectCommands(ControlRegistry& registry)
{
	registerConventionCommand(registry);
	registerExportCommand(registry);
	registerReadCommand(registry);
	registerImportCommand(registry);
}

} // namespace lmms
