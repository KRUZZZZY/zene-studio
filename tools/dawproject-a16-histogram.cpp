/*
 * dawproject-a16-histogram.cpp - THE SPEC A16 histogram, MEASURED off the tree.
 *
 * tests/src/core/ReversibilityContractTest.cpp asserts a row count and the four
 * class counts against a constant. The lane brief calls that constant a
 * MEASUREMENT, not a sum to compute on a branch: this probe measures it, exactly
 * the way ReversibilityTable's own constructor assembles the table - the four
 * literal blocks (the true_inverse join, the snapshot block, the passive block,
 * the stem block) inserted into a keyed map in the constructor's own order, so a
 * command declared in two blocks counts once, as it does in the product.
 *
 * The ctest's constant is the base for a configuration with no telemetry, no
 * wasmtime and no stem engine, and `documentedHistogram()` adds the rows a build
 * option compiles in. So compare like with like: the number this prints is what
 * the TEST would measure in the same configuration, and the constants differ by
 * exactly the #ifdef additions.
 *
 * The class counts are printed as `MEASURED rows=... true_inverse=... snapshot=...
 * irreversible=... not_mutating=...`.
 *
 * Build and run it with tools/dawproject-proof.sh, which compiles the tables with
 * the project's own flags. The tables pull in ControlReversibility.cpp, whose
 * undo machinery needs the Engine; this probe is about the TABLE, so the script
 * links the tables without that object and defines the two undo entry points as
 * no-ops here (they are never called).
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

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "ControlReversibility.h"

#ifdef DAWPROJECT_PROOF_LINK
/*! Link-only stubs for ControlReversibility.cpp's undo entry points: this probe
 *  reads the TABLE, and never records anything. Declared here rather than
 *  linking ControlReversibility.o, which needs the whole Engine. */
namespace lmms
{

class JournallingObject;

class ProjectJournal
{
public:
	static void addJournalStructure(std::function<void()>, std::function<void()>, long long);
	static void addJournalCheckPoint(const QList<JournallingObject*>&);
};

class Engine
{
public:
	static ProjectJournal* s_projectJournal;
};

ProjectJournal* Engine::s_projectJournal = nullptr;

namespace control
{

void addStructuralUndoStep(std::function<void()>, std::function<void()>, long long) {}
void addUndoStep(const QList<JournallingObject*>&) {}

} // namespace control
} // namespace lmms
#endif

using namespace lmms::control;

namespace
{

void printBlock(const char* name, const ReversibilityRow* rows, int rowCount)
{
	int trueInverse = 0;
	int snapshot = 0;
	int irreversible = 0;
	int notMutating = 0;
	for (int index = 0; index < rowCount; index++)
	{
		switch (rows[index].cls)
		{
			case ReversibilityClass::TrueInverse: trueInverse++; break;
			case ReversibilityClass::Snapshot: snapshot++; break;
			case ReversibilityClass::Irreversible: irreversible++; break;
			case ReversibilityClass::NotMutating: notMutating++; break;
		}
	}
	std::printf("%-10s rows=%3d true_inverse=%3d snapshot=%3d irreversible=%3d not_mutating=%3d\n",
		name, rowCount, trueInverse, snapshot, irreversible, notMutating);
}

//! The commands declared MORE THAN ONCE across the blocks, sorted. Each block is
//! a raw literal array, so the same command in two of them is two declarations
//! and - because the constructor inserts into one keyed map - ONE entry: the
//! difference is the duplicate count, and until 2026-09-16 it was a hand-kept
//! note (24 rows in the live block, retired by board card #677) rather than a
//! measured invariant. Named here so the count can be a refusal and not a
//! number a reader has to remember.
QStringList declaredTwice(const QVector<QPair<const ReversibilityRow*, int>>& blocks)
{
	QHash<QString, int> declarations;
	for (const QPair<const ReversibilityRow*, int>& block : blocks)
	{
		for (int index = 0; index < block.second; index++)
		{
			++declarations[QString::fromUtf8(block.first[index].command)];
		}
	}
	QStringList duplicates;
	for (auto it = declarations.constBegin(); it != declarations.constEnd(); ++it)
	{
		if (it.value() > 1) { duplicates.append(it.key()); }
	}
	duplicates.sort();
	return duplicates;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	int joined = 0;
	const ReversibilityRow* rows = reversibilityRowTable(&joined);
	int snapshots = 0;
	const ReversibilityRow* snapshotRows = reversibilitySnapshotRowTable(&snapshots);
	int passive = 0;
	const ReversibilityRow* passiveRows = reversibilityPassiveRowTable(&passive);
	int stems = 0;
	const ReversibilityRow* stemRows = reversibilityStemRowTable(&stems);

	printBlock("joined", rows, joined);
	printBlock("snapshot", snapshotRows, snapshots);
	printBlock("passive", passiveRows, passive);
	printBlock("stems", stemRows, stems);

	// The constructor's own assembly, in its own order: later blocks overwrite.
	QHash<QString, ReversibilityClass> entries;
	const auto insert = [&entries](const ReversibilityRow* block, int count)
	{
		for (int index = 0; index < count; index++)
		{
			entries.insert(QString::fromUtf8(block[index].command), block[index].cls);
		}
	};
	insert(rows, joined);
	insert(snapshotRows, snapshots);
	insert(passiveRows, passive);
	insert(stemRows, stems);

	int trueInverse = 0;
	int snapshot = 0;
	int irreversible = 0;
	int notMutating = 0;
	for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
	{
		switch (it.value())
		{
			case ReversibilityClass::TrueInverse: trueInverse++; break;
			case ReversibilityClass::Snapshot: snapshot++; break;
			case ReversibilityClass::Irreversible: irreversible++; break;
			case ReversibilityClass::NotMutating: notMutating++; break;
		}
	}
	std::printf("MEASURED rows=%d true_inverse=%d snapshot=%d irreversible=%d not_mutating=%d\n",
		static_cast<int>(entries.size()), trueInverse, snapshot, irreversible, notMutating);

	// The table's integrity derivation, and the reason this probe is worth
	// running before quoting the line above: the four blocks are RAW literal
	// arrays, the constructor inserts them into ONE keyed map, and a command
	// declared in two blocks is one entry - so a duplicate is invisible in every
	// figure above and visible only as declared > entries. It is a refusal
	// (exit 1) rather than a footnote, because the count of these was a hand-kept
	// note for three waves ("24 pre-existing cross-file duplicates").
	const QVector<QPair<const ReversibilityRow*, int>> blocks = {
		{rows, joined}, {snapshotRows, snapshots}, {passiveRows, passive}, {stemRows, stems}};
	const QStringList duplicates = declaredTwice(blocks);
	const int declared = joined + snapshots + passive + stems;
	std::printf("DECLARED rows=%d entries=%d duplicates=%d\n", declared,
		static_cast<int>(entries.size()), static_cast<int>(duplicates.size()));
	for (const QString& duplicate : duplicates)
	{
		std::printf("  DECLARED TWICE: %s\n", qPrintable(duplicate));
	}

	for (int index = 0; index < joined; index++)
	{
		const QString command = QString::fromUtf8(rows[index].command);
		if (command.startsWith(QLatin1String("dawproject.")))
		{
			std::printf("  %-24s class=%d reversible=%d\n", rows[index].command,
				static_cast<int>(rows[index].cls), rows[index].reversible ? 1 : 0);
		}
	}
	// 1 = the probe could not measure a table (nothing declared), or the table
	// declares a command twice: either way the figures above must not be quoted.
	return (entries.isEmpty() || !duplicates.isEmpty()) ? 1 : 0;
}
