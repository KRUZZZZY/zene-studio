/*
 * ControlCommandsTransportMap.cpp - the tempo map's half of the transport.*
 *                                    command group (SPEC-zene-studio A11-A16).
 *
 * The transport group already exists (ControlCommandsTransport.cpp); this is the
 * D11 tempo-map surface ADDED to it, not a parallel group, so every id here
 * starts with `transport.` exactly like transport.play / set_tempo / get_state.
 *
 * The engine half is include/TempoMap.h + src/core/TempoMap.cpp and the two
 * accessors on Song. What this file holds to account is the SURFACE the release
 * contract section 3.1 requires of it: registered commands with argument and
 * result schemas, typed refusals, and a reversibility class whose inverse
 * actually works.
 *
 * REVERSIBILITY, and why it is NOT the Song checkpoint. A Song checkpoint
 * captures the object's own serialized state, which for a Song is
 * TrackContainer::saveSettings - the track container, and nothing else. The
 * tempo map is not in there, so `transport.set_tempo`'s mechanism does not reach
 * it. These commands therefore record an ACTION checkpoint through
 * control::addUndoStep, the mechanism transport.seek uses for engine state the
 * journal cannot restore: the captured TempoMap is written back through
 * TempoMapPublisher::edit when the stack unwinds. That is a real inverse on the
 * engine's own undo stack (one command, one Ctrl+Z), and
 * ControlTempoMapCommandsTest::everyMapEditUndoesToItsPreCommandState proves it
 * by reading the map back through transport.tempo_map_get.
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
#include <QString>

#include "ControlEdit.h"
#include "ControlRegistry.h"

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "TimePos.h"
#include "TempoMap.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Why these commands are reversible, in the words the transaction record uses.
QString mapMechanism()
{
	return QStringLiteral("action checkpoint: the tempo map is not a JournallingObject and "
		"is not inside the Song's own checkpoint (which captures "
		"TrackContainer::saveSettings), so the map captured before the edit is written "
		"back through TempoMapPublisher::edit when the undo stack unwinds");
}

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

QJsonObject eventState(const TempoMapEvent& event)
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

QJsonArray eventsState(const TempoMap& map)
{
	QJsonArray events;
	for (const TempoMapEvent& event : map.all()) { events.append(eventState(event)); }
	return events;
}

/*! The map AS THE TIMELINE READS IT: the stored events, what each query answers
 *  at the play head, and the two conversions that follow from it. This is the
 *  state query's payload, and it is what makes the map observable over the
 *  socket (the release contract: not drivable or observable through the socket
 *  means not in the release). */
QJsonObject mapState(Song* song)
{
	const TempoMap& map = song->tempoMap().map();
	const tick_t position = song->getPlayPos().getTicks();
	const TempoMapTimeSignature signature =
		map.timeSignatureAtTick(position, globalTimeSignature(song));

	QJsonObject result;
	result.insert(QStringLiteral("active"), map.active());
	result.insert(QStringLiteral("event_count"), map.size());
	result.insert(QStringLiteral("max_events"), TempoMap::MaxEvents);
	result.insert(QStringLiteral("events"), eventsState(map));
	result.insert(QStringLiteral("global_tempo"), globalTempo(song));
	result.insert(QStringLiteral("position_ticks"), static_cast<qint64>(position));
	// What the map answers AT THE PLAY HEAD - the global value whenever the map
	// is inactive, empty, or has no event at or before the play head.
	result.insert(QStringLiteral("tempo_at_position"), song->tempoAtTick(position));
	result.insert(QStringLiteral("timesig_at_position"),
		QStringLiteral("%1/%2").arg(signature.numerator).arg(signature.denominator));
	// The ticks -> time conversion, read through the map (the timeline half of
	// D11 made observable): exact at every event, continuous across it.
	result.insert(QStringLiteral("seconds_at_position"), song->secondsAtTick(position));
	return result;
}

//! The map as a transaction's before-state.
QJsonObject mapPayload(const TempoMap& map)
{
	QJsonObject payload;
	payload.insert(QStringLiteral("active"), map.active());
	payload.insert(QStringLiteral("event_count"), map.size());
	payload.insert(QStringLiteral("events"), eventsState(map));
	return payload;
}

