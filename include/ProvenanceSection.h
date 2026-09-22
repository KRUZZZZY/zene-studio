/*
 * ProvenanceSection.h - the append-only <z:provenance> document section
 *                       (SPEC-ARCH-4 1.9 Requirement 8, ARCH-4 slice S6).
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

#ifndef LMMS_PROVENANCE_SECTION_H
#define LMMS_PROVENANCE_SECTION_H

#include "lmms_export.h"

#include <QJsonObject>
#include <QList>
#include <QString>

class QDomDocument;
class QDomElement;

namespace lmms
{

class DataFile;

namespace provenance
{

//! The section's literal tag name, written and matched unprefixed-ly as-is:
//! the reader parses with namespace processing OFF (DocumentIndex.h states the
//! same for the index), so `z:provenance` is a plain name to QDom.
LMMS_EXPORT QString nodeName();

//! One recorded change - a <z:change> element's attributes. After a load every
//! field is the VERBATIM string the file carried; the writer never re-derives
//! one, which is what lets a second save append without rewriting the first.
struct Change
{
	quint64 seq = 0;      //!< the document's own chain, strictly ascending
	QString at;           //!< UTC instant, Qt ISO-8601 ("...Z")
	QString actor;        //!< "human" | "agent" - never a name (see AgentScope)
	QString instance;     //!< ProjectIds::writerInstance(): a per-process token
	QString command;      //!< the registry's own command id (SPEC A11 vocabulary)
	QString target;       //!< the object address the before-state names, if any
	QString before;       //!< "sha256:..." over the recorded before-state JSON
	QString after;        //!< "sha256:..." over the command's post-change report
	int bytes = 0;        //!< serialised size, the quantity the byte bound counts

	//! Content equality - what a save/load round trip must preserve. `bytes`
	//! is excluded on purpose: it is a measurement of the rest, not content.
	friend bool operator==(const Change& a, const Change& b)
	{
		return a.seq == b.seq && a.at == b.at && a.actor == b.actor
			&& a.instance == b.instance && a.command == b.command
			&& a.target == b.target && a.before == b.before && a.after == b.after;
	}
};

/*! The process-wide section: the document's change log, and the model both the
 *  writer (Song::saveProjectFile) and the reader (Song::restoreNamedSection)
 *  speak. DOCUMENT-scoped, not session-scoped: a load replaces whatever was
 *  held with the file's own entries (or with nothing when the file carries no
 *  section - reset on absence, the rule every named section rides), and a save
 *  re-emits every held entry before any appended in this session.
 *
 *  BOUNDED exactly as the in-memory journal is bounded (SPEC-ARCH-4 1.9 cites
 *  ProjectJournal.h for this): the journal's LIVE count cap and byte budget,
 *  FIFO eviction of the OLDEST entry, and at least one entry always kept even
 *  over budget - the same three properties control.transactions reports.
 *
 *  Additive: append() is reached only from recordTransactionOf's path (a
 *  successful MUTATING command), so a session that recorded nothing has an
 *  empty section and an empty section is never written.
 *
 *  UI-thread only - the same thread every control handler runs on
 *  (ControlRegistry.h's dispatch contract). Nothing here is touched by the
 *  audio thread. */
class LMMS_EXPORT Section
{
public:
	static Section& instance();

	/*! Adopt \a section's entries. Returns false - claiming NOTHING - unless
	 *  the section is exactly what this writer produces: version 1 (or absent)
	 *  and at least one well-formed <z:change> child. A future version, an
	 *  unknown child or a malformed entry makes the caller preserve the whole
	 *  element verbatim as an unclaimed section instead of rewriting it. */
	bool load(const QDomElement& section);
	//! Forget every entry (load's reset-on-absence half).
	void clear();

	bool isEmpty() const { return m_changes.isEmpty(); }
	int size() const { return m_changes.size(); }
	const Change& at(int index) const { return m_changes.at(index); }
	//! The highest seq assigned, 0 when nothing has been recorded.
	quint64 lastSeq() const { return m_nextSeq == 0 ? 0 : m_nextSeq - 1; }

	/*! Append \a change with the next seq and the byte measurement, then trim.
	 *  Public because recordChange() (the registry seam) fills a Change the
	 *  section itself sequences; no other caller exists. */
	void append(Change change);
	//! Refresh the newest entry's `after` digest (a coalesced run extends the
	//! change it started instead of appending a second one). Re-measures bytes.
	void updateLastAfter(const QString& after);

private:
	Section() = default;
	void trim();

	QList<Change> m_changes;
	qint64 m_bytes = 0;
	quint64 m_nextSeq = 1;
};

/*! The registry seam (SPEC-ARCH-4 1.9): called ONLY where a transaction has
 *  been recorded - a successful mutating command, exactly once per undo step.
 *  \a before is the command's recorded before-state; \a report is the
 *  handler's result payload (the post-change report - for state-shaped
 *  commands such as clip.move it IS the after-state), with the private
 *  __transaction key stripped before it is digested. Either may be empty, and
 *  an empty one writes no digest attribute rather than a digest of nothing. */
LMMS_EXPORT void recordChange(const QString& command, const QJsonObject& before,
	const QJsonObject& report);
//! A coalesced run's own report refreshes the newest entry's after-digest.
LMMS_EXPORT void extendTopChange(const QJsonObject& report);

/*! Attribute changes made while a control-socket client is being dispatched to
 *  "agent" for the scope's duration (the socket IS the agent surface, SPEC
 *  A12); everything else records as "human". The actor is a session KIND, not
 *  a person: no name, credential, path or address is ever derived from it
 *  (SPEC-ARCH-4 1.9: the file must stay shareable). Best-effort attribution -
 *  the UI thread serialises dispatches, so the window a scope covers is the
 *  invoke it wraps. */
class LMMS_EXPORT AgentScope
{
public:
	AgentScope();
	~AgentScope();

	AgentScope(const AgentScope&) = delete;
	AgentScope& operator=(const AgentScope&) = delete;

private:
	int m_previous;
};

/*! Write the section into \a file's <song>: re-emitting every held entry and
 *  binding xmlns:z to the one URI the <z:index> writer binds (DocumentIndex's
 *  measured spelling, so the two cannot drift). Writes NOTHING - and returns
 *  false - when no change has been recorded (the additive rule). */
LMMS_EXPORT bool writeTo(DataFile& file);

} // namespace provenance
} // namespace lmms

#endif // LMMS_PROVENANCE_SECTION_H
