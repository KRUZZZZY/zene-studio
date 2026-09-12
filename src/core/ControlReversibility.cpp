/*
 * ControlReversibility.cpp - the machinery of the SPEC A16 reversibility
 *                            contract: the table lookup and the shared
 *                            undo-step helper. The table itself is data and
 *                            lives in ControlReversibilityTable.cpp.
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

#include "ControlReversibility.h"

#include <utility>

#include "Engine.h"
#include "JournallingObject.h"
#include "ProjectJournal.h"

namespace lmms
{
namespace control
{

QString reversibilityClassName(ReversibilityClass cls)
{
	switch (cls)
	{
		case ReversibilityClass::NotMutating: return QStringLiteral("not_mutating");
		case ReversibilityClass::TrueInverse: return QStringLiteral("true_inverse");
		case ReversibilityClass::Snapshot: return QStringLiteral("snapshot");
		case ReversibilityClass::Irreversible: return QStringLiteral("irreversible");
	}
	return QString();
}

ReversibilityClass reversibilityClassFromName(const QString& name)
{
	if (name == QLatin1String("true_inverse")) { return ReversibilityClass::TrueInverse; }
	if (name == QLatin1String("snapshot")) { return ReversibilityClass::Snapshot; }
	if (name == QLatin1String("irreversible")) { return ReversibilityClass::Irreversible; }
	return ReversibilityClass::NotMutating;
}

ReversibilityTable& ReversibilityTable::instance()
{
	static ReversibilityTable table;
	return table;
}

ReversibilityTable::ReversibilityTable() :
	m_entries()
{
	int rowCount = 0;
	const ReversibilityRow* rows = reversibilityRowTable(&rowCount);
	for (int i = 0; i < rowCount; ++i)
	{
		const ReversibilityRow& row = rows[i];
		ReversibilityEntry entry;
		entry.command = QString::fromUtf8(row.command);
		entry.cls = row.cls;
		entry.reason = QString::fromUtf8(row.reason);
		entry.mechanism = QString::fromUtf8(row.mechanism);
		entry.fallback = QString::fromUtf8(row.fallback);
		entry.reversible = row.reversible;
		m_entries.insert(entry.command, entry);
	}
}

const ReversibilityEntry* ReversibilityTable::lookup(const QString& command) const
{
	const auto it = m_entries.constFind(command);
	return it == m_entries.constEnd() ? nullptr : &it.value();
}

QStringList ReversibilityTable::commandIds() const
{
	QStringList ids = m_entries.keys();
	ids.sort();
	return ids;
}

QVector<ReversibilityEntry> ReversibilityTable::entries() const
{
	QVector<ReversibilityEntry> out;
	for (const QString& id : commandIds())
	{
		out.append(*lookup(id));
	}
	return out;
}

// ---------------------------------------------------------------------------
// the shared undo-step helpers
// ---------------------------------------------------------------------------

void addUndoStep(std::function<void()> undo, std::function<void()> redo)
{
	ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr || !undo) { return; }
	journal->addJournalAction(std::move(undo), std::move(redo));
}

void addUndoStep(const QVector<JournallingObject*>& journallingObjects)
{
	ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr) { return; }
	journal->addJournalCheckPoint(journallingObjects);
}

} // namespace control
} // namespace lmms
