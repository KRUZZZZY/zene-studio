/*
 * ControlTransactions.cpp - the transaction record the control registry keeps
 *                          for each undo step (SPEC A16 deliverable 2), and the
 *                          COALESCING of that record with the run it belongs to.
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

/*
 * ONE RECORD PER UNDO STEP. The record is bounded TWO ways - a count cap and a
 * total byte cap - and evicted FIFO, so the newest record (the one control.undo
 * reads) is never the one dropped. A record whose journal step a bound has
 * evicted is REFUSED by control.undo rather than unwound, which is what the
 * `step` serial on the record is for; see ControlCommandsControl.cpp.
 *
 * The record's own accounting lives here, next to the coalescing that changes it,
 * rather than in ControlRegistry.cpp - which is at the file-length ratchet's
 * limit and answers a different question ("how does a command get dispatched?").
 * These are still ControlRegistry's own member functions.
 */

#include <QJsonDocument>

#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{

int control::serialisedRecordBytes(const ControlRegistry::Transaction& tx)
{
	// The two JSON halves plus the mechanism text plus the command id: exactly
	// what a record occupies on the wire, and exactly what MaxTransactionBytes
	// bounds. Called from the append path AND from a coalesced run's
	// re-measurement, so the two cannot drift.
	return QJsonDocument(tx.before).toJson(QJsonDocument::Compact).size()
		+ QJsonDocument(tx.inverse).toJson(QJsonDocument::Compact).size()
		+ static_cast<int>(tx.mechanism.toUtf8().size())
		+ static_cast<int>(tx.command.toUtf8().size());
}

void ControlRegistry::trimTransactions()
{
	// FIFO on both caps, and the newest record is never dropped (`size() > 1`
	// in the byte branch): a client must always be able to ask what the last
	// command was. What was dropped is counted and reported, never hidden.
	while (m_transactions.size() > control::MaxTransactionRecords
		|| (m_recordedBytes > control::MaxTransactionBytes && m_transactions.size() > 1))
	{
		m_recordedBytes -= m_transactions.first().bytes;
		m_transactions.remove(0);
		++m_evicted;
	}
}

void ControlRegistry::extendTopTransaction()
{
	if (m_transactions.isEmpty()) { return; }

	// ONE RECORD PER UNDO STEP, exactly as there is one journal step: a
	// coalesced run extends the record it started rather than appending a second
	// one, so a client never sees a record for a step that no longer exists
	// separately. Everything the record says stays true: `before` is still the
	// pre-gesture state and `inverse` still reverts it - that is what coalescing
	// means - so only the command count and the byte accounting change.
	Transaction& top = m_transactions.last();
	const int previousBytes = top.bytes;
	++top.commands;
	top.bytes = control::serialisedRecordBytes(top);
	m_recordedBytes += top.bytes - previousBytes;
	trimTransactions();
}


void ControlRegistry::recordTransaction(const Transaction& tx)
{
	// The record is bounded TWO ways (SPEC A16 deliverable 2):
	//   - a hard count cap of MaxTransactionRecords records;
	//   - a hard total cap of MaxTransactionBytes of serialised before-state
	//     plus inverse descriptor.
	// Eviction policy: FIFO - the OLDEST record is dropped first, so the most
	// recent history is never the part that is lost, and the newest record
	// (the one control.undo reads) is always present. What happens at the cap
	// is reported, not hidden: `control.transactions` returns `evicted`,
	// `capped` and the retained/limit byte counts. The engine's own undo stack
	// evicts at the same depth (ProjectJournal::MAX_UNDO_STATES = 100), so an
	// agent never sees a record for a step it can no longer undo.
	Transaction stamped = tx;
	stamped.bytes = control::serialisedRecordBytes(stamped);

	m_transactions.append(stamped);
	m_recordedBytes += stamped.bytes;
	trimTransactions();
}


const ControlRegistry::Transaction* ControlRegistry::lastTransaction() const
{
	return m_transactions.isEmpty() ? nullptr : &m_transactions.last();
}


QJsonArray ControlRegistry::transactions() const
{
	QJsonArray out;
	for (const Transaction& tx : m_transactions)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("command"), tx.command);
		entry.insert(QStringLiteral("class"), tx.cls);
		entry.insert(QStringLiteral("before"), tx.before);
		entry.insert(QStringLiteral("inverse"), tx.inverse);
		entry.insert(QStringLiteral("reversible"), tx.reversible);
		entry.insert(QStringLiteral("mechanism"), tx.mechanism);
		entry.insert(QStringLiteral("bytes"), tx.bytes);
		// How many COMMANDS this one undo step covers: 1 for a normal command,
		// more when a same-command-same-target run was coalesced into it.
		entry.insert(QStringLiteral("commands"), tx.commands);
		// WHICH undo step this record describes (0 when its inverse is a command
		// rather than a step). control.undo compares it against the stack's
		// oldest retained serial: a record whose step a bound has evicted is
		// refused, and this field is what that refusal is about.
		entry.insert(QStringLiteral("step"), static_cast<double>(tx.step));
		out.append(entry);
	}
	return out;
}


QJsonObject ControlRegistry::transactionsReport() const
{
	QJsonObject out;
	out.insert(QStringLiteral("transactions"), transactions());
	out.insert(QStringLiteral("count"), m_transactions.size());
	out.insert(QStringLiteral("retained_bytes"), m_recordedBytes);
	out.insert(QStringLiteral("cap_records"), control::MaxTransactionRecords);
	out.insert(QStringLiteral("cap_bytes"), control::MaxTransactionBytes);
	out.insert(QStringLiteral("evicted"), m_evicted);
	// `capped` says out loud that older records were dropped, so a client can
	// tell "this is the whole history" from "this is what the bound retains".
	out.insert(QStringLiteral("capped"), m_evicted > 0);
	return out;
}


void ControlRegistry::clearTransactions()
{
	m_transactions.clear();
	m_recordedBytes = 0;
	m_evicted = 0;
}
namespace control
{

ControlRegistry::Transaction makeTransaction(const QString& command, QJsonObject before,
	QJsonObject inverse, bool reversible, const QString& mechanism)
{
	ControlRegistry::Transaction tx;
	tx.command = command;
	tx.before = std::move(before);
	tx.inverse = std::move(inverse);
	tx.reversible = reversible;
	tx.mechanism = mechanism;
	return tx;
}

} // namespace control

} // namespace lmms
