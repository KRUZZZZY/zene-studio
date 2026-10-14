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

#include <algorithm>
#include <limits>

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

//! The bytes one character costs as it sits inside a serialised attribute:
//! five for each of the five entities QDom escapes (&amp; &lt; &gt; &quot;
//! &apos; - one character, five bytes), otherwise its own UTF-8 width.
int escapedCharBytes(const QChar character)
{
	const char16_t code = character.unicode();
	const bool escaped = code == u'&' || code == u'<' || code == u'>'
		|| code == u'"' || code == u'\'';
	if (escaped) { return 5; }
	return code < 0x80 ? 1 : QString(character).toUtf8().size();
}

//! The value's length as QDom ESCAPES it inside an attribute. Counting the
//! raw QString would undercount an ampersand by four bytes per occurrence.
int escapedAttributeBytes(const QString& value)
{
	int bytes = 0;
	for (const QChar character : value) { bytes += escapedCharBytes(character); }
	return bytes;
}

//! One serialised attribute as ` name="value"`: the leading space, the name,
//! the equals sign and the two quotes the writer emits around every value.
int attributeBytes(const char* name, const QString& value)
{
	return escapedAttributeBytes(value) + static_cast<int>(QLatin1String(name).size()) + 4;
}

//! The one quantity the byte bound counts, beside the count cap: the entry as
//! it is actually SERIALISED - the <z:change/> markup, every attribute name,
//! its quoting and its UTF-8 bytes, and no more (an absent optional attribute
//! is not written, so it is not counted). A serialised-size measurement like
//! control::serialisedRecordBytes, never an estimate.
int serialisedBytes(const provenance::Change& change)
{
	int bytes = static_cast<int>(QStringLiteral("<z:change/>").toUtf8().size());
	bytes += attributeBytes("seq", QString::number(change.seq));
	bytes += attributeBytes("at", change.at);
	bytes += attributeBytes("actor", change.actor);
	bytes += attributeBytes("instance", change.instance);
	bytes += attributeBytes("command", change.command);
	if (!change.target.isEmpty()) { bytes += attributeBytes("target", change.target); }
	if (!change.before.isEmpty()) { bytes += attributeBytes("before", change.before); }
	if (!change.after.isEmpty()) { bytes += attributeBytes("after", change.after); }
	if (!change.nested.isEmpty()) { bytes += attributeBytes("nested", change.nested); }
	return bytes;
}

//! The attributes writeTo puts on a <z:change>: the five it always writes,
//! then the three it writes only when they have a value. Anything else on the
//! element is an attribute this writer never emits.
const char* const s_supportedAttributes[] = {
	"seq", "at", "actor", "instance", "command", "target", "before", "after", "nested"};

//! The attributes whose absence means the entry is not one writeTo produced:
//! seq (checked separately, as a positive number), and the four it sets
//! unconditionally - `at`, `actor`, `instance`, `command`.
bool hasRequiredAttributes(const QDomElement& element)
{
	for (const char* required : {"at", "actor", "instance", "command"})
	{
		if (!element.hasAttribute(QLatin1String(required))) { return false; }
	}
	return true;
}

//! Every attribute the element carries is one this writer emits. An unknown
//! attribute would be silently DROPPED on the next save - the rewrite the
//! append-only rule forbids - so the entry is refused instead.
bool hasOnlySupportedAttributes(const QDomElement& element)
{
	const QDomNamedNodeMap attributes = element.attributes();
	for (int i = 0; i < attributes.count(); ++i)
	{
		const QByteArray name = attributes.item(i).nodeName().toLatin1();
		if (std::none_of(std::begin(s_supportedAttributes), std::end(s_supportedAttributes),
			[&name](const char* supported) { return name == supported; }))
		{
			return false;
		}
	}
	return true;
}

