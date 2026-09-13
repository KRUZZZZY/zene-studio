/*
 * ControlUndoCoalescing.cpp - the coalescing rule's implementation, and the two
 *                             ControlRegistry members that apply it to a
 *                             command's transaction record.
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
 * WHY THIS IS ITS OWN TRANSLATION UNIT
 *
 * ControlRegistry.cpp is already at the file-length ratchet's limit, and the two
 * members the coalescing rule needs there (coalesceStepOf and
 * extendTopTransaction) are this rule's, not the dispatcher's. They are still
 * ControlRegistry's own member functions - the class, its invariants and its
 * public API are unchanged by where the definitions live.
 */

#include "ControlUndoCoalescing.h"

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ProjectJournal.h"

namespace lmms
{
namespace control
{

bool UndoCoalescer::continues(const QString& key, quint64 previousSerial) const
{
	// Order matters only for cheapness: the string compare rejects every
	// unrelated command before the clock is consulted.
	if (m_windowMs <= 0 || key.isEmpty() || key != m_key) { return false; }
	if (m_topSerial == 0 || previousSerial != m_topSerial) { return false; }
	const qint64 since = sinceLastMs();
	return since >= 0 && since <= static_cast<qint64>(m_windowMs);
}

void UndoCoalescer::stepOpened(const QString& key, quint64 serial)
{
	if (!m_clock.isValid()) { m_clock.start(); }
	m_key = key;
	m_topSerial = serial;
	m_atMs = m_clock.elapsed();
}

void UndoCoalescer::breakRun()
{
	m_key.clear();
	m_topSerial = 0;
}

bool UndoCoalescer::setWindowMs(int ms)
{
	if (ms < 0 || ms > MaxUndoCoalesceWindowMs) { return false; }
	m_windowMs = ms;
	// A window change is a boundary: whatever was in flight is not continued,
	// so "set the window to 0, then drag" cannot inherit the previous drag.
	breakRun();
	return true;
}

qint64 UndoCoalescer::sinceLastMs() const
{
	if (m_atMs < 0 || !m_clock.isValid()) { return -1; }
	return m_clock.elapsed() - m_atMs;
}

QStringList coalescingCommands()
{
	QStringList out;
	for (const ReversibilityEntry& entry : ReversibilityTable::instance().entries())
	{
		if (entry.coalesces()) { out.append(entry.command); }
	}
	out.sort();
	return out;
}

QString coalescingKey(const QString& commandId, const QJsonObject& args)
{
	const ReversibilityEntry* entry = ReversibilityTable::instance().lookup(commandId);
	if (entry == nullptr || !entry->coalesces()) { return QString(); }

	QString key = commandId;
	// The declaration is "clip", or "target,plugin,name,index" for a parameter:
	// the VALUES of those arguments, in the declared order, name the target. An
	// argument a call did not supply contributes an empty field, which keeps the
	// field positions stable ("ch-1::0" and "ch-1::1" are two parameters).
	for (const QString& raw : entry->coalesceTarget.split(QLatin1Char(',')))
	{
		const QString name = raw.trimmed();
		if (name.isEmpty()) { continue; }
		key += QLatin1Char(':');
		const QJsonValue value = args.value(name);
		key += value.isString() ? value.toString() : QString::number(value.toDouble());
	}
	return key;
}

QString coalescingRuleText()
{
	return QStringLiteral("two consecutive calls of the same command on the same target, with no other "
		"undo step pushed in between and less than the coalescing window apart, are ONE undo step; "
		"another command, another target, another step in between, a pause longer than the window, or "
		"control.set_undo_depth/control.set_undo_coalescing each open a new one");
}

bool coalesceOrOpenStep(UndoCoalescer& coalescer, ProjectJournal& journal,
	const QString& commandId, const QJsonObject& args, bool producedStep)
{
	// A call that produced NO step - a read, a refusal, a command whose inverse
	// is a command rather than a step - leaves the run ALONE: the rule is "no
	// other undo step pushed in between", and a client that reads state between
	// two moves of the same thing is still making one gesture. (The window's own
	// clock still ends a run that has gone quiet.)
	if (!producedStep) { return false; }

	const QString key = coalescingKey(commandId, args);
	const quint64 previous = journal.previousStepSerial();
	if (coalescer.continues(key, previous) && journal.coalesceTopStepIntoPrevious())
	{
		// The survivor is the run's own step: the next call must see it directly
		// below its own. After the merge that step IS the top one.
		coalescer.stepOpened(key, journal.topStepSerial());
		return true;
	}
	coalescer.stepOpened(key, journal.topStepSerial());
	return false;
}

} // namespace control

bool ControlRegistry::coalesceStepOf(ProjectJournal& journal, const QString& commandId,
	const QJsonObject& args, bool producedStep)
{
	return coalesceOrOpenStep(m_coalescer, journal, commandId, args, producedStep);
}

} // namespace lmms
