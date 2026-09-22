/*
 * ProvenanceSection.cpp - the append-only <z:provenance> document section
 *                         (SPEC-ARCH-4 1.9 Requirement 8, ARCH-4 slice S6).
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/*
 * THE MEASURED SEAM (SPEC-ARCH-4 1.9, re-verified 2026-09-22 against the
 * 0.4.0 train tip): ControlRegistry::Transaction carries the command's
 * recorded BEFORE-STATE and its inverse (ControlRegistry.h:110-135) - there
 * is no pre-computed digest anywhere on that path (recordTransactionOf /
 * serialisedRecordBytes measure BYTES, not hashes) and no stored after-state.
 * This file therefore computes the two digests the spec's <z:change> carries:
 *   before = sha256 over the canonical compact JSON of the recorded
 *            before-state - recomputable by anyone holding the same record
 *            (control.transactions), which the registered test does;
 *   after  = sha256 over the command's post-change REPORT payload (the
 *            handler's result minus the private __transaction key). For
 *            state-shaped commands (clip.move, transport.punch_set, ...) the
 *            report IS the after-state; where a command reports something
 *            else, the digest commits to what the command said after
 *            mutating rather than to a state that was never recorded.
 */

#include "ProvenanceSection.h"

#include <QAtomicInteger>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDomDocument>
#include <QDomElement>
#include <QJsonDocument>

#include "DataFile.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "ProjectIds.h"
#include "ProjectJournal.h"