/*! Apply \a fn to the map and record ONE action checkpoint that puts the map
 *  captured in \a before back. False - with the map untouched - when \a fn
 *  refuses the edit, so a refusal writes nothing (SPEC A16). */
template <typename Fn>
bool editMapWithUndo(Song* song, const TempoMap& before, Fn&& fn)
{
	if (!song->tempoMap().edit(fn)) { return false; }
	const TempoMap after = song->tempoMap().map();
	control::addUndoStep(
		[song, before]() { song->tempoMap().edit([&before](TempoMap& map) { map = before; return true; }); },
		[song, after]() { song->tempoMap().edit([&after](TempoMap& map) { map = after; return true; }); });
	return true;
}

//! The event \a args describe, or false when they describe neither half.
bool eventFromArgs(const QJsonObject& args, TempoMapEvent* event)
{
	event->tick = static_cast<tick_t>(args.value(QStringLiteral("tick")).toDouble());
	if (args.contains(QStringLiteral("bpm")))
	{
		event->hasTempo = true;
		event->tempo = static_cast<int>(args.value(QStringLiteral("bpm")).toDouble());
	}
	if (args.contains(QStringLiteral("numerator")) || args.contains(QStringLiteral("denominator")))
	{
		event->hasTimeSignature = true;
		event->numerator = args.value(QStringLiteral("numerator")).toInt();
		event->denominator = args.value(QStringLiteral("denominator")).toInt();
	}
	return event->hasTempo || event->hasTimeSignature;
}

//! The typed refusal for an event the ENGINE will not accept.
ControlResult refusalForEvent(const TempoMapEvent& event)
{
	if (!event.hasTempo && !event.hasTimeSignature)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("give at least one of `bpm` (the tempo) or `numerator` plus "
				"`denominator` (the time signature)"));
	}
	if (event.hasTimeSignature
		&& (event.numerator < TempoMapMinNumerator || event.numerator > TempoMapMaxNumerator
			|| !isSupportedTempoMapDenominator(event.denominator)))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a time signature is %1..%2 over a power of two up to %3 "
				"(1, 2, 4, 8, 16, 32)").arg(TempoMapMinNumerator)
				.arg(TempoMapMaxNumerator).arg(TempoMapLargestDenominator));
	}
	if (event.tick < 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a tempo map event's tick cannot be negative"));
	}
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("the engine refused the event: tempo %1 is outside %2..%3")
			.arg(event.tempo).arg(TempoMapMinTempo).arg(TempoMapMaxTempo));
}

/*! A mutating result: the map's new summary, plus the A16 transaction. \a before
 *  is the map as it was before the edit, and \a inverseOp / \a inverseArgs name
 *  the command a reader could re-issue by hand for the single-event cases; both
 *  empty where no single command is the honest inverse (the journal's action
 *  checkpoint is what restores those, and the record says so). */
ControlResult mutationResult(Song* song, const TempoMap& before, QJsonObject result,
	const QString& inverseOp, const QJsonObject& inverseArgs)
{
	const TempoMap& map = song->tempoMap().map();
	result.insert(QStringLiteral("event_count"), map.size());
	result.insert(QStringLiteral("active"), map.active());
	result.insert(QStringLiteral("__transaction"), control::transactionPayload(
		mapPayload(before), inverseOp, inverseArgs, true, mapMechanism()));
	return ControlResult::success(result);
}

//! The refusal a full or refusing map gives.
ControlResult mapRefusal()
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("the tempo map is full (%1 events) or refused the event")
			.arg(TempoMap::MaxEvents));
}

void registerGetCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.tempo_map_get");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("tempo_map_get");
	cmd.description = QStringLiteral("The tempo map: every tempo and time-signature event, "
		"whether the map is in force, and what its queries answer AT THE PLAY HEAD - the "
		"tempo, the time signature and the elapsed seconds, read through the map's own "
		"ticks-to-time conversion. An empty or inactive map reports the global tempo at "
		"every position, and before the first event the global tempo is what is in force "
		"(docs/TEMPO-MAP.md).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("active"), booleanProperty()},
		{QStringLiteral("event_count"), integerProperty(0, TempoMap::MaxEvents)},
		{QStringLiteral("max_events"), integerProperty()},
		{QStringLiteral("events"), arrayProperty()},
		{QStringLiteral("global_tempo"), integerProperty()},
		{QStringLiteral("position_ticks"), integerProperty()},
		{QStringLiteral("tempo_at_position"), integerProperty()},
		{QStringLiteral("timesig_at_position"), stringProperty()},
		{QStringLiteral("seconds_at_position"), numberProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		return ControlResult::success(mapState(Engine::getSong()));
	};
	registry.registerCommand(cmd);
}

void registerAddCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.tempo_map_add");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("tempo_map_add");
	cmd.description = QStringLiteral("Add a tempo and/or time-signature event to the map, or "
		"replace the half it names at that tick (the other half is kept). Adding an event "
		"brings the map into force unless `active` is passed false: an event no timeline read "
		"would obey is a trap. Refused, typed, when the event fails the engine's own bounds "
		"or carries neither half, and a refusal writes nothing.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("tick"), tickProperty()},
		{QStringLiteral("bpm"), integerProperty(MinTempo, MaxTempo)},
		{QStringLiteral("numerator"), integerProperty(TempoMapMinNumerator, TempoMapMaxNumerator)},
		{QStringLiteral("denominator"), integerProperty(1, TempoMapLargestDenominator)},
		{QStringLiteral("active"), booleanProperty()},
	}, {QStringLiteral("tick")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("tick"), integerProperty()},
		{QStringLiteral("bpm"), integerProperty()},
		{QStringLiteral("numerator"), integerProperty()},
		{QStringLiteral("denominator"), integerProperty()},
		{QStringLiteral("has_tempo"), booleanProperty()},
		{QStringLiteral("has_time_signature"), booleanProperty()},
		{QStringLiteral("replaced"), booleanProperty()},
		{QStringLiteral("event_count"), integerProperty()},
		{QStringLiteral("active"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		TempoMapEvent event;
		if (!eventFromArgs(args, &event)) { return refusalForEvent(event); }
		if (!TempoMap::validEvent(event)) { return refusalForEvent(event); }

		const TempoMap before = song->tempoMap().map();
		const bool replaced = before.hasEventAt(event.tick);
		const bool activate = !args.contains(QStringLiteral("active"))
			|| args.value(QStringLiteral("active")).toBool();
		// The candidate is built OFF the map and assigned only when the engine
		// accepts it, so a refusal writes neither the event nor the active flag.
		if (!editMapWithUndo(song, before, [&event, activate](TempoMap& map) {
				TempoMap candidate = map;
				candidate.setActive(activate);
				if (!candidate.addEvent(event)) { return false; }
				map = candidate;
				return true;
			}))
		{
			return mapRefusal();
		}

		QJsonObject result;
		result.insert(QStringLiteral("tick"), static_cast<qint64>(event.tick));
		result.insert(QStringLiteral("bpm"), event.hasTempo ? event.tempo : 0);
		result.insert(QStringLiteral("numerator"), event.hasTimeSignature ? event.numerator : 0);
		result.insert(QStringLiteral("denominator"), event.hasTimeSignature ? event.denominator : 0);
		result.insert(QStringLiteral("has_tempo"), event.hasTempo);
		result.insert(QStringLiteral("has_time_signature"), event.hasTimeSignature);
		result.insert(QStringLiteral("replaced"), replaced);
		return mutationResult(song, before, result, QStringLiteral("transport.tempo_map_remove"),
			QJsonObject{{QStringLiteral("tick"), static_cast<qint64>(event.tick)}});
	};
	registry.registerCommand(cmd);
}

void registerRemoveCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.tempo_map_remove");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("tempo_map_remove");
	cmd.description = QStringLiteral("Remove the tempo map event at an exact tick. A tick "
		"carrying no event is typed not_found. Removing the LAST event leaves an empty map, "
		"which is the pre-tempo-map engine: every tick answers the global tempo again.");
	cmd.argsSchema = objectSchema({{QStringLiteral("tick"), tickProperty()}},
		{QStringLiteral("tick")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("tick"), integerProperty()},
		{QStringLiteral("bpm"), integerProperty()},
		{QStringLiteral("event_count"), integerProperty()},
		{QStringLiteral("active"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		const tick_t tick = static_cast<tick_t>(args.value(QStringLiteral("tick")).toDouble());
		const TempoMap before = song->tempoMap().map();
		if (!before.hasEventAt(tick))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no tempo map event at tick %1").arg(static_cast<qint64>(tick)));
		}
		// The removed event, so the record carries a re-issuable inverse.
		TempoMapEvent removed;
		for (const TempoMapEvent& candidate : before.all())
		{
			if (candidate.tick == tick) { removed = candidate; }
		}
		if (!editMapWithUndo(song, before,
				[tick](TempoMap& map) { return map.removeEvent(tick); }))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the engine refused to remove the event at tick %1")
					.arg(static_cast<qint64>(tick)));
		}

		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("tick"), static_cast<qint64>(removed.tick));
		if (removed.hasTempo) { inverseArgs.insert(QStringLiteral("bpm"), removed.tempo); }
		if (removed.hasTimeSignature)
		{
			inverseArgs.insert(QStringLiteral("numerator"), removed.numerator);
			inverseArgs.insert(QStringLiteral("denominator"), removed.denominator);
		}
		QJsonObject result;
		result.insert(QStringLiteral("tick"), static_cast<qint64>(tick));
		result.insert(QStringLiteral("bpm"), removed.hasTempo ? removed.tempo : 0);
		return mutationResult(song, before, result, QStringLiteral("transport.tempo_map_add"),
			inverseArgs);
	};
	registry.registerCommand(cmd);
}

void registerClearCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.tempo_map_clear");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("tempo_map_clear");
	cmd.description = QStringLiteral("Remove every event AND switch the map off, in one step "
		"(one undo). Rejects an already empty, inactive map with invalid_args: an edit that "
		"changes nothing is not an edit. The inverse is the whole captured map, so this "
		"command names no single inverse command - the transaction records the map itself.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("removed"), integerProperty()},
		{QStringLiteral("event_count"), integerProperty()},
		{QStringLiteral("active"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject&) {
		Song* song = Engine::getSong();
		const TempoMap before = song->tempoMap().map();
		if (before.empty() && !before.active())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("the tempo map is already empty and inactive"));
		}
		const int removed = before.size();
		if (!editMapWithUndo(song, before, [](TempoMap& map) {
				map.clear();
				return true;
			}))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the engine refused to clear the tempo map"));
		}
		QJsonObject result;
		result.insert(QStringLiteral("removed"), removed);
		return mutationResult(song, before, result, QString(), QJsonObject());
	};
	registry.registerCommand(cmd);
}

void registerSetActiveCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("transport.tempo_map_set_active");
	cmd.group = QStringLiteral("transport");
	cmd.verb = QStringLiteral("tempo_map_set_active");
	cmd.description = QStringLiteral("Switch the tempo map's authority on or off WITHOUT "
		"editing its events: off, every tick answers the global tempo again and the stored "
		"events are still saved with the project; on, the timeline obeys them. A call that "
		"sets the state it already has is invalid_args.");
	cmd.argsSchema = objectSchema({{QStringLiteral("active"), booleanProperty()}},
		{QStringLiteral("active")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("active"), booleanProperty()},
		{QStringLiteral("event_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		const bool wanted = args.value(QStringLiteral("active")).toBool();
		const TempoMap before = song->tempoMap().map();
		if (wanted == before.active())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("the tempo map is already %1").arg(
					wanted ? QStringLiteral("active") : QStringLiteral("inactive")));
		}
		if (!editMapWithUndo(song, before, [wanted](TempoMap& map) {
				map.setActive(wanted);
				return true;
			}))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the engine refused to change the tempo map's active state"));
		}
		QJsonObject result;
		result.insert(QStringLiteral("active"), wanted);
		return mutationResult(song, before, result, QStringLiteral("transport.tempo_map_set_active"),
			QJsonObject{{QStringLiteral("active"), !wanted}});
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerTransportTempoMapCommands(ControlRegistry& registry)
{
	registerGetCommand(registry);
	registerAddCommand(registry);
	registerRemoveCommand(registry);
	registerClearCommand(registry);
	registerSetActiveCommand(registry);
}

} // namespace lmms
