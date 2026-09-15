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
 * The release notes quote the RELEASE configuration's figures - 227 rows /
 * 120 / 18 / 7 / 82 with the telemetry client compiled in and no wasmtime,
 * 225 / 120 / 18 / 7 / 80 with the client out - which these reduce to. The six
 * `wasm.*` rows (item #614: three snapshot, three not_mutating) are present
 * exactly when the wasmtime C API is: without it WANT_WASM degrades to OFF, the
 * group's sources are not compiled, ControlRegistry.cpp's #ifdef removes its
 * registration and the rows leave the table with the ids - which is the
 * direction the other tests in this file assert (every row names a registered
 * command). The seven `stem.*` rows below follow the same rule in the same
 * direction: the offline stem engine is compiled only when WANT_STEM_SPLIT is
 * ON - OFF in the release configuration - so its rows are present exactly when
 * its ids are, and the figures above are the release configuration's own.
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
	/*! telemetry-off, wasm-off base; the guards add the rest.
	 *
	 *  RE-MEASURED ON THE MERGED TIP, 2026-09-15, by the wave-4 integration
	 *  train - the constant is a MEASUREMENT and this is the tree's own number,
	 *  not a sum of anybody's lane report. Measured with
	 *  `bash tools/dawproject-proof.sh` (its part 2, the A16 histogram probe, on its own
	 *  on its own: the probe tools/dawproject-a16-histogram.cpp compiled against
	 *  this tree's src/core/ControlReversibilityTable*.cpp with this build's own
	 *  flags, the tables assembled the way ReversibilityTable's constructor
	 *  assembles them). It printed, for this configuration (ZENE_TELEMETRY_ENABLED
	 *  on, -DLMMS_HAVE_WASM=1, WANT_STEM_SPLIT off):
	 *
	 *      MEASURED rows=322 true_inverse=156 snapshot=30 irreversible=10 not_mutating=126
	 *
	 *  The base below is that measurement with the guards' own additions removed
	 *  (-2 rows / -2 not_mutating for telemetry, -6 rows / -3 snapshot /
	 *  -3 not_mutating for wasm; stem is off, so its seven rows are absent from
	 *  both sides), i.e. 314 / 156 / 27 / 10 / 121 - and the four class columns
	 *  sum to 314 exactly.
	 *
	 *  WHAT MOVED IT SINCE THE WAVE-3 TIP (285 / 152 / 21 / 6 / 106 in the same
	 *  base): the eight branches this train merged. Their deltas are each named
	 *  beside their own rows in src/core/ControlReversibilityTable*.cpp, and the
	 *  merge commits record the per-file union counts; the numbers above are what
	 *  the TABLE measures, which is the thing this assertion is about.
	 *
	 *  DRIFT WARNING, STATED RATHER THAN HIDDEN: the A16 paragraph in
	 *  docs/RELEASE-NOTES-v0.3.0-alpha.md still carries the wave-3 train's
	 *  figures (284 rows and the per-lane deltas around it). The two are meant to
	 *  agree; re-taking that paragraph is the merge point's remaining doc task,
	 *  named in the train's report rather than silently reconciled here.
	 *
	 *  The older history below is kept because each paragraph names a real
	 *  branch-local figure and why it is not this tree's number.
	 *
	 *  THE WAVE-2 FIVE-LANE TRAIN'S MEASUREMENT, as it stood before this merge:
	 *  the table measured 283 rows over the four classes - 151 true_inverse,
	 *  21 snapshot, 6 irreversible, 105 not_mutating - so its base was
	 *  281 / 151 / 21 / 6 / 103 with the telemetry guard adding the two
	 *  telemetry.* rows back. EVERY FIGURE A LANE WROTE WHILE IT WAS LANDING WAS
	 *  BRANCH-LOCAL, measured on the lane's own base - 228 from
	 *  030/undo-structural, 236 from 030/chord-track, 227 from 030/project-archive
	 *  - against the 265 / 141 / 21 / 7 / 96 the wave-1 eight-lane train's tip
	 *  carried. None of them is this tree's number, and the constant is not a
	 *  sum of anybody's report either: it was measured. The undo lane's one
	 *  RECLASSIFICATION shows as the irreversible column going DOWN - a removed
	 *  device is re-instantiated with its settings by one `control.undo`, so
	 *  plugin.unload left the irreversible block for true_inverse (-1
	 *  irreversible, +1 true_inverse) and its four structural rows
	 *  (track.add, track.move, track.remove, plugin.unload) live in their own
	 *  table TU, src/core/ControlReversibilityTableStructure.cpp, joined into
	 *  the action half so the block still reads as ONE `true_inverse` block with
	 *  one row count. Each lane's delta is named beside its rows in
	 *  src/core/ControlReversibilityTable*.cpp. */
	DocumentedHistogram out{314, 156, 27, 10, 121};
#ifdef ZENE_TELEMETRY_ENABLED
	out.rows += 2;          // the two telemetry.* commands' not_mutating rows
	out.notMutating += 2;
#endif
#ifdef LMMS_HAVE_WASM
	out.rows += 6;          // wasm.load / unload / set_param, list / get_state / process
	out.snapshot += 3;
	out.notMutating += 3;
#endif
#ifdef LMMS_HAVE_STEM_SPLIT
	// The seven stem.* rows (feature row 26, board task #653): stem.get_state,
	// stem.job_start / job_status / job_result / job_cancel, and the two
	// model-store verbs. All seven are not_mutating - one offline engine,
	// output artefacts and no project state - and they are present exactly
	// when the engine is (WANT_STEM_SPLIT, OFF by default), so the release
	// configuration's figures are unchanged by them.
	out.rows += 7;
	out.notMutating += 7;
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
	//! At 0.3.0 the same four counts read 227 / 120 / 18 / 7 / 82 over 227 rows at
	//! the three-merge tip this train lands, and the
	//! current figure lives in docs/RELEASE-NOTES-v0.3.0-alpha.md. Two compile-time
	//! groups move with their option and are ADDED to the invariant part rather
	//! than written out per configuration: the two `telemetry.*` not_mutating rows
	//! (ZENE_TELEMETRY_ENABLED) and the six `wasm.*` rows - three snapshot, three
	//! not_mutating - which are present exactly when the wasmtime C API is
	//! (LMMS_HAVE_WASM, item #614). The release configuration has the client in and
	//! no wasmtime, so the notes' own figures are its 227 / 120 / 18 / 7 / 82.
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
	//! commands the table calls "writes nothing" are the four the handlers
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
			// Declared mutating and refused on every call: this build has no
			// upload and no network code of any kind in the crash reporter
			// (include/CrashReporter.h), so no send is faked.
			QStringLiteral("crash.upload_report"),
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