//! One <z:change> child as the loader accepts it: an element named exactly
//! `z:change`, carrying a positive decimal seq, carrying every attribute the
//! writer always writes and nothing it does not. Anything else is a refusal,
//! not a guess. Returns false for a child this writer could not have written.
bool parseChange(const QDomNode& node, provenance::Change& change, quint64& highest)
{
	if (!node.isElement() || node.nodeName() != QLatin1String("z:change")) { return false; }
	const QDomElement element = node.toElement();
	if (!hasRequiredAttributes(element) || !hasOnlySupportedAttributes(element)) { return false; }
	bool seqOk = false;
	const QString seqText = element.attribute(QStringLiteral("seq"));
	const quint64 seq = seqText.toULongLong(&seqOk);
	if (!seqOk || seq == 0 || seqText != QString::number(seq)) { return false; }
	change.seq = seq;
	change.at = element.attribute(QStringLiteral("at"));
	change.actor = element.attribute(QStringLiteral("actor"));
	change.instance = element.attribute(QStringLiteral("instance"));
	change.command = element.attribute(QStringLiteral("command"));
	change.target = element.attribute(QStringLiteral("target"));
	change.before = element.attribute(QStringLiteral("before"));
	change.after = element.attribute(QStringLiteral("after"));
	change.nested = element.attribute(QStringLiteral("nested"));
	highest = qMax(highest, seq);
	return true;
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

void Section::updateLastAfter(const QString& after, const QString& nested)
{
	if (m_changes.isEmpty() || (after.isEmpty() && nested.isEmpty())) { return; }
	Change& newest = m_changes.last();
	const int previous = newest.bytes;
	if (!after.isEmpty()) { newest.after = after; }
	if (!nested.isEmpty()) { newest.nested = newest.nested.isEmpty() ? nested : newest.nested + QLatin1Char(' ') + nested; }
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

namespace
{

//! Every child of a section, in order. Refused (false) on a foreign child, a bad entry, or an entry that does
//! not ascend in seq: those are not shapes this writer produces. A `nested` entry is refused unless the
//! section is v="2" (versionTwo), since a v="1" section never carries it.
bool parseEntries(const QDomElement& section, bool versionTwo, QList<Change>* parsed, quint64* highest)
{
	for (QDomNode node = section.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		Change change;
		if (!parseChange(node, change, *highest)) { return false; }
		if (!change.nested.isEmpty() && !versionTwo) { return false; }
		// STRICTLY ascending, the shape writeTo produces (it assigns
		// m_nextSeq++ and appends). An entry that does not ascend - or
		// repeats - was not written by this writer; claiming it would append
		// a further entry after the break and leave a file its own loader
		// refuses. Refused, so the caller preserves it verbatim.
		if (!parsed->isEmpty() && change.seq <= parsed->last().seq) { return false; }
		parsed->append(change);
	}
	return true;
}

} // namespace

bool Section::load(const QDomElement& section)
{
	const QString version = section.attribute(QStringLiteral("v"));
	// v="2" is the section that carries a change's optional `nested` ids (an aggregate's children).
	// writeTo sets v="2" only when some entry carries them, so a v="1" section never has `nested`.
	const bool versionTwo = version == QLatin1String("2");
	if (!version.isEmpty() && version != QLatin1String("1") && !versionTwo) { return false; }

	QList<Change> parsed;
	quint64 highest = 0;
	if (!parseEntries(section, versionTwo, &parsed, &highest)) { return false; }
	// Claim nothing (and preserve the element verbatim as unclaimed) when the
	// section is empty or any child is foreign: a write-back of a partial or
	// future-shaped section would be the rewrite the append-only rule forbids.
	if (parsed.isEmpty()) { return false; }
	// An exhausted sequence is refused, not wrapped: `highest + 1` at
	// UINT64_MAX is 0, and the next append would then write seq="0" - which
	// this same loader rejects as malformed (a seq of zero is refused above).
	// Claiming the section would leave it in memory unwritable.
	if (highest == std::numeric_limits<quint64>::max()) { return false; }

	clear();
	m_changes = parsed;
	for (Change& change : m_changes) { change.bytes = serialisedBytes(change); m_bytes += change.bytes; }
	m_nextSeq = highest + 1;
	trim();
	return true;
}

void recordChange(const QString& command, const QJsonObject& before, const QJsonObject& report,
	const QStringList& nested)
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
	change.nested = nested.join(QLatin1Char(' '));
	Section::instance().append(change);
}

void extendTopChange(const QJsonObject& report, const QStringList& nested)
{
	QJsonObject outcome = report;
	outcome.remove(QStringLiteral("__transaction"));
	Section::instance().updateLastAfter(outcome.isEmpty() ? QString() : digestJson(outcome), nested.join(QLatin1Char(' ')));
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

	QDomElement element = document.createElement(nodeName());
	// ONE spelling of the `z` binding: the same attribute and URI the
	// <z:index> writer sets (DocumentIndex.h). It is declared HERE, on the
	// section that carries the prefixed name, and NOT on the document root
	// (DEFECT-D4b).
	//
	// The root spelling was a per-save volatile. This section is written only
	// when a change has been recorded, and a save's own change is recorded
	// AFTER its write, so the FIRST save of a session whose journal is empty
	// wrote neither a section nor a root binding while the SECOND wrote both:
	// two consecutive saves of one UNCHANGED session differed in a root
	// ATTRIBUTE, which stripping the journal block cannot remove. The
	// save-canonical contract (docs/SAVE-CANONICAL-STABILITY.md) is "the same
	// bytes modulo the journal block, declaration included", and a document
	// whose only z-prefixed content is the journal still binds the prefix - a
	// local declaration is in scope for the name exactly as the root's was,
	// because the reader parses with namespace processing OFF
	// (include/ProvenanceSection.h) and matches `z:provenance` as a literal.
	element.setAttribute(documentIndexNamespaceAttribute(), documentIndexNamespaceUri());
	element.setAttribute(QStringLiteral("seq"), QString::number(section.lastSeq()));
	// v="2" only when an entry carries `nested`: a section without them is byte-for-byte the v="1" shape
	// that every build with the strict loader reads, and an older build refuses a v="2" section as a whole.
	bool anyNested = false;
	for (int i = 0; i < section.size(); ++i) { anyNested = anyNested || !section.at(i).nested.isEmpty(); }
	element.setAttribute(QStringLiteral("v"), anyNested ? QStringLiteral("2") : QStringLiteral("1"));
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
		if (!change.nested.isEmpty()) { child.setAttribute(QStringLiteral("nested"), change.nested); }
		element.appendChild(child);
	}
	content.appendChild(element);
	return true;
}

} // namespace provenance
} // namespace lmms
