/*
 * ControlRegistryNestingTest.cpp - the undo-record bookkeeping for nested and wrapped commands
 *                                 (SPEC A16; one record per journal undo step).
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

#include <QtTest>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "ProvenanceSection.h"

using namespace lmms;

class ControlRegistryNestingTest : public QObject
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

	void cleanup()
	{
		ControlRegistry::setReady(true);
	}

	//! A mutating test command whose handler describes \a op as its inverse; with \a flag it also
	//! pushes one journal action that sets *flag false on undo and true on redo.
	static ControlCommand describedCommand(const QString& id, bool mutating, const QString& op, bool* flag = nullptr)
	{
		ControlCommand command;
		command.id = id;
		command.group = QStringLiteral("test");
		command.verb = id.mid(QStringLiteral("test.").size());
		command.mutating = mutating;
		command.handler = [op, flag](const QJsonObject&) {
			if (flag != nullptr)
			{
				Engine::projectJournal()->addJournalAction([flag] { *flag = false; }, [flag] { *flag = true; });
			}
			QJsonObject transaction{{QStringLiteral("before"), QJsonObject{{QStringLiteral("value"), 1}}},
				{QStringLiteral("inverse"), QJsonObject{{QStringLiteral("op"), op}}},
				{QStringLiteral("reversible"), true}, {QStringLiteral("mechanism"), QStringLiteral("test")}};
			return ControlResult::success(QJsonObject{{QStringLiteral("__transaction"), transaction}});
		};
		return command;
	}

	//! A non-mutating wrapper running two mutating children that share ONE journal step: one undo
	//! step gives ONE record for the wrapper, and control.undo restores both children.
	void nonMutatingWrapperRecordsOneStepAsOneRecord()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		registry->clearTransactions();
		// static: the journal keeps these steps after the test returns, and a later undo or redo runs them.
		static bool touchedA = true;
		static bool touchedB = true;
		touchedA = touchedB = true;
		const ControlCommand childA = describedCommand(QStringLiteral("test.wrap_child_a"), true, QStringLiteral("test.wrap_child_a"), &touchedA);
		const ControlCommand childB = describedCommand(QStringLiteral("test.wrap_child_b"), true, QStringLiteral("test.wrap_child_b"), &touchedB);
		registry->registerCommand(childA);
		registry->registerCommand(childB);
		ControlCommand wrapper = describedCommand(QStringLiteral("test.wrap_outer"), false, QString());
		wrapper.handler = [childA, childB](const QJsonObject& args) {
			const bool first = ControlRegistry::instance()->invoke(childA.id, args).ok;
			const bool second = ControlRegistry::instance()->invoke(childB.id, args).ok;
			return first && second ? ControlResult::success() : ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a child refused"));
		};
		registry->registerCommand(wrapper);

		const int depthBefore = journal->undoDepth();
		QVERIFY(registry->invoke(wrapper.id).ok);
		QCOMPARE(journal->undoDepth() - depthBefore, 1);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		const QJsonObject record = transactions.last().toObject();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), wrapper.id);
		QVERIFY(record.value(QStringLiteral("reversible")).toBool());
		QCOMPARE(record.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("children")).toArray().size(), 2);
		// The aggregate's provenance change names the children it covers (none is dropped).
		const provenance::Section& section = provenance::Section::instance();
		QCOMPARE(section.at(section.size() - 1).nested, QStringLiteral("test.wrap_child_a test.wrap_child_b"));
		QVERIFY(registry->invoke(QStringLiteral("control.undo")).ok);
		QVERIFY(!touchedA);
		QVERIFY(!touchedB);
	}

	//! A mutating outer call over a folded nested change must not claim the step reverses it. Three cases:
	//! an outer with no descriptor, an outer whose own descriptor says reversible, and a mutating ->
	//! non-mutating -> mutating chain. In each the single record says the nested change is not covered.
	void foldedNestedChangeIsNotClaimedReversible()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand inner = describedCommand(QStringLiteral("test.folded_inner"), true, QStringLiteral("test.folded_inner"));
		registry->registerCommand(inner);
		ControlCommand middle = describedCommand(QStringLiteral("test.folded_middle"), false, QString());
		middle.handler = [inner](const QJsonObject& args) {
			return ControlRegistry::instance()->invoke(inner.id, args).ok ? ControlResult::success()
				: ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("refused"));
		};
		registry->registerCommand(middle);
		const auto runCase = [registry, inner, middle](const QString& outerId, bool describes, bool viaMiddle) {
			ControlCommand outer = describedCommand(outerId, true, QString());
			outer.handler = [inner, middle, describes, viaMiddle](const QJsonObject& args) {
				const bool nested = viaMiddle ? ControlRegistry::instance()->invoke(middle.id, args).ok
					: ControlRegistry::instance()->invoke(inner.id, args).ok;
				if (!nested) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("refused")); }
				ControlResult result = ControlResult::success();
				if (describes)
				{
					result.result.insert(QStringLiteral("__transaction"), QJsonObject{{QStringLiteral("before"), QJsonObject()},
						{QStringLiteral("inverse"), QJsonObject()}, {QStringLiteral("reversible"), true},
						{QStringLiteral("mechanism"), QStringLiteral("outer")}});
				}
				return result;
			};
			registry->registerCommand(outer);
			registry->clearTransactions();
			QVERIFY(registry->invoke(outer.id).ok);
			const QJsonArray transactions = registry->transactions();
			QCOMPARE(transactions.size(), 1);
			const QJsonObject record = transactions.last().toObject();
			QVERIFY(!record.value(QStringLiteral("reversible")).toBool());
			QVERIFY2(record.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("ran inside itself")),
				qPrintable(record.value(QStringLiteral("mechanism")).toString()));
		};
		runCase(QStringLiteral("test.fold_bare"), false, false);
		runCase(QStringLiteral("test.fold_described"), true, false);
		runCase(QStringLiteral("test.fold_via_wrapper"), false, true);
	}

	//! control.undo runs a command-applies inverse: that run must not add a record, so the count holds.
	void undoThroughCommandInverseAddsNoRecord()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();
		ControlCommand target = describedCommand(QStringLiteral("test.undo_target"), true, QStringLiteral("test.undo_target"));
		target.handler = [](const QJsonObject&) { return ControlResult::success(); };
		registry->registerCommand(target);
		ControlCommand source = describedCommand(QStringLiteral("test.undo_source"), true, QStringLiteral("test.undo_target"));
		source.handler = [](const QJsonObject&) {
			QJsonObject transaction{{QStringLiteral("before"), QJsonObject()},
				{QStringLiteral("inverse"), QJsonObject{{QStringLiteral("applies"), QStringLiteral("command")},
					{QStringLiteral("op"), QStringLiteral("test.undo_target")}}},
				{QStringLiteral("reversible"), true}, {QStringLiteral("mechanism"), QStringLiteral("test")}};
			return ControlResult::success(QJsonObject{{QStringLiteral("__transaction"), transaction}});
		};
		registry->registerCommand(source);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QVERIFY(registry->invoke(source.id).ok);
		QCOMPARE(registry->transactions().size(), 1);
		const int depthBefore = journal->undoDepth();
		const ControlResult undone = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY(undone.ok);
		QCOMPARE(undone.result.value(QStringLiteral("restored_by")).toString(), target.id);
		QCOMPARE(registry->transactions().size(), 1);
		// The inverse is a command, so the undo takes no journal step: the depth is untouched too.
		QCOMPARE(journal->undoDepth(), depthBefore);
	}


	//! A mutating call under a wrapper whose OWN descriptor is folded (its nested change not
	//! covered) is flagged too, on the held path: wrapper -> C (described) -> D (described).
	void foldedChangeUnderAHeldCallIsFlagged()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();
		const ControlCommand inner = describedCommand(QStringLiteral("test.held_inner"), true, QStringLiteral("test.held_inner"));
		registry->registerCommand(inner);
		ControlCommand middle = describedCommand(QStringLiteral("test.held_middle"), true, QStringLiteral("test.held_middle"));
		middle.handler = [inner](const QJsonObject& args) {
			ControlResult result = ControlRegistry::instance()->invoke(inner.id, args);
			if (!result.ok) { return result; }
			result = ControlResult::success();
			result.result.insert(QStringLiteral("__transaction"), QJsonObject{{QStringLiteral("before"), QJsonObject()},
				{QStringLiteral("inverse"), QJsonObject{{QStringLiteral("op"), QStringLiteral("test.held_middle")}}},
				{QStringLiteral("reversible"), true}, {QStringLiteral("mechanism"), QStringLiteral("middle")}});
			return result;
		};
		registry->registerCommand(middle);
		ControlCommand wrapper = describedCommand(QStringLiteral("test.held_wrapper"), false, QString());
		wrapper.handler = [middle](const QJsonObject& args) {
			return ControlRegistry::instance()->invoke(middle.id, args);
		};
		registry->registerCommand(wrapper);
		QVERIFY(registry->invoke(wrapper.id).ok);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		const QJsonObject record = transactions.last().toObject();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), middle.id);
		QVERIFY(!record.value(QStringLiteral("reversible")).toBool());
		QVERIFY2(record.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("ran inside itself")),
			qPrintable(record.value(QStringLiteral("mechanism")).toString()));
	}

	//! The class of an aggregate is the weakest of its children's, and never the wrapper's own class.
	void aggregateClassIsTheWeakestChildClass()
	{
		using R = ControlRegistry;
		QCOMPARE(R::aggregateClassOf({QStringLiteral("true_inverse"), QStringLiteral("true_inverse")}), QStringLiteral("true_inverse"));
		QCOMPARE(R::aggregateClassOf({QStringLiteral("true_inverse"), QStringLiteral("snapshot")}), QStringLiteral("snapshot"));
		QCOMPARE(R::aggregateClassOf({QStringLiteral("snapshot"), QStringLiteral("irreversible")}), QStringLiteral("irreversible"));
		// A child with no row, or one that is not mutating, leaves nothing honest to claim.
		QCOMPARE(R::aggregateClassOf({QStringLiteral("true_inverse"), QString()}), QString());
		QCOMPARE(R::aggregateClassOf({QStringLiteral("not_mutating"), QStringLiteral("true_inverse")}), QString());
		QCOMPARE(R::aggregateClassOf({}), QString());
	}

	//! Two mixer.set_volume calls under a wrapper, after an earlier call on the same channel. Before the
	//! fix the first wrapped call coalesced into the EARLIER step, so the wrapper's step was empty. Now the
	//! wrapper's two calls are one new step: depth +1, one aggregate record of class true_inverse, and
	//! control.undo takes the whole step back.
	void wrappedCallsDoNotCoalesceIntoAnEarlierStep()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		registry->clearTransactions();
		// The master channel's persistent id, whatever number this build gave it.
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(0));
		const QJsonObject earlier{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.5}};
		QVERIFY(registry->invoke(QStringLiteral("mixer.set_volume"), earlier).ok);
		const int depthBefore = journal->undoDepth();
		const int recordsBefore = registry->transactions().size();

		ControlCommand wrapper = describedCommand(QStringLiteral("test.volume_wrapper"), false, QString());
		wrapper.handler = [channel](const QJsonObject&) {
			const bool first = ControlRegistry::instance()->invoke(QStringLiteral("mixer.set_volume"),
				{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.6}}).ok;
			const bool second = ControlRegistry::instance()->invoke(QStringLiteral("mixer.set_volume"),
				{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.7}}).ok;
			return first && second ? ControlResult::success() : ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("refused"));
		};
		registry->registerCommand(wrapper);
		QVERIFY(registry->invoke(wrapper.id).ok);
		QCOMPARE(journal->undoDepth() - depthBefore, 1);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size() - recordsBefore, 1);
		const QJsonObject record = transactions.last().toObject();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), wrapper.id);
		QCOMPARE(record.value(QStringLiteral("class")).toString(), QStringLiteral("true_inverse"));
		QCOMPARE(record.value(QStringLiteral("commands")).toInt(), 2);
		QVERIFY(record.value(QStringLiteral("reversible")).toBool());

		QVERIFY(registry->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(journal->undoDepth(), depthBefore);
	}

	//! A mutating call run inside a mutating call joins no earlier step: the outer call's step must contain it.
	void aMutatingChildOfAMutatingCallDoesNotJoinAnEarlierStep()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		registry->clearTransactions();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(0));
		QVERIFY(registry->invoke(QStringLiteral("mixer.set_volume"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.5}}).ok);
		ControlCommand outer = describedCommand(QStringLiteral("test.guard_outer"), true, QString());
		outer.handler = [channel](const QJsonObject&) {
			return ControlRegistry::instance()->invoke(QStringLiteral("mixer.set_volume"),
				{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.6}});
		};
		registry->registerCommand(outer);
		const int depthBefore = journal->undoDepth();
		const int recordsBefore = registry->transactions().size();
		QVERIFY(registry->invoke(outer.id).ok);
		QCOMPARE(journal->undoDepth() - depthBefore, 1);
		QCOMPARE(registry->transactions().size() - recordsBefore, 1);
	}

	//! A wrapper that declares coalescing (arrangement.nudge) makes a burst of presses ONE undo step: each
	//! press extends the run's record, and control.undo restores the position from before the burst. A
	//! press after an unrelated command starts a new step (and a new record).
	void aBurstOfWrapperCallsIsOneUndoStep()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		registry->clearTransactions();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(0));
		static bool unrelatedTouched = true;  // static: the journal keeps the step after the test returns
		const ControlCommand unrelated = describedCommand(QStringLiteral("test.burst_unrelated"), true,
			QStringLiteral("test.burst_unrelated"), &unrelatedTouched);
		registry->registerCommand(unrelated);
		ControlCommand nudge = describedCommand(QStringLiteral("test.burst_nudge"), false, QString());
		nudge.coalescesWrapped = QStringLiteral("mixer.set_volume");
		nudge.handler = [channel](const QJsonObject& args) {
			return ControlRegistry::instance()->invoke(QStringLiteral("mixer.set_volume"),
				{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), args.value(QStringLiteral("volume")).toDouble()}});
		};
		registry->registerCommand(nudge);
		const auto press = [registry, &nudge](double volume) {
			return registry->invoke(nudge.id, {{QStringLiteral("volume"), volume}}).ok;
		};
		QVERIFY(registry->invoke(QStringLiteral("mixer.set_volume"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), 0.5}}).ok);
		QVERIFY(registry->invoke(unrelated.id).ok);  // breaks any run: the presses start their own
		const float pre = Engine::mixer()->mixerChannel(0)->m_volumeModel.value();
		const int depthBefore = journal->undoDepth();
		const int recordsBefore = registry->transactions().size();

		QVERIFY(press(0.6));
		QVERIFY(press(0.7));
		QVERIFY(press(0.8));
		QCOMPARE(journal->undoDepth() - depthBefore, 1);
		QCOMPARE(registry->transactions().size() - recordsBefore, 1);
		const QJsonObject record = registry->transactions().last().toObject();
		// One nested call: the record is that call's own (as for one track), extended by each later press.
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("mixer.set_volume"));
		QCOMPARE(record.value(QStringLiteral("commands")).toInt(), 3);
		QVERIFY(record.value(QStringLiteral("reversible")).toBool());
		QVERIFY(registry->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(journal->undoDepth(), depthBefore);
		QVERIFY(qFuzzyCompare(Engine::mixer()->mixerChannel(0)->m_volumeModel.value(), pre));

		// After an unrelated command, a press is a new step and a new record.
		QVERIFY(registry->invoke(unrelated.id).ok);
		const int afterUnrelated = registry->transactions().size();
		QVERIFY(press(0.6));
		QVERIFY(registry->invoke(unrelated.id).ok);
		QVERIFY(press(0.7));
		QCOMPARE(registry->transactions().size() - afterUnrelated, 3);  // press, unrelated, press: three records
	}

	//! A recorded call that produces NO journal step breaks a coalescing run: the press after it opens its own
	//! step and record, and never extends the step-less record (a press before and after a step-less one).
	void aStepLessRecordBreaksACoalescingRun()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		registry->clearTransactions();
		static bool breakerTouched = true;  // static: the journal keeps the step after the test returns
		const ControlCommand breaker = describedCommand(QStringLiteral("test.step_breaker"), true,
			QStringLiteral("test.step_breaker"), &breakerTouched);
		registry->registerCommand(breaker);
		const ControlCommand stepless = describedCommand(QStringLiteral("test.stepless"), true, QStringLiteral("test.stepless"));
		registry->registerCommand(stepless);
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(0));
		const auto press = [registry, &channel](double volume) {
			return registry->invoke(QStringLiteral("mixer.set_volume"),
				{{QStringLiteral("channel"), channel}, {QStringLiteral("volume"), volume}}).ok;
		};

		QVERIFY(registry->invoke(breaker.id).ok);  // an unrelated step: no run is open
		const int depthBefore = journal->undoDepth();
		const int recordsBefore = registry->transactions().size();
		QVERIFY(press(0.6));
		QVERIFY(registry->invoke(stepless.id).ok);  // records a transaction, pushes no journal step
		QVERIFY(press(0.7));
		QCOMPARE(journal->undoDepth() - depthBefore, 2);  // the two presses are two steps, not one
		QCOMPARE(registry->transactions().size() - recordsBefore, 3);
		QCOMPARE(registry->transactions().last().toObject().value(QStringLiteral("command")).toString(),
			QStringLiteral("mixer.set_volume"));
	}
};

QTEST_GUILESS_MAIN(ControlRegistryNestingTest)
#include "ControlRegistryNestingTest.moc"
