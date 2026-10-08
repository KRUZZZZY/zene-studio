/*
 * ControlRegistryRecords.cpp - the undo-record bookkeeping of nested and wrapped commands (SPEC A16):
 * the aggregate record, the merged-run extension, and the aggregate class.
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

#include "ControlRegistry.h"

#include "ProjectJournal.h"
#include "ProvenanceSection.h"

#include "Engine.h"

namespace lmms
{

bool ControlRegistry::mayCoalesce(const QString& commandId, bool enclosedByWrapper, bool enclosedByMutating) const
{
	const ControlCommand* spec = command(commandId);
	return !enclosedByWrapper && !enclosedByMutating && (spec == nullptr || !spec->coalescesWrapped);
}

QString ControlRegistry::aggregateClassOf(const QStringList& childClasses)
{
	// Weakest first: a record of several children claims no more than its least reversible one.
	static const QStringList order{QStringLiteral("irreversible"), QStringLiteral("snapshot"), QStringLiteral("true_inverse")};
	QString weakest;
	int weakestRank = order.size();
	for (const QString& cls : childClasses)
	{
		const int rank = order.indexOf(cls);
		if (rank < 0) { return QString(); }  // no row, or not a mutating class: nothing honest to claim
		if (rank < weakestRank)
		{
			weakestRank = rank;
			weakest = cls;
		}
	}
	return weakest;
}

void ControlRegistry::flushNestedRecords(const QString& wrapperId, const ControlResult& wrapper, quint64 step)
{
	QVector<NestedRecord> held;
	held.swap(m_nestedRecords);
	QStringList nestedIds;
	for (const NestedRecord& child : held) { nestedIds.append(child.command); }
	// A wrapper that declares coalescing joins its one nested call's run: a burst of its calls is one step.
	if (held.size() == 1 && step != 0 && m_commands.value(wrapperId).coalescesWrapped)
	{
		ProjectJournal* journal = Engine::projectJournal();
		if (journal != nullptr && coalesceStepOf(*journal, held[0].command, held[0].args, true))
		{
			extendForMergedStep(held[0], wrapper, nestedIds);
			return;
		}
	}
	if (held.size() == 1)
	{
		recordTransactionOf(held[0].command, &held[0].result, false, held[0].step);
		return;
	}
	recordAggregate(wrapperId, wrapper, step, held, nestedIds);
}

void ControlRegistry::extendForMergedStep(const NestedRecord& child, const ControlResult& wrapper, const QStringList& nestedIds)
{
	if (m_transactions.isEmpty())
	{
		ControlResult copy = child.result;
		recordTransactionOf(child.command, &copy, false, child.step);
		return;
	}
	extendTopTransaction();
	const QJsonObject recorded = child.result.result.value(QStringLiteral("__transaction")).toObject();
	Transaction stamped;
	stamped.command = child.command;
	stamped.reversible = recorded.value(QStringLiteral("reversible")).toBool(false);
	stampContract(child.command, &stamped);
	const bool reversible = stamped.reversible && child.step != 0;
	Transaction& top = m_transactions.last();
	const int previousBytes = top.bytes;
	// The run's record now covers the nested call too: its class is the weaker of the two, and it is not
	// reversible if the nested call is not (the record may not claim more than the step holds).
	top.cls = aggregateClassOf({top.cls, stamped.cls});
	if (!reversible)
	{
		top.reversible = false;
		top.mechanism = QStringLiteral("a nested command of a coalesced run is not reversible, so this step is not "
			"reported as reversible; ") + top.mechanism;
	}
	top.bytes = control::serialisedRecordBytes(top);
	m_recordedBytes += top.bytes - previousBytes;
	provenance::extendTopChange(wrapper.result, nestedIds);
}

void ControlRegistry::recordAggregate(const QString& wrapperId, const ControlResult& wrapper, quint64 step,
	const QVector<NestedRecord>& held, const QStringList& nestedIds)
{
	Transaction aggregate;
	aggregate.command = wrapperId;
	aggregate.step = step;
	aggregate.commands = held.size();
	QJsonArray children;
	QStringList classes;
	bool everyChildReversible = step != 0;
	for (const NestedRecord& child : held)
	{
		const QJsonObject recorded = child.result.result.value(QStringLiteral("__transaction")).toObject();
		Transaction stamped;
		stamped.command = child.command;
		stamped.reversible = recorded.value(QStringLiteral("reversible")).toBool(false);
		stampContract(child.command, &stamped);
		const bool reversible = stamped.reversible && child.step != 0;
		everyChildReversible = everyChildReversible && reversible;
		classes.append(stamped.cls);
		children.append(QJsonObject{{QStringLiteral("command"), child.command}, {QStringLiteral("class"), stamped.cls},
			{QStringLiteral("reversible"), reversible}, {QStringLiteral("before"), recorded.value(QStringLiteral("before"))},
			{QStringLiteral("inverse"), recorded.value(QStringLiteral("inverse"))}});
	}
	aggregate.inverse = QJsonObject{{QStringLiteral("applies"), QStringLiteral("journal")},
		{QStringLiteral("children"), children}};
	aggregate.reversible = everyChildReversible;
	// The class is the children's, never the wrapper's row: the wrapper is not_mutating, and this record
	// is the mutating work it ran. stampContract is not applied, for the same reason.
	aggregate.cls = aggregateClassOf(classes);
	aggregate.mechanism = everyChildReversible
		? QStringLiteral("one journal step covering the nested commands listed in inverse.children; control.undo reverts all of them")
		: QStringLiteral("one journal step covering the nested commands listed in inverse.children, not every one of them "
			"reversible; the step is not reported as reversible");
	recordTransaction(aggregate);
	provenance::recordChange(wrapperId, QJsonObject(), wrapper.result, nestedIds);
}

} // namespace lmms
