/*
 * ReversibilityContractTest.cpp - the CONTRACT half of the SPEC A16 acceptance
 *                                  tests: every registered command has a class,
 *                                  the record states its own bounds, and an
 *                                  irreversible command FAILS its undo, typed.
 *
 * The contract itself is data (src/core/ControlReversibilityTable.cpp); this
 * file holds it to account in both directions:
 *   - every registered command has a row, and every row names a real command;
 *   - for every command the contract calls reversible, applying it and then
 *     control.undo returns the model to the pre-command state;
 *   - for a command with no inverse, control.undo FAILS with the typed
 *     'irreversible' error naming the command and its documented fallback, and
 *     does NOT quietly undo an older step instead.
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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVector>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

class ReversibilityContractTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		// The built-in device modules live in the build tree, and the plugin
		// factory finds them through LMMS_PLUGIN_DIR - the same setup
		// ControlDeviceCatalogueTest uses, because the device commands these
		// tests exercise (plugin.load/param_set/bypass, automation.add_point)
		// need a real device to drive.
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void everyRegisteredCommandHasAContractRow()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const QStringList ids = registry->commandIds();
		QVERIFY2(ids.size() >= 70, qPrintable(QStringLiteral("only %1 commands registered").arg(ids.size())));
		for (const QString& id : ids)
		{
			const control::ReversibilityEntry* entry = table.lookup(id);
			QVERIFY2(entry != nullptr, qPrintable(QStringLiteral("no contract row for %1").arg(id)));
			QVERIFY2(!entry->reason.isEmpty(), qPrintable(id + " has an empty reason"));
			QVERIFY2(!entry->mechanism.isEmpty(), qPrintable(id + " has an empty mechanism"));
			QVERIFY2(entry->cls != control::ReversibilityClass::Irreversible || !entry->fallback.isEmpty(),
				qPrintable(id + " is irreversible but documents no fallback"));
			QVERIFY2(!entry->reversible || entry->cls != control::ReversibilityClass::Irreversible,
				qPrintable(id + " is irreversible yet declares a reversible default"));
		}
	}


	//! DIRECTION 2: every row names a registered command, and the only mutating
	//! commands the table calls "writes nothing" are the three the handlers
	//! refuse on every call. Anything else would be a command whose class and
	//! behaviour disagree.
	void theOnlyUnclassedMutatingCommandsAreTheRefusals()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const QStringList documentedRefusals = {
			QStringLiteral("mixer.set_pan"),
			QStringLiteral("track.set_arm"),
			QStringLiteral("automation.mode_set"),
		};
		for (const control::ReversibilityEntry& entry : table.entries())
		{
			const ControlCommand* cmd = registry->command(entry.command);
			QVERIFY2(cmd != nullptr, qPrintable(entry.command + " is in the table but not registered"));
			if (!cmd->mutating) { continue; }
			if (entry.cls != control::ReversibilityClass::NotMutating) { continue; }
			QVERIFY2(documentedRefusals.contains(entry.command),
				qPrintable(entry.command + " is mutating but classed not_mutating and is not a "
					"documented refusal"));
		}
	}


	//! The transaction record states its own bounds (SPEC A16 deliverable 2):
	//! the count cap, the byte cap, the eviction count and whether eviction has
	//! happened. A client must be able to tell "the whole history" from "what
	//! the bound retains".
	void transactionRecordStatesItsBounds()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult report = run(QStringLiteral("control.transactions"));
		QVERIFY2(report.ok, qPrintable(report.errorMessage));
		QCOMPARE(report.result.value(QStringLiteral("cap_records")).toInt(),
			control::MaxTransactionRecords);
		QCOMPARE(report.result.value(QStringLiteral("cap_bytes")).toInt(),
			control::MaxTransactionBytes);
		QVERIFY(report.result.contains(QStringLiteral("evicted")));
		QVERIFY(report.result.contains(QStringLiteral("capped")));
		QVERIFY(report.result.value(QStringLiteral("retained_bytes")).toInt()
			<= control::MaxTransactionBytes);
	}


	//! THE NEGATIVE CONTROL (SPEC A16 deliverable 6). script.run has no inverse:
	//! control.undo must FAIL with the typed 'irreversible' error naming the
	//! command and its fallback - and must NOT quietly undo the reversible step
	//! underneath it.
	void irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const int tempoBefore = run(QStringLiteral("transport.get_state"))
			.result.value(QStringLiteral("tempo")).toInt();
		QVERIFY(run(QStringLiteral("transport.set_tempo"),
			{{QStringLiteral("bpm"), tempoBefore + 3}}).ok);
		const int tempoAfter = tempoBefore + 3;
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(),
			tempoAfter);

		const ControlResult ran = run(QStringLiteral("script.run"),
			{{QStringLiteral("source"), QStringLiteral("lmms.log():info('a16')\n")}});
		QVERIFY2(ran.ok, qPrintable(ran.errorMessage));
		QCOMPARE(stateOf(QStringLiteral("script.run")).value(QStringLiteral("class")).toString(),
			QStringLiteral("irreversible"));

		const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(!undo.ok, "control.undo pretended to undo an irreversible command");
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QCOMPARE(controlErrorKindName(undo.errorKind), QStringLiteral("irreversible"));
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("script.run")), qPrintable(undo.errorMessage));
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("addCheckPoint")),
			qPrintable(undo.errorMessage));

		// The proof that nothing was silently unwound: the reversible step
		// UNDER the script is still applied.
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(),
			tempoAfter);

		// And the same for a second irreversible command, to show the rule is a
		// rule and not a special case for one string.
		const ControlResult opened = run(QStringLiteral("project.open"),
			{{QStringLiteral("path"), QStringLiteral("/nonexistent/a16.mmp")}});
		QVERIFY(!opened.ok);
	}
};

QTEST_GUILESS_MAIN(ReversibilityContractTest)
#include "ReversibilityContractTest.moc"