namespace lmms
{

namespace
{

QAtomicInteger<int> s_actor{0}; // 0 = human (default), 1 = agent
constexpr int ActorAgent = 1;

QString currentActor()
{
	return s_actor.loadRelaxed() == ActorAgent ? QStringLiteral("agent")
		: QStringLiteral("human");
}

//! "sha256:<hex>" over the compact JSON - the spelling sectionDigest()
//! already writes (DocumentIndex.cpp), so one digest form serves the file's
//! section digests and a change's state digests alike.
QString digestJson(const QJsonObject& object)
{
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	return QStringLiteral("sha256:")
		+ QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

//! True when \a value is one of the registry's object addresses - the ONE
//! grammar ProjectIds::familyPrefix() declares (trk-7, clip-7, ...): digits
//! after the family's prefix, nothing else. A track NAME that happens to look
//! like prose never matches; an address does.
bool isFamilyAddress(const QString& value)
{
	static const ProjectIds::IdFamily families[] = {
		ProjectIds::IdFamily::Track, ProjectIds::IdFamily::Clip,
		ProjectIds::IdFamily::Lane, ProjectIds::IdFamily::Note,
		ProjectIds::IdFamily::Scene, ProjectIds::IdFamily::Warp,
		ProjectIds::IdFamily::Channel, ProjectIds::IdFamily::Effect};
	for (const ProjectIds::IdFamily family : families)
	{
		const QString prefix = QString::fromLatin1(ProjectIds::familyPrefix(family));
		if (!value.startsWith(prefix)) { continue; }
		const QString tail = value.mid(prefix.size());
		if (tail.isEmpty()) { return false; }
		bool allDigits = true;
		for (const QChar digit : tail) { allDigits = allDigits && digit.isDigit(); }
		if (allDigits) { return true; }
	}
	return false;
}

//! The object the recorded before-state names, if it names one: the first
//! value in the state that is an address in the id grammar. A state that
//! names an aggregate (track.add's {track_count, tracks[]}) or nothing
//! (transport.punch_set's tick range) yields an EMPTY target - and an empty
//! target is written as an absent attribute, never as a placeholder.
QString targetFrom(const QJsonObject& before)
{
	for (auto it = before.constBegin(); it != before.constEnd(); ++it)
	{
		const QString value = it.value().toString();
		if (!value.isEmpty() && isFamilyAddress(value)) { return value; }
	}
	return QString();
}

//! The one quantity the byte bound counts, beside the count cap: every
//! attribute value plus the attribute names they ride on - a serialised-size
//! measurement like control::serialisedRecordBytes, never an estimate.
int serialisedBytes(const provenance::Change& change)
{
	int bytes = QString::number(change.seq).size();
	bytes += change.at.size() + change.actor.size() + change.instance.size();
	bytes += change.command.size() + change.target.size();
	bytes += change.before.size() + change.after.size();
	return bytes;
}

} // namespace

namespace provenance
{

QString nodeName()
{
	return QStringLiteral("z:provenance");
}

Section& Section::instance()
{
	static Section section;
	return section;
}

void Section::clear()
{
	m_changes.clear();
	m_bytes = 0;
	m_nextSeq = 1;
}

void Section::append(Change change)
{
	change.seq = m_nextSeq++;
	change.bytes = serialisedBytes(change);
	m_bytes += change.bytes;
	m_changes.append(change);
	trim();
}

void Section::updateLastAfter(const QString& after)
{
	if (m_changes.isEmpty() || after.isEmpty()) { return; }
	Change& newest = m_changes.last();
	const int previous = newest.bytes;
	newest.after = after;
	newest.bytes = serialisedBytes(newest);
	m_bytes += newest.bytes - previous;
	trim();
}

void Section::trim()
{
	// The journal's bound, by its own numbers (ProjectJournal.h: count cap AND
	// byte budget, oldest dropped first, newest never the one lost, and one
	// entry kept even if it alone is over budget - an audit log that silently
	// dropped the change just made is worse than one over budget).
	const ProjectJournal* journal = Engine::projectJournal();
	const int countCap = journal != nullptr ? journal->maxUndoStates()
		: ProjectJournal::MAX_UNDO_STATES;
	const qint64 byteCap = journal != nullptr ? journal->maxUndoBytes()
		: ProjectJournal::DefaultMaxUndoBytes;
	while (m_changes.size() > countCap || (m_bytes > byteCap && m_changes.size() > 1))
	{
		m_bytes -= m_changes.first().bytes;
		m_changes.removeFirst();
	}
}

bool Section::load(const QDomElement& section)
{
	const QString version = section.attribute(QStringLiteral("v"));
	if (!version.isEmpty() && version != QLatin1String("1")) { return false; }

	QList<Change> parsed;
	quint64 highest = 0;
	for (QDomNode node = section.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		if (!node.isElement() || node.nodeName() != QLatin1String("z:change")) { return false; }
		const QDomElement element = node.toElement();
		bool seqOk = false;
		const quint64 seq = element.attribute(QStringLiteral("seq")).toULongLong(&seqOk);
		if (!seqOk || seq == 0) { return false; }
		Change change;
		change.seq = seq;
		change.at = element.attribute(QStringLiteral("at"));
		change.actor = element.attribute(QStringLiteral("actor"));
		change.instance = element.attribute(QStringLiteral("instance"));
		change.command = element.attribute(QStringLiteral("command"));
		change.target = element.attribute(QStringLiteral("target"));
		change.before = element.attribute(QStringLiteral("before"));
		change.after = element.attribute(QStringLiteral("after"));
		highest = qMax(highest, seq);
		parsed.append(change);
	}
	// Claim nothing (and preserve the element verbatim as unclaimed) when the
	// section is empty or any child is foreign: a write-back of a partial or
	// future-shaped section would be the rewrite the append-only rule forbids.
	if (parsed.isEmpty()) { return false; }

	clear();
	m_changes = parsed;
	for (Change& change : m_changes) { change.bytes = serialisedBytes(change); m_bytes += change.bytes; }
	m_nextSeq = highest + 1;
	trim();
	return true;
}

void recordChange(const QString& command, const QJsonObject& before, const QJsonObject& report)
{
	QJsonObject outcome = report;
	outcome.remove(QStringLiteral("__transaction"));

	Change change;
	change.at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	change.actor = currentActor();
	// The instance token behind <head writer>: one UUID fragment per process,
	// never derived from a user, a host or a path - the shareability rule
	// (SPEC-ARCH-4 1.9) states it is not an identity, and it must cross-
	// reference the head stamp rather than compete with it.
	change.instance = ProjectIds::writerInstance();
	change.command = command;
	change.target = targetFrom(before);
	if (!before.isEmpty()) { change.before = digestJson(before); }
	if (!outcome.isEmpty()) { change.after = digestJson(outcome); }
	Section::instance().append(change);
}

void extendTopChange(const QJsonObject& report)
{
	QJsonObject outcome = report;
	outcome.remove(QStringLiteral("__transaction"));
	if (outcome.isEmpty()) { return; }
	Section::instance().updateLastAfter(digestJson(outcome));
}

AgentScope::AgentScope()
	: m_previous(s_actor.fetchAndStoreOrdered(ActorAgent))
{
}

AgentScope::~AgentScope()
{
	s_actor.storeRelaxed(m_previous);
}

bool writeTo(DataFile& file)
{
	Section& section = Section::instance();
	if (section.isEmpty()) { return false; }

	QDomElement& content = file.content();
	QDomDocument document = content.ownerDocument();
	// A QDom handle copy points at the same node, so setting through it is
	// setting on the document's root.
	QDomElement root = file.documentElement();
	// ONE spelling of the `z` binding: the same attribute and URI the
	// <z:index> writer sets (DocumentIndex.h). setAttribute on an attribute
	// the index later re-sets is idempotent, so a document carrying both
	// binds the prefix exactly once and its bytes do not depend on order.
	root.setAttribute(documentIndexNamespaceAttribute(), documentIndexNamespaceUri());

	QDomElement element = document.createElement(nodeName());
	element.setAttribute(QStringLiteral("seq"), QString::number(section.lastSeq()));
	element.setAttribute(QStringLiteral("v"), QStringLiteral("1"));
	for (int i = 0; i < section.size(); ++i)
	{
		const Change& change = section.at(i);
		QDomElement child = document.createElement(QStringLiteral("z:change"));
		child.setAttribute(QStringLiteral("seq"), QString::number(change.seq));
		child.setAttribute(QStringLiteral("at"), change.at);
		child.setAttribute(QStringLiteral("actor"), change.actor);
		child.setAttribute(QStringLiteral("instance"), change.instance);
		child.setAttribute(QStringLiteral("command"), change.command);
		if (!change.target.isEmpty()) { child.setAttribute(QStringLiteral("target"), change.target); }
		if (!change.before.isEmpty()) { child.setAttribute(QStringLiteral("before"), change.before); }
		if (!change.after.isEmpty()) { child.setAttribute(QStringLiteral("after"), change.after); }
		element.appendChild(child);
	}
	content.appendChild(element);
	return true;
}

} // namespace provenance
} // namespace lmms
