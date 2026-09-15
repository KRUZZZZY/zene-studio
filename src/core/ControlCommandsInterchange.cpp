/*
 * ControlCommandsInterchange.cpp - the `interchange.*` command group: the
 *                                 Standard MIDI File conductor track.
 *
 * Feature row 33 (tempo-map export / SMF cross-DAW interchange). The engine half
 * is include/SmfInterchange.h + src/core/SmfInterchange.cpp; what this file holds
 * to account is the SURFACE the release contract (section 3.1) requires of it:
 * registered commands with argument and result schemas, typed refusals, and a
 * reversibility class whose inverse actually works.
 *
 * FOUR IDS, one per thing a caller can want to know or do:
 *
 *   interchange.smf_convention  the tick/PPQ and time-signature convention, as
 *                               data on the wire rather than as prose in a doc
 *   interchange.smf_export      write the tempo map as a conductor track
 *   interchange.smf_read        read a file's conductor events back (no session
 *                               change: this is what makes the round trip
 *                               checkable against the FILE and not the hash)
 *   interchange.smf_import      apply a file's conductor events to the map
 *
 * REVERSIBILITY. smf_export and smf_read do not touch the session - they read it
 * or write a file outside it - so they are not_mutating, the same class
 * project.save and render.stems carry. smf_import DOES mutate (it replaces the
 * tempo map), and it is reversible on the engine's own undo stack for the reason
 * the tempo map's own commands are (src/core/ControlCommandsTransportMap.cpp):
 * a Song checkpoint captures TrackContainer::saveSettings and the tempo map is
 * not inside it, so the map captured before the import is written back through
 * TempoMapPublisher::edit when the stack unwinds - an action checkpoint, and one
 * command is one Ctrl+Z.
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

#include "ControlEdit.h"
#include "ControlRegistry.h"

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SmfInterchange.h"
#include "Song.h"
#include "TimePos.h"
#include "TempoMap.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The global (project-wide) tempo, which is the map's out-of-range answer.
int globalTempo(Song* song)
{
	return static_cast<int>(song->getTempo());
}

//! The global time signature, the map's other out-of-range answer.
TempoMapTimeSignature globalTimeSignature(Song* song)
{
	return TempoMapTimeSignature{
		song->getTimeSigModel().numeratorModel().value(),
		song->getTimeSigModel().denominatorModel().value() };
}

QJsonObject conventionState()
{
	const interchange::SmfConvention& convention = interchange::smfConvention();
	QJsonObject result;
	result.insert(QStringLiteral("ticks_per_quarter"), convention.ticksPerQuarterNote);
	result.insert(QStringLiteral("lmms_ticks_per_quarter"), convention.lmmsTicksPerQuarterNote);
	result.insert(QStringLiteral("smf_ticks_per_lmms_tick"), convention.smfTicksPerLmmsTick);
	result.insert(QStringLiteral("tempo_unit"), convention.tempoUnit);
	result.insert(QStringLiteral("time_signature_encoding"), convention.timeSignatureEncoding);
	result.insert(QStringLiteral("track_shape"), convention.trackShape);
	result.insert(QStringLiteral("tick_zero_rule"), convention.tickZeroRule);
	result.insert(QStringLiteral("stated_limits"), convention.statedLimits);
	return result;
}

QJsonObject eventState(const interchange::SmfInterchangeEvent& event)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("tick"), static_cast<qint64>(event.tick));
	entry.insert(QStringLiteral("has_tempo"), event.hasTempo);
	entry.insert(QStringLiteral("bpm"), event.hasTempo ? event.tempo : 0);
	entry.insert(QStringLiteral("has_time_signature"), event.hasTimeSignature);
	entry.insert(QStringLiteral("numerator"), event.hasTimeSignature ? event.numerator : 0);
	entry.insert(QStringLiteral("denominator"), event.hasTimeSignature ? event.denominator : 0);
	return entry;
}

QJsonArray eventsState(const std::vector<interchange::SmfInterchangeEvent>& events)
{
	QJsonArray array;
	for (const interchange::SmfInterchangeEvent& event : events) { array.append(eventState(event)); }
	return array;
}

QJsonArray mapEvents(const TempoMap& map)
{
	QJsonArray array;
	for (const TempoMapEvent& event : map.all())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("tick"), static_cast<qint64>(event.tick));
		entry.insert(QStringLiteral("has_tempo"), event.hasTempo);
		entry.insert(QStringLiteral("bpm"), event.hasTempo ? event.tempo : 0);
		entry.insert(QStringLiteral("has_time_signature"), event.hasTimeSignature);
		entry.insert(QStringLiteral("numerator"), event.hasTimeSignature ? event.numerator : 0);
		entry.insert(QStringLiteral("denominator"), event.hasTimeSignature ? event.denominator : 0);
		array.append(entry);
	}
	return array;
}

//! The map as a transaction's before-state (the shape the tempo map's own
//! commands record).
QJsonObject mapPayload(const TempoMap& map)
{
	QJsonObject payload;
	payload.insert(QStringLiteral("active"), map.active());
	payload.insert(QStringLiteral("event_count"), map.size());
	payload.insert(QStringLiteral("events"), mapEvents(map));
	return payload;
}

//! Why smf_import is reversible, in the words the transaction record uses.
QString importMechanism()
{
	return QStringLiteral("action checkpoint: the tempo map is not a JournallingObject and is "
		"not inside the Song's own checkpoint (which captures TrackContainer::saveSettings), so "
		"the map captured before the import is written back through TempoMapPublisher::edit "
		"when the undo stack unwinds");
}

//! The typed refusal every id gives a bad path argument with.
ControlResult missingFile(const QString& path)
{
	return ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no such file: %1").arg(path));
}

void registerConventionCommand(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("interchange.smf_convention");
		cmd.group = QStringLiteral("interchange");
		cmd.verb = QStringLiteral("smf_convention");
		cmd.description = QStringLiteral("The tick/PPQ and time-signature convention this "
			"product's Standard MIDI File interchange uses, as data: the division it writes "
			"(ticks per quarter note), LMMS' own ticks per quarter note and the exact ratio "
			"between them, the unit the tempo meta event is expressed in, the byte layout of "
			"the time-signature meta event, the file shape (format and track layout), the "
			"tick-0 rule and the stated limits (steps only, conductor track only).");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("ticks_per_quarter"), integerProperty()},
			{QStringLiteral("lmms_ticks_per_quarter"), integerProperty()},
			{QStringLiteral("smf_ticks_per_lmms_tick"), integerProperty()},
			{QStringLiteral("tempo_unit"), stringProperty()},
			{QStringLiteral("time_signature_encoding"), stringProperty()},
			{QStringLiteral("track_shape"), stringProperty()},
			{QStringLiteral("tick_zero_rule"), stringProperty()},
			{QStringLiteral("stated_limits"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return ControlResult::success(conventionState()); };
		registry.registerCommand(cmd);
	}
}

void registerExportCommand(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("interchange.smf_export");
		cmd.group = QStringLiteral("interchange");
		cmd.verb = QStringLiteral("smf_export");
		cmd.description = QStringLiteral("Write the tempo map as a Standard MIDI File another "
			"DAW can read: format 1, one conductor track carrying the tempo and time-signature "
			"events, at division 480 ticks per quarter note (LMMS' own 48 ticks per quarter "
			"mapped in exactly). The file always names the step in force at tick 0, so a map "
			"whose first event is later still exports the session it is part of; `seed_events` "
			"reports how many halves that rule had to add. Notes, clips and automation are NOT "
			"in this file - it is a conductor track (the note export is File > Export MIDI, a "
			"different, pre-existing path). Refuses an existing file unless `overwrite` is "
			"true, and refuses a relative path. Writes a file; changes nothing in the session.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("overwrite"), booleanProperty()},
		}, {QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("bytes"), integerProperty()},
			{QStringLiteral("sha256"), stringProperty()},
			{QStringLiteral("format"), integerProperty()},
			{QStringLiteral("track_count"), integerProperty()},
			{QStringLiteral("ticks_per_quarter"), integerProperty()},
			{QStringLiteral("lmms_ticks_per_quarter"), integerProperty()},
			{QStringLiteral("smf_ticks_per_lmms_tick"), integerProperty()},
			{QStringLiteral("event_count"), integerProperty()},
			{QStringLiteral("tempo_events"), integerProperty()},
			{QStringLiteral("meter_events"), integerProperty()},
			{QStringLiteral("seed_events"), integerProperty()},
			{QStringLiteral("first_tick"), integerProperty()},
			{QStringLiteral("last_tick"), integerProperty()},
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("global_tempo"), integerProperty()},
			{QStringLiteral("global_timesig"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) {
			Song* song = Engine::getSong();
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

			const TempoMapTimeSignature signature = globalTimeSignature(song);
			interchange::SmfWriteReport report;
			QString error;
			if (!interchange::writeConductorTrack(path, song->tempoMap().map(),
					globalTempo(song), signature, &report, &error))
			{
				return ControlResult::failure(ControlErrorKind::Refused, error);
			}

			QJsonObject result;
			result.insert(QStringLiteral("path"), report.path);
			result.insert(QStringLiteral("bytes"), static_cast<qint64>(report.bytes));
			result.insert(QStringLiteral("sha256"), report.sha256);
			result.insert(QStringLiteral("format"), report.format);
			result.insert(QStringLiteral("track_count"), report.trackCount);
			result.insert(QStringLiteral("ticks_per_quarter"), report.ticksPerQuarterNote);
			result.insert(QStringLiteral("lmms_ticks_per_quarter"),
				interchange::LmmsTicksPerQuarterNote);
			result.insert(QStringLiteral("smf_ticks_per_lmms_tick"),
				interchange::SmfTicksPerLmmsTick);
			result.insert(QStringLiteral("event_count"), report.eventCount);
			result.insert(QStringLiteral("tempo_events"), report.tempoEvents);
			result.insert(QStringLiteral("meter_events"), report.meterEvents);
			result.insert(QStringLiteral("seed_events"), report.seedEvents);
			result.insert(QStringLiteral("first_tick"), report.firstTick);
			result.insert(QStringLiteral("last_tick"), report.lastTick);
			result.insert(QStringLiteral("active"), song->tempoMap().map().active());
			result.insert(QStringLiteral("global_tempo"), globalTempo(song));
			result.insert(QStringLiteral("global_timesig"),
				QStringLiteral("%1/%2").arg(signature.numerator).arg(signature.denominator));
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}
}

void registerReadCommand(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("interchange.smf_read");
		cmd.group = QStringLiteral("interchange");
		cmd.verb = QStringLiteral("smf_read");
		cmd.description = QStringLiteral("Read a Standard MIDI File's conductor events back and "
			"report them in LMMS' own tick domain, WITHOUT touching the session: every track's "
			"tempo and time-signature meta events, merged by tick (the first the file names at "
			"a tick wins, per half), with the file's own division, how many events had to be "
			"rounded onto LMMS' 48-ticks-per-quarter grid, and whether the map could hold them "
			"all. This is what makes a round trip checkable against the file rather than "
			"against its hash: read the file, compare the map. A file that is not a Standard "
			"MIDI File, or whose division is an SMPTE rate, is a typed invalid_args refusal.");
		cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}},
			{QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("format"), integerProperty()},
			{QStringLiteral("track_count"), integerProperty()},
			{QStringLiteral("ticks_per_quarter"), integerProperty()},
			{QStringLiteral("lmms_ticks_per_quarter"), integerProperty()},
			{QStringLiteral("smf_ticks_per_lmms_tick"), integerProperty()},
			{QStringLiteral("event_count"), integerProperty()},
			{QStringLiteral("tempo_events"), integerProperty()},
			{QStringLiteral("meter_events"), integerProperty()},
			{QStringLiteral("rounded_events"), integerProperty()},
			{QStringLiteral("superseded_events"), integerProperty()},
			{QStringLiteral("capacity_events"), integerProperty()},
			{QStringLiteral("importable"), booleanProperty()},
			{QStringLiteral("first_tick"), integerProperty()},
			{QStringLiteral("last_tick"), integerProperty()},
			{QStringLiteral("events"), arrayProperty()},
			{QStringLiteral("tempo_unit"), stringProperty()},
			{QStringLiteral("time_signature_encoding"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) {
			const QString path = args.value(QStringLiteral("path")).toString();
			if (!QFileInfo::exists(path)) { return missingFile(path); }
			const interchange::SmfReadReport report = interchange::readConductorTrack(path);
			if (!report.ok)
			{
				return ControlResult::failure(ControlErrorKind::InvalidArgs, report.error);
			}

			const interchange::SmfConvention& convention = interchange::smfConvention();
			QJsonObject result;
			result.insert(QStringLiteral("path"), path);
			result.insert(QStringLiteral("format"), report.format);
			result.insert(QStringLiteral("track_count"), report.trackCount);
			result.insert(QStringLiteral("ticks_per_quarter"), report.ticksPerQuarterNote);
			result.insert(QStringLiteral("lmms_ticks_per_quarter"), convention.lmmsTicksPerQuarterNote);
			result.insert(QStringLiteral("smf_ticks_per_lmms_tick"), convention.smfTicksPerLmmsTick);
			result.insert(QStringLiteral("event_count"), static_cast<qint64>(report.events.size()));
			result.insert(QStringLiteral("tempo_events"), report.tempoEvents);
			result.insert(QStringLiteral("meter_events"), report.meterEvents);
			result.insert(QStringLiteral("rounded_events"), report.roundedEvents);
			result.insert(QStringLiteral("superseded_events"), report.supersededEvents);
			result.insert(QStringLiteral("capacity_events"), report.capacityEvents);
			result.insert(QStringLiteral("importable"), report.capacityEvents == 0);
			result.insert(QStringLiteral("first_tick"),
				report.events.empty() ? 0 : static_cast<qint64>(report.events.front().tick));
			result.insert(QStringLiteral("last_tick"),
				report.events.empty() ? 0 : static_cast<qint64>(report.events.back().tick));
			result.insert(QStringLiteral("events"), eventsState(report.events));
			result.insert(QStringLiteral("tempo_unit"), convention.tempoUnit);
			result.insert(QStringLiteral("time_signature_encoding"), convention.timeSignatureEncoding);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}
}

void registerImportCommand(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("interchange.smf_import");
		cmd.group = QStringLiteral("interchange");
		cmd.verb = QStringLiteral("smf_import");
		cmd.description = QStringLiteral("Replace the tempo map with the conductor events of a "
			"Standard MIDI File: every track's tempo and time-signature meta events, merged by "
			"tick, in LMMS' own tick domain at the file's own division. The imported map is "
			"ACTIVE - a conductor track IS a tempo map, so a map switched off would import "
			"tempo changes the timeline does not obey. One command, one undo: the map captured "
			"before the import is written back through TempoMapPublisher::edit when the stack "
			"unwinds. Refused, typed, when the file is not readable as a Standard MIDI File, "
			"when an event is outside the engine's own bounds (tempo 10..999, a denominator "
			"that is a power of two up to 32), or when the file needs more than the map's 128 "
			"events - a refusal writes nothing.");
		cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}},
			{QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("ticks_per_quarter"), integerProperty()},
			{QStringLiteral("event_count"), integerProperty()},
			{QStringLiteral("tempo_events"), integerProperty()},
			{QStringLiteral("meter_events"), integerProperty()},
			{QStringLiteral("rounded_events"), integerProperty()},
			{QStringLiteral("active"), booleanProperty()},
			{QStringLiteral("first_tick"), integerProperty()},
			{QStringLiteral("last_tick"), integerProperty()},
			{QStringLiteral("events"), arrayProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) {
			Song* song = Engine::getSong();
			const QString path = args.value(QStringLiteral("path")).toString();
			if (!QFileInfo::exists(path)) { return missingFile(path); }
			const interchange::SmfReadReport report = interchange::readConductorTrack(path);
			if (!report.ok)
			{
				return ControlResult::failure(ControlErrorKind::InvalidArgs, report.error);
			}

			TempoMap imported;
			QString error;
			if (!interchange::mapFromEvents(report.events, &imported, &error))
			{
				return ControlResult::failure(ControlErrorKind::Refused, error);
			}

			const TempoMap before = song->tempoMap().map();
			if (!song->tempoMap().edit([&imported](TempoMap& map) {
					map = imported;
					return true;
				}))
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("the engine refused to apply the file's tempo map"));
			}
			const TempoMap after = song->tempoMap().map();
			control::addUndoStep(
				[song, before]() { song->tempoMap().edit([&before](TempoMap& map) { map = before; return true; }); },
				[song, after]() { song->tempoMap().edit([&after](TempoMap& map) { map = after; return true; }); });

			QJsonObject result;
			result.insert(QStringLiteral("path"), path);
			result.insert(QStringLiteral("ticks_per_quarter"), report.ticksPerQuarterNote);
			result.insert(QStringLiteral("event_count"), after.size());
			result.insert(QStringLiteral("tempo_events"), report.tempoEvents);
			result.insert(QStringLiteral("meter_events"), report.meterEvents);
			result.insert(QStringLiteral("rounded_events"), report.roundedEvents);
			result.insert(QStringLiteral("active"), after.active());
			result.insert(QStringLiteral("first_tick"),
				after.size() > 0 ? static_cast<qint64>(after[0].tick) : 0);
			result.insert(QStringLiteral("last_tick"),
				after.size() > 0 ? static_cast<qint64>(after[after.size() - 1].tick) : 0);
			result.insert(QStringLiteral("events"), mapEvents(after));
			// The inverse is the whole captured map, so no single command is the
			// honest inverse and the record says so (the transport.tempo_map_clear
			// shape).
			result.insert(QStringLiteral("__transaction"), control::transactionPayload(
				mapPayload(before), QString(), QJsonObject(), true, importMechanism()));
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}
}

} // namespace

void registerInterchangeCommands(ControlRegistry& registry)
{
	registerConventionCommand(registry);
	registerExportCommand(registry);
	registerReadCommand(registry);
	registerImportCommand(registry);
}

} // namespace lmms
