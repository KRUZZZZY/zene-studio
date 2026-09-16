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
#include <QFile>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"


//! Where the published A16 histogram lives (tests/CMakeLists.txt passes the path;
//! the fallback makes a build that forgets it fail by name, not check nothing).
#ifndef A16_HISTOGRAM_DOC
#define A16_HISTOGRAM_DOC "/nonexistent/A16_HISTOGRAM_DOC-not-passed-by-the-build"
#endif

using namespace lmms;
using namespace revtest;

namespace
{

//! The five numbers of the A16 histogram, in the table's four classes.
struct A16Figure
{
	int rows = 0;
	int trueInverse = 0;
	int snapshot = 0;
	int irreversible = 0;
	int notMutating = 0;

	QString toString() const
	{
		return QStringLiteral("%1 rows = %2 true_inverse + %3 snapshot + %4 irreversible + %5 not_mutating")
			.arg(rows).arg(trueInverse).arg(snapshot).arg(irreversible).arg(notMutating);
	}

	//! Adds \a value under \a name; false when the name is not one of the five.
	bool add(const QString& name, int value)
	{
		if (name == QLatin1String("rows")) { rows += value; }
		else if (name == QLatin1String("true_inverse")) { trueInverse += value; }
		else if (name == QLatin1String("snapshot")) { snapshot += value; }
		else if (name == QLatin1String("irreversible")) { irreversible += value; }
		else if (name == QLatin1String("not_mutating")) { notMutating += value; }
		else { return false; }
		return true;
	}
};

//! A build option that moves rows: \a witness is a command it compiles in, \a rows what it adds.
struct A16Option
{
	QString witness;
	bool inPublished = false;
	A16Figure rows;
};

//! The published block: the figure, the witnesses its configuration has in, and each option's rows.
struct A16Published
{
	A16Figure figure;
	QStringList configuration;
	QVector<A16Option> options;
};

//! Parses the `name=value` tokens of one published line into \a figure.
bool addPublishedTokens(const QStringList& tokens, A16Figure& figure, QString& error)
{
	for (const QString& token : tokens)
	{
		const int equals = token.indexOf(QLatin1Char('='));
		bool ok = false;
		const int value = equals < 0 ? 0 : token.mid(equals + 1).toInt(&ok);
		if (!ok || !figure.add(token.left(equals), value))
		{
			error = QStringLiteral("the A16-HISTOGRAM block carries an unknown token: '%1' "
				"(expected name=value: rows/true_inverse/snapshot/irreversible/not_mutating)").arg(token);
			return false;
		}
	}
	return true;
}

//! The text between the A16-HISTOGRAM markers, or an empty string with \a error set.
QString publishedBlockText(QString& error)
{
	QFile doc(QString::fromUtf8(A16_HISTOGRAM_DOC));
	if (!doc.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		error = QStringLiteral("cannot read %1 - the published A16 histogram lives there, and "
			"this test measures the table against it: %2")
			.arg(QString::fromUtf8(A16_HISTOGRAM_DOC), doc.errorString());
		return QString();
	}
	const QString text = QString::fromUtf8(doc.readAll());
	const int begin = text.indexOf(QLatin1String("A16-HISTOGRAM-BEGIN"));
	const int end = text.indexOf(QLatin1String("A16-HISTOGRAM-END"));
	if (begin < 0 || end < begin)
	{
		error = QStringLiteral("%1 carries no A16-HISTOGRAM block (the published figure is "
			"what sits between the BEGIN and END markers)").arg(QString::fromUtf8(A16_HISTOGRAM_DOC));
		return QString();
	}
	return text.mid(begin, end - begin);
}

//! One line of the block: the figure, the published configuration's witnesses, or one
//! option and what it adds. An unrecognised line is refused, never skipped.
bool readPublishedLine(const QString& line, A16Published& published, QString& error)
{
	const QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	const QString keyword = tokens.value(0);
	if (keyword == QLatin1String("measured:"))
	{
		return addPublishedTokens(tokens.mid(1), published.figure, error);
	}
	if (keyword == QLatin1String("configuration:"))
	{
		published.configuration = tokens.mid(1);
		return true;
	}
	if (keyword == QLatin1String("option"))
	{
		if (tokens.size() < 3)
		{
			error = QStringLiteral("an option line of the A16-HISTOGRAM block is incomplete: "
				"'%1' (expected: option <witness-command> name=value ...)")
				.arg(tokens.join(QLatin1Char(' ')));
			return false;
		}
		A16Option option;
		option.witness = tokens.at(1);
		if (!addPublishedTokens(tokens.mid(2), option.rows, error)) { return false; }
		published.options.append(option);
		return true;
	}
	error = QStringLiteral("the A16-HISTOGRAM block carries a line this test does not know: '%1'")
		.arg(tokens.join(QLatin1Char(' ')));
	return false;
}

//! The ONE published figure (and the options that move it): this test holds NO copy.
bool readPublishedFigure(A16Published& published, QString& error)
{
	const QString block = publishedBlockText(error);
	if (block.isEmpty()) { return false; }
	for (const QString& raw : block.split(QLatin1Char('\n')))
	{
		const QString line = raw.simplified();
		if (line.isEmpty() || line.startsWith(QLatin1String("<!--"))
			|| line.startsWith(QLatin1String("A16-HISTOGRAM")))
		{
			continue;
		}
		if (!readPublishedLine(line, published, error)) { return false; }
	}
	if (published.figure.rows <= 0)
	{
		error = QStringLiteral("the A16-HISTOGRAM block in %1 states no figure (a 'measured:' "
			"line)").arg(QString::fromUtf8(A16_HISTOGRAM_DOC));
		return false;
	}
	for (A16Option& option : published.options)
	{
		option.inPublished = published.configuration.contains(option.witness);
	}
	return true;
}

//! The published figure moved by every option whose presence here differs from it.
A16Figure expectedHere(const A16Published& published, ControlRegistry* registry)
{
	A16Figure expected = published.figure;
	for (const A16Option& option : published.options)
	{
		const bool here = registry->command(option.witness) != nullptr;
		if (here == option.inPublished) { continue; }
		const int sign = here ? 1 : -1;
		expected.rows += sign * option.rows.rows;
		expected.trueInverse += sign * option.rows.trueInverse;
		expected.snapshot += sign * option.rows.snapshot;
		expected.irreversible += sign * option.rows.irreversible;
		expected.notMutating += sign * option.rows.notMutating;
	}
	return expected;
}

//! The table's own histogram: the row total and one count per class.
A16Figure figureOf(const QVector<control::ReversibilityEntry>& entries)
{
	A16Figure figure;
	figure.rows = static_cast<int>(entries.size());
	for (const control::ReversibilityEntry& entry : entries)
	{
		switch (entry.cls)
		{
			case control::ReversibilityClass::TrueInverse: { ++figure.trueInverse; break; }
			case control::ReversibilityClass::Snapshot: { ++figure.snapshot; break; }
			case control::ReversibilityClass::Irreversible: { ++figure.irreversible; break; }
			case control::ReversibilityClass::NotMutating: { ++figure.notMutating; break; }
		}
	}
	return figure;
}

//! Every command the four blocks DECLARE, sorted. The table is a keyed map, so a command
//! declared twice is ONE entry: this list's length minus the entry count is the duplicate
//! count - a hand-kept note ("24 pre-existing cross-file duplicates") until 2026-09-16.
QStringList declaredCommandIds()
{
	QStringList ids;
	for (const control::ReversibilityRow* (*rowsFor)(int*) : {control::reversibilityRowTable,
			control::reversibilitySnapshotRowTable, control::reversibilityPassiveRowTable,
			control::reversibilityStemRowTable})
	{
		int count = 0;
		const control::ReversibilityRow* rows = rowsFor(&count);
		for (int index = 0; index < count; ++index) { ids.append(QString::fromUtf8(rows[index].command)); }
	}
	ids.sort();
	return ids;
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


	//! THE HISTOGRAM, DERIVED ON EVERY RUN (0.2.1 coverage gap 3a; board card #677).
	/*!
	 * The table's shape is a DERIVED artefact and this slot is its derivation: the four
	 * counts are measured off the table here, and the ONE number in circulation - the
	 * figure published in docs/RELEASE-NOTES-v0.3.0-alpha.md, between its
	 * A16-HISTOGRAM-BEGIN/END markers - is READ FROM THAT FILE on every run and compared
	 * with the measurement, so neither can go stale alone. A row added, removed or
	 * reclassified without re-taking the published figure (with `bash
	 * tools/dawproject-proof.sh`, part 2) fails here, naming both figures.
	 *
	 * Two invariants come with it that a hand-kept constant could not hold: every command is
	 * declared ONCE (declared rows == keyed entries, the duplicates named on failure) and
	 * every row is in exactly one class (the counts must sum to the row total). The published
	 * block also declares what each build option adds and a witness command for it, so a build
	 * whose options differ from the published configuration adjusts the figure rather than
	 * failing for its options.
	 */
	void theTableHistogramIsTheDocumentedOne()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const A16Figure measured = figureOf(table.entries());

		const QStringList declared = declaredCommandIds();
		QStringList duplicates;
		for (int index = 1; index < declared.size(); ++index)
		{
			if (declared.at(index) == declared.at(index - 1)
				&& !duplicates.contains(declared.at(index)))
			{
				duplicates.append(declared.at(index));
			}
		}
		QVERIFY2(duplicates.isEmpty(),
			qPrintable(QStringLiteral("the table declares %1 command(s) twice: %2 (one row per "
				"command - see ControlReversibilityTableLive.cpp's retirement note)")
				.arg(duplicates.size()).arg(duplicates.join(QStringLiteral(", ")))));
		QVERIFY2(declared.size() == measured.rows,
			qPrintable(QStringLiteral("the four blocks declare %1 rows, the table has %2 entries")
				.arg(declared.size()).arg(measured.rows)));
		const int classSum = measured.trueInverse + measured.snapshot + measured.irreversible
			+ measured.notMutating;
		QVERIFY2(measured.rows == classSum,
			qPrintable(QStringLiteral("the table measures %1 rows but its four classes sum to %2: a "
				"row is in no class this test counts").arg(measured.rows).arg(classSum)));

		A16Published published;
		QString error;
		QVERIFY2(readPublishedFigure(published, error), qPrintable(error));

		const A16Figure expected = expectedHere(published, registry);
		QVERIFY2(measured.rows == expected.rows && measured.trueInverse == expected.trueInverse
				&& measured.snapshot == expected.snapshot
				&& measured.irreversible == expected.irreversible
				&& measured.notMutating == expected.notMutating,
			qPrintable(QStringLiteral("the table measures %1; %2 publishes %3, which for this build "
				"is %4 - re-measure it with `bash tools/dawproject-proof.sh` (part 2) and re-take the "
				"published block in the same change").arg(measured.toString(),
					QString::fromUtf8(A16_HISTOGRAM_DOC), published.figure.toString(),
					expected.toString())));
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
