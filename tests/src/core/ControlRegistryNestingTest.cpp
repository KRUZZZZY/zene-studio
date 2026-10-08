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
#include "Engine.h"
#include "ProjectJournal.h"

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
		QVERIFY(registry->invoke(source.id).ok);
		QCOMPARE(registry->transactions().size(), 1);
		const ControlResult undone = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY(undone.ok);
		QCOMPARE(undone.result.value(QStringLiteral("restored_by")).toString(), target.id);
		QCOMPARE(registry->transactions().size(), 1);
	}

};

QTEST_GUILESS_MAIN(ControlRegistryNestingTest)
#include "ControlRegistryNestingTest.moc"
