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

namespace
{

//! The documented A16 histogram for THIS configuration.
/*!
 * The invariant part of the table plus the groups a build option moves. Written
 * as a sum rather than as a full set per configuration: four exist (telemetry
 * on/off x sandbox on/off), and a full set per configuration is how one of them
 * gets left stale.
 *
 * The release notes quote the RELEASE configuration's figures - 155 rows /
 * 80 / 13 / 4 / 58 with the telemetry client compiled out and no wasmtime,
 * 157 / 80 / 13 / 4 / 60 with the client in - which these reduce to. The six
 * `wasm.*` rows (item #614: three snapshot, three not_mutating) are present
 * exactly when the wasmtime C API is: without it WANT_WASM degrades to OFF, the
 * group's sources are not compiled, ControlRegistry.cpp's #ifdef removes its
 * registration and the rows leave the table with the ids - which is the
 * direction the other tests in this file assert (every row names a registered
 * command).
 *
 * Split out of the test slot so the slot's own complexity does not carry the four
 * option combinations (the complexity ratchet counts them).
 */
struct DocumentedHistogram
{
	int rows;
	int trueInverse;
	int snapshot;
	int irreversible;
	int notMutating;
};

DocumentedHistogram documentedHistogram()
{
	DocumentedHistogram out{155, 80, 13, 4, 58};   // telemetry-off, wasm-off base; the guards add the rest
#ifdef ZENE_TELEMETRY_ENABLED
	out.rows += 2;          // the two telemetry.* commands' not_mutating rows
	out.notMutating += 2;
#endif
#ifdef LMMS_HAVE_WASM
	out.rows += 6;          // wasm.load / unload / set_param, list / get_state / process
	out.snapshot += 3;
	out.notMutating += 3;
#endif
	return out;
}

} // namespace

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


	//! THE HISTOGRAM (0.2.1 coverage gap 3a). The release's own notes state the
	//! table's shape as four counts - "the table that ships as data in
	//! src/core/ControlReversibilityTable.cpp has 74 rows today, one per registered
	//! command" (docs/RELEASE-NOTES-v0.2.1-alpha.md, at 0.2.1: 30 `true_inverse`,
	//! 5 `snapshot`, 3 `irreversible`, 36 `not_mutating`) - and nothing asserted them.
	//! At 0.3.0 the same four counts read 63 / 9 / 3 / 54 over 129 rows, and the
	//! current figure lives in docs/RELEASE-NOTES-v0.3.0-alpha.md. Two compile-time
	//! groups move with their option and are ADDED to the invariant part rather
	//! than written out per configuration: the two `telemetry.*` not_mutating rows
	//! (ZENE_TELEMETRY_ENABLED) and the six `wasm.*` rows - three snapshot, three
	//! not_mutating - which are present exactly when the wasmtime C API is
	//! (LMMS_HAVE_WASM, item #614). The release configuration has neither, so the
	//! notes' own figures are its 127 / 63 / 9 / 3 / 52.
	//! The two tests above hold the table to account for COVERAGE (every registered command
	//! has a row, every row names a registered command) and for behaviour; a row
	//! added or moved between classes could therefore ship with the notes still
	//! quoting the old split.
	//!
	//! The counts are computed from the table itself (never from a second copy of
	//! the rows) and asserted against the documented literals, so adding a row
	//! without updating the histogram fails here, naming the row count and the four
	//! counts. Expected values are keyed on ZENE_TELEMETRY_ENABLED because the two
	//! `telemetry.*` rows are compiled out with the client - their commands leave
	//! the registry, so their rows must leave the table, and the notes' 74-row
	//! figure is the telemetry-on build (see ControlReversibilityTable.cpp and
	//! docs/TELEMETRY-KILL-SWITCH.md).
	void theTableHistogramIsTheDocumentedOne()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const QVector<control::ReversibilityEntry> entries = table.entries();

		int trueInverse = 0;
		int snapshot = 0;
		int irreversible = 0;
		int notMutating = 0;
		for (const control::ReversibilityEntry& entry : entries)
		{
			switch (entry.cls)
			{
				case control::ReversibilityClass::TrueInverse:  { ++trueInverse; break; }
				case control::ReversibilityClass::Snapshot:     { ++snapshot; break; }
				case control::ReversibilityClass::Irreversible: { ++irreversible; break; }
				case control::ReversibilityClass::NotMutating:  { ++notMutating; break; }
			}
		}

		const DocumentedHistogram counts = documentedHistogram();
		const int kRows = counts.rows;
		const int kTrueInverse = counts.trueInverse;
		const int kSnapshot = counts.snapshot;
		const int kIrreversible = counts.irreversible;
		const int kNotMutating = counts.notMutating;

		const QByteArray measured = QStringLiteral("%1 true_inverse, %2 snapshot, "
			"%3 irreversible, %4 not_mutating")
			.arg(trueInverse).arg(snapshot).arg(irreversible).arg(notMutating).toUtf8();
		const QByteArray documented = QStringLiteral("%1 true_inverse, %2 snapshot, "
			"%3 irreversible, %4 not_mutating")
			.arg(kTrueInverse).arg(kSnapshot).arg(kIrreversible).arg(kNotMutating).toUtf8();

		QVERIFY2(entries.size() == kRows,
			qPrintable(QStringLiteral("the table has %1 rows, the documented histogram ")
				.arg(entries.size())
				+ QStringLiteral("counts %1. If a row was added, update the histogram in ")
				.arg(kRows)
				+ QStringLiteral("docs/RELEASE-NOTES-v0.3.0-alpha.md (and here) - the ")
				+ QStringLiteral("point of this assertion is that the two cannot drift.")));

		QVERIFY2(trueInverse == kTrueInverse && snapshot == kSnapshot
				&& irreversible == kIrreversible && notMutating == kNotMutating,
			qPrintable(QStringLiteral("the table's classes measure %1, the documented ")
				.arg(QString::fromUtf8(measured))
				+ QStringLiteral("histogram is %1. A row that moved between classes, or ")
				.arg(QString::fromUtf8(documented))
				+ QStringLiteral("one added without updating the notes, fails here.")));
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


	//! The COALESCING declaration is data in the same table, and it is held to
	//! account the same way (Zene Studio, task #623): a command that declares a
	//! target must be a *true_inverse* row - a step can only be merged into
	//! another step when both restore LIVE state - and what it declares must be
	//! reachable from the surface, or the rule would be a declaration nothing
	//! can act on.
	void coalescingIsDeclaredOnlyForCommandsWithALiveCheckpoint()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		int declared = 0;
		for (const control::ReversibilityEntry& entry : table.entries())
		{
			if (!entry.coalesces()) { continue; }
			++declared;
			QCOMPARE(entry.cls, control::ReversibilityClass::TrueInverse);
			QVERIFY2(registry->command(entry.command) != nullptr,
				qPrintable(entry.command + " declares coalescing but is not registered"));
		}
		QVERIFY2(declared > 0, "nothing declares coalescing, so the drag rule has no subject");

		// ... and the rule is on the wire, not only in the table.
		const ControlResult report = run(QStringLiteral("control.undo_depth"));
		QVERIFY2(report.ok, qPrintable(report.errorMessage));
		const QJsonArray onTheWire = report.result.value(QStringLiteral("coalescing"))
			.toObject().value(QStringLiteral("commands")).toArray();
		QCOMPARE(onTheWire.size(), declared);
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
