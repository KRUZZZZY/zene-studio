/*
 * ControlTempoMapCommandsTest.cpp - the tempo map's control surface: the five
 *                                    transport.* commands, their schemas, their
 *                                    typed refusals and their inverse.
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

// The release contract section 3.1, for the D11 tempo map: the ENGINE half is
// include/TempoMap.h and TempoMapTest proves its arithmetic; what this file
// holds to account is the SURFACE - five registered commands with argument and
// result schemas, typed refusals that write nothing, and an A16 class whose
// inverse actually works. The load-bearing case is
// `everyMapEditUndoesToItsPreCommandState`: a tempo map is not a
// JournallingObject and no Song checkpoint carries it, so the inverse is a
// recorded ACTION checkpoint (docs/TEMPO-MAP.md section 5), and that is read
// back through transport.tempo_map_get after control.undo.

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "TempoMap.h"

using namespace lmms;

namespace
{

//! Invoke a command through the registry, exactly as the socket does.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject mapState()
{
	return run(QStringLiteral("transport.tempo_map_get")).result;
}

int eventCount()
{
	return mapState().value(QStringLiteral("event_count")).toInt();
}

bool mapActive()
{
	return mapState().value(QStringLiteral("active")).toBool();
}

int tempoAtPosition()
{
	return mapState().value(QStringLiteral("tempo_at_position")).toInt();
}

//! The stored event the map reports at \a tick, or an empty object when there
//! is none - the entries transport.tempo_map_get publishes.
QJsonObject eventAt(int tick)
{
	const QJsonArray events = mapState().value(QStringLiteral("events")).toArray();
	for (const QJsonValue& value : events)
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("tick")).toInt() == tick) { return entry; }
	}
	return QJsonObject();
}

//! Re-issue the inverse the LAST recorded command advertised, by hand, exactly
//! as a client reading the record would: its op and its args.
ControlResult reissueAdvertisedInverse()
{
	const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
	if (tx == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no transaction was recorded"));
	}
	const QString op = tx->inverse.value(QStringLiteral("op")).toString();
	if (op.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Irreversible,
			QStringLiteral("the record advertised NO inverse command"));
	}
	return run(op, tx->inverse.value(QStringLiteral("args")).toObject());
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

} // namespace


class ControlTempoMapCommandsTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every test starts from a project the map has never touched.
	void init()
	{
		Engine::getSong()->clearProject();
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}

	//! Every command of the group declares the contract's parts: a transport.
	//! group.verb id, both schemas, a description and an empty `requires`
	//! (headless parity, SPEC A13), and the mutating flag the table class
	//! expects.
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("transport.tempo_map_add"),
			QStringLiteral("transport.tempo_map_remove"),
			QStringLiteral("transport.tempo_map_clear"),
			QStringLiteral("transport.tempo_map_set_active")};
		const QStringList all = QStringList{ QStringLiteral("transport.tempo_map_get") } + mutating;
		for (const QString& id : all)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			// The map is part of the TRANSPORT group, not a parallel one.
			QCOMPARE(cmd->group, QStringLiteral("transport"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
		// The pre-existing transport commands are untouched by this group.
		QVERIFY(registry->hasCommand(QStringLiteral("transport.set_tempo")));
		QVERIFY(registry->hasCommand(QStringLiteral("transport.get_state")));
	}

	//! A project nobody has mapped: the map is empty, inactive, and every query
	//! answers the global tempo - the state the engine was in before D11.
	void aFreshProjectReportsAnEmptyInactiveMap()
	{
		const QJsonObject state = mapState();
		QCOMPARE(state.value(QStringLiteral("active")).toBool(), false);
		QCOMPARE(state.value(QStringLiteral("event_count")).toInt(), 0);
		QCOMPARE(state.value(QStringLiteral("max_events")).toInt(), TempoMap::MaxEvents);
		QCOMPARE(state.value(QStringLiteral("events")).toArray().size(), 0);
		const int global = state.value(QStringLiteral("global_tempo")).toInt();
		QCOMPARE(global, static_cast<int>(Engine::getSong()->getTempo()));
		QCOMPARE(state.value(QStringLiteral("tempo_at_position")).toInt(), global);
		QCOMPARE(state.value(QStringLiteral("timesig_at_position")).toString(),
			QStringLiteral("4/4"));
		QCOMPARE(state.value(QStringLiteral("position_ticks")).toInt(), 0);
		// Zero ticks is zero seconds, whatever the tempo.
		QCOMPARE(state.value(QStringLiteral("seconds_at_position")).toDouble(), 0.0);
	}

	//! add -> the event in the map, add-or-replace merging PER PROPERTY, the map
	//! coming into force, remove -> gone, clear -> empty and off.
	void addRemoveClearEditTheMap()
	{
		const int global = static_cast<int>(Engine::getSong()->getTempo());

		// Out of order on purpose: the engine holds the set sorted by tick.
		const ControlResult late = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 768}, {QStringLiteral("bpm"), 180}});
		QVERIFY2(late.ok, qPrintable(late.errorMessage));
		QCOMPARE(late.result.value(QStringLiteral("replaced")).toBool(), false);
		const ControlResult first = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));

		QCOMPARE(eventCount(), 2);
		QVERIFY(mapActive());  // an event nothing would obey is a trap
		QCOMPARE(tempoAtPosition(), 120);
		QJsonArray events = mapState().value(QStringLiteral("events")).toArray();
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("tick")).toInt(), 0);
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("bpm")).toInt(), 120);
		QCOMPARE(events.at(1).toObject().value(QStringLiteral("tick")).toInt(), 768);
		QCOMPARE(events.at(1).toObject().value(QStringLiteral("bpm")).toInt(), 180);

		// A metre at the tick that already carries a tempo: ONE event, both
		// halves - and a tempo-only call at the same tick keeps the metre.
		const ControlResult metre = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("numerator"), 3},
				{QStringLiteral("denominator"), 4}});
		QVERIFY2(metre.ok, qPrintable(metre.errorMessage));
		QCOMPARE(metre.result.value(QStringLiteral("replaced")).toBool(), true);
		QCOMPARE(eventCount(), 2);
		QCOMPARE(mapState().value(QStringLiteral("timesig_at_position")).toString(),
			QStringLiteral("3/4"));
		const ControlResult retempo = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 96}});
		QVERIFY2(retempo.ok, qPrintable(retempo.errorMessage));
		events = mapState().value(QStringLiteral("events")).toArray();
		QCOMPARE(events.size(), 2);
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("bpm")).toInt(), 96);
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("numerator")).toInt(), 3);
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("denominator")).toInt(), 4);
		QCOMPARE(tempoAtPosition(), 96);

		// Below the first event the global tempo still rules.
		const ControlResult after = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 1536}, {QStringLiteral("bpm"), 75}});
		QVERIFY2(after.ok, qPrintable(after.errorMessage));
		// The play head sits at tick 0, so the event at 1536 cannot move it.
		QCOMPARE(tempoAtPosition(), 96);
		QCOMPARE(Engine::getSong()->tempoAtTick(1536), 75);
		// ...and the tick BEFORE the new event still carries the event that
		// governs it (the one at 768), not the one just added.
		QCOMPARE(Engine::getSong()->tempoAtTick(1535), 180);
		QVERIFY(global != 0);

		const ControlResult removed = run(QStringLiteral("transport.tempo_map_remove"),
			{{QStringLiteral("tick"), 1536}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(eventCount(), 2);

		const ControlResult cleared = run(QStringLiteral("transport.tempo_map_clear"));
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(cleared.result.value(QStringLiteral("removed")).toInt(), 2);
		QCOMPARE(eventCount(), 0);
		QCOMPARE(mapActive(), false);
		// The same state the fresh project reported: the pre-D11 engine.
		QCOMPARE(tempoAtPosition(), global);
	}

	//! The add reply describes the merged event, including its untouched half.
	void addReplyReportsTheMergedEvent()
	{
		const ControlResult metre = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 384}, {QStringLiteral("numerator"), 7},
				{QStringLiteral("denominator"), 8}});
		QVERIFY2(metre.ok, qPrintable(metre.errorMessage));

		const ControlResult tempo = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 384}, {QStringLiteral("bpm"), 137}});
		QVERIFY2(tempo.ok, qPrintable(tempo.errorMessage));

		const QJsonArray events = mapState().value(QStringLiteral("events")).toArray();
		QCOMPARE(events.size(), 1);
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("numerator")).toInt(), 7);
		QCOMPARE(events.at(0).toObject().value(QStringLiteral("denominator")).toInt(), 8);

		QCOMPARE(tempo.result.value(QStringLiteral("bpm")).toInt(), 137);
		QCOMPARE(tempo.result.value(QStringLiteral("has_tempo")).toBool(), true);
		QCOMPARE(tempo.result.value(QStringLiteral("has_time_signature")).toBool(), true);
		QCOMPARE(tempo.result.value(QStringLiteral("numerator")).toInt(), 7);
		QCOMPARE(tempo.result.value(QStringLiteral("denominator")).toInt(), 8);
	}

	//! set_active switches authority WITHOUT editing events, and the events are
	//! still there (and still saved) while it is off.
	void setActiveSwitchesWithoutEditingEvents()
	{
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 384}, {QStringLiteral("numerator"), 7},
				{QStringLiteral("denominator"), 8}}).ok);
		QCOMPARE(eventCount(), 2);
		QCOMPARE(tempoAtPosition(), 120);

		const ControlResult off = run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), false}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QCOMPARE(mapActive(), false);
		QCOMPARE(eventCount(), 2);           // the events survive the switch
		const int global = static_cast<int>(Engine::getSong()->getTempo());
		QCOMPARE(tempoAtPosition(), global); // and every tick answers the global tempo
		QCOMPARE(Engine::getSong()->tempoAtTick(0), global);

		QVERIFY(run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), true}}).ok);
		QCOMPARE(mapActive(), true);
		QCOMPARE(tempoAtPosition(), 120);
		QCOMPARE(Engine::getSong()->tempoAtTick(0), 120);
	}

	//! Every refusal is a typed error with a message, and it changes nothing.
	void refusalsAreTypedAndChangeNothing()
	{
		// Neither half.
		ControlResult result = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!result.errorMessage.isEmpty());
		QCOMPARE(eventCount(), 0);

		// A tempo outside the engine's own bounds, a denominator that is not a
		// power of two, and a numerator out of range.
		result = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), MaxTempo + 100}});
		QCOMPARE(result.ok, false);
		result = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("numerator"), 4},
				{QStringLiteral("denominator"), 3}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("numerator"), 99},
				{QStringLiteral("denominator"), 4}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(eventCount(), 0);

		// The schema itself catches a missing required argument.
		result = run(QStringLiteral("transport.tempo_map_add"), QJsonObject());
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);

		// remove at a tick with no event, and clear on an empty map.
		result = run(QStringLiteral("transport.tempo_map_remove"), {{QStringLiteral("tick"), 384}});
		QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
		QVERIFY(!result.errorMessage.contains(QStringLiteral("clip")));
		result = run(QStringLiteral("transport.tempo_map_clear"));
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		result = run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), false}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);

		// A refused call records NO transaction, so it cannot shadow the undo
		// of the real edit underneath it.
		QVERIFY(ControlRegistry::instance()->lastTransaction() == nullptr);

		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		const ControlResult again = run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), true}});
		QCOMPARE(again.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(again.errorMessage.contains(QStringLiteral("already")));
		QCOMPARE(eventCount(), 1);
	}

	//! The contract table classifies the group, the registry stamps the class the
	//! handlers claim, and the mechanism names the action checkpoint.
	void contractRowsClassifyTheGroup()
	{
		const QStringList mutating = {
			QStringLiteral("transport.tempo_map_add"),
			QStringLiteral("transport.tempo_map_remove"),
			QStringLiteral("transport.tempo_map_clear"),
			QStringLiteral("transport.tempo_map_set_active")};
		for (const QString& id : mutating)
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
			QVERIFY(row->reversible);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}
		const control::ReversibilityEntry* read = contractRow(QStringLiteral("transport.tempo_map_get"));
		QVERIFY(read != nullptr);
		QCOMPARE(control::reversibilityClassName(read->cls), QStringLiteral("not_mutating"));

		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QCOMPARE(tx->command, QStringLiteral("transport.tempo_map_add"));
		QCOMPARE(tx->cls, QStringLiteral("true_inverse"));
		QCOMPARE(tx->reversible, true);
		QVERIFY2(tx->mechanism.contains(QStringLiteral("action checkpoint")),
			qPrintable(tx->mechanism));
		// This add ACTIVATES the inactive empty map. tempo_map_remove would leave the map active, so it
		// does not restore the prior state and no command is advertised (Luna 4905, review): the
		// checkpoint is the way back. This line used to assert the remove here - the defect the review
		// flagged for an add that changes the active flag.
		QVERIFY2(tx->inverse.value(QStringLiteral("op")).toString().isEmpty(),
			"an add that activated the map advertised a remove that leaves it active");
		// The before-state IS the map, so the record can restore it by hand too.
		QCOMPARE(tx->before.value(QStringLiteral("event_count")).toInt(), 0);
		QCOMPARE(tx->before.value(QStringLiteral("active")).toBool(), false);
	}

	/*! A remove's advertised inverse has to bring back the event EXACTLY as it
	 *  was, or it is not an inverse: the tempo-map report publishes each event's
	 *  `ramp` and the map's `active` flag, so a re-issued add that drops either
	 *  rebuilds a DIFFERENT map than the one the remove took away (Luna cluster
	 *  4906 / 4000).
	 */
	void removeInverseRebuildsRampAndInactiveState()
	{
		// A ramping tempo event on an INACTIVE map - both halves an inverse can
		// silently drop.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 768}, {QStringLiteral("bpm"), 90},
				{QStringLiteral("ramp"), true}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), false}}).ok);
		QCOMPARE(eventAt(768).value(QStringLiteral("ramp")).toBool(), true);
		QCOMPARE(mapActive(), false);

		QVERIFY(run(QStringLiteral("transport.tempo_map_remove"),
			{{QStringLiteral("tick"), 768}}).ok);
		QCOMPARE(eventCount(), 1);
		QCOMPARE(mapActive(), false);

		// Re-issue the inverse the record advertised, by hand.
		const ControlResult back = reissueAdvertisedInverse();
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
		QCOMPARE(eventCount(), 2);
		// THE DEFECT: the add inverse named the tempo but not `ramp`, so the
		// event came back holding instead of ramping.
		QCOMPARE(eventAt(768).value(QStringLiteral("ramp")).toBool(), true);
		// AND the map came back IN FORCE: adding an event activates the map
		// unless `active:false` is passed, and the inverse did not pass it.
		QCOMPARE(mapActive(), false);
	}

	/*! Luna cluster 4905 (review): an add to a map that is INACTIVE activates it. tempo_map_remove keeps
	 *  the flag it finds, so its advertised inverse leaves the map active and does not restore the
	 *  prior state. The record must name the checkpoint instead, and control.undo must put the
	 *  inactive map back.
	 */
	void anAddThatActivatesAnInactiveMapAdvertisesNoRemoveInverse()
	{
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), false}}).ok);
		QCOMPARE(mapActive(), false);

		QVERIFY2(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 768}, {QStringLiteral("bpm"), 90}}).ok, "tempo_map_add refused");
		QCOMPARE(mapActive(), true);  // the add activated the map
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QVERIFY2(tx->inverse.value(QStringLiteral("op")).toString().isEmpty(),
			"an add that activated the map advertised a remove that leaves it active");
		QVERIFY2(tx->mechanism.contains(QStringLiteral("checkpoint")), qPrintable(tx->mechanism));

		// The honest way back: control.undo restores the map as it was - inactive, one event.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(mapActive(), false);
		QCOMPARE(eventCount(), 1);
	}

	/*! An add's advertised inverse must restore the map the add changed. When the
	 *  add REPLACED the half an existing event carried, the inverse
	 *  `tempo_map_remove` DELETES that event instead of putting its old contents
	 *  back - so the record must name the checkpoint as the way back rather than
	 *  advertise a command that cannot restore the prior state (Luna cluster
	 *  4905).
	 */
	void addInverseIsAdvertisedOnlyWhereItRestoresThePriorState()
	{
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		// A plain ADD to a tick carrying nothing: remove at that tick really does
		// restore the prior map, so the inverse stays advertised and works.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 768}, {QStringLiteral("bpm"), 90}}).ok);
		QCOMPARE(eventCount(), 2);
		QVERIFY2(reissueAdvertisedInverse().ok,
			"the inverse of a plain add must be advertised and re-issuable");
		QCOMPARE(eventCount(), 1);

		// A REPLACEMENT at an occupied tick: the contents it overwrote are not
		// recoverable by remove, so the record must not advertise that command.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 150}}).ok);
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QVERIFY2(tx->inverse.value(QStringLiteral("op")).toString().isEmpty(),
			"a replacing add advertised an inverse that DELETES the event it replaced");
		QVERIFY2(tx->mechanism.contains(QStringLiteral("checkpoint")),
			qPrintable(tx->mechanism));
		// The honest way back still works: control.undo restores the replaced half.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tempoAtTick(0), 120);
	}

	/*! THE A16 PROOF. Each case applies an edit, asks control.undo to take it
	 *  back, and reads the map through transport.tempo_map_get afterwards - the
	 *  same behaviour an agent gets over the socket.
	 */
	void everyMapEditUndoesToItsPreCommandState()
	{
		const int global = static_cast<int>(Engine::getSong()->getTempo());

		// (1) The FIRST event: its before-state is an empty map, and the undo
		// must take the map back to empty AND inactive.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		QCOMPARE(eventCount(), 1);
		QCOMPARE(tempoAtPosition(), 120);
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the map edit");
		QCOMPARE(eventCount(), 0);
		QCOMPARE(mapActive(), false);
		QCOMPARE(tempoAtPosition(), global);
		QCOMPARE(Engine::getSong()->tempoAtTick(768), global);

		// (2) A second event is taken back to the first, not to nothing.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 120}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 768}, {QStringLiteral("bpm"), 180}}).ok);
		QCOMPARE(eventCount(), 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(eventCount(), 1);
		QCOMPARE(Engine::getSong()->tempoAtTick(768), 120);

		// (3) A merge at an existing tick: the undo restores the half the call
		// replaced, not only the event count.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 0}, {QStringLiteral("numerator"), 5},
				{QStringLiteral("denominator"), 4}}).ok);
		QCOMPARE(mapState().value(QStringLiteral("timesig_at_position")).toString(),
			QStringLiteral("5/4"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(eventCount(), 1);
		QCOMPARE(mapState().value(QStringLiteral("timesig_at_position")).toString(),
			QStringLiteral("4/4"));

		// (4) A remove: the event comes back.
		QVERIFY(run(QStringLiteral("transport.tempo_map_remove"),
			{{QStringLiteral("tick"), 0}}).ok);
		QCOMPARE(eventCount(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(eventCount(), 1);
		QCOMPARE(Engine::getSong()->tempoAtTick(0), 120);

		// (5) set_active, and (6) clear: BOTH halves of clear (the events and
		// the active flag) come back as one step.
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"),
			{{QStringLiteral("tick"), 384}, {QStringLiteral("bpm"), 90}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_set_active"),
			{{QStringLiteral("active"), false}}).ok);
		QCOMPARE(mapActive(), false);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(mapActive(), true);

		QCOMPARE(eventCount(), 2);
		QVERIFY(run(QStringLiteral("transport.tempo_map_clear")).ok);
		QCOMPARE(eventCount(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(eventCount(), 2);
		QCOMPARE(mapActive(), true);
		QCOMPARE(Engine::getSong()->tempoAtTick(0), 120);
		QCOMPARE(Engine::getSong()->tempoAtTick(384), 90);
	}
};

QTEST_GUILESS_MAIN(ControlTempoMapCommandsTest)
#include "ControlTempoMapCommandsTest.moc"
