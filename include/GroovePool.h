/*
 * GroovePool.h - the project's named grooves: the collection the groove.*
 *                command group reads and edits
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
 *
 */

#ifndef LMMS_GROOVE_POOL_H
#define LMMS_GROOVE_POOL_H

#include <vector>

#include <QString>

#include "GrooveTemplate.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

/*! The project's groove pool: named GrooveTemplates, in insertion order.
 *
 *  THE NAME IS THE KEY. A template is found, replaced and addressed by its
 *  name, so "extract the feel of this clip and call it 'shuffle'" twice leaves
 *  ONE groove called shuffle - the second read replaces the first. That is what
 *  makes a pool a pool rather than an append-only log, and it is why the
 *  control surface has no "extract as a new name" variant.
 *
 *  WHERE IT LIVES, AND WHY THAT DECIDES ITS REVERSIBILITY. The pool is project
 *  state on the Song, serialised as ONE `<groove-pool>` element inside `<song>`
 *  and written ONLY when the pool is non-empty, so a project that never used a
 *  groove re-saves byte for byte as before (the same rule
 *  TempoMap::shouldPersist and ModulationLayer::shouldPersist follow). It is
 *  NOT inside the track container, and a Song journal checkpoint carries the
 *  track container and not this - the finding docs/TEMPO-MAP.md and
 *  docs/MODULATION.md both record. So the inverse of a pool edit is a recorded
 *  ACTION checkpoint that writes the captured element back
 *  (control::addUndoStep), never a claimed Song checkpoint.
 *
 *  BOUNDS, stated so nothing here is implicit:
 *    - at most MaxTemplates templates (a new name past the bound is refused by
 *      the command, and the bound is reported on the wire);
 *    - each template at most GrooveTemplate::MaxSteps slots, each step two
 *      bounded integers, so one template serialises to a few KB at most;
 *    - the whole pool therefore stays well inside the SPEC A16 record cap
 *      (control::MaxTransactionBytes, 256 KiB) even at the worst case, which is
 *      what lets a pool edit record its before-state in full.
 *
 *  NOTHING here is called from the audio thread; the pool is edited by commands
 *  and read by commands.
 */
class GroovePool
{
public:
	//! The most templates one project may hold. Past this a new name is
	//! refused; replacing an existing name is always allowed.
	static constexpr int MaxTemplates = 32;

	//! True when the pool holds no template at all.
	bool empty() const { return m_templates.empty(); }
	int size() const { return static_cast<int>(m_templates.size()); }

	/*! Whether the project file needs a <groove-pool> element. False for an
	 *  empty pool, which is what keeps a project that never used a groove
	 *  byte-identical to how it saved before this feature existed. */
	bool shouldPersist() const { return !m_templates.empty(); }

	//! Drops every template. Called by Song::clearProject, so a new project
	//! never inherits the previous one's grooves.
	void clear() { m_templates.clear(); }

	//! Index of the template called \a name, or -1.
	int indexOf(const QString& name) const;
	//! The template called \a name, or nullptr.
	const GrooveTemplate* find(const QString& name) const;
	//! The template at \a index; the caller has proved the index.
	const GrooveTemplate& at(int index) const { return m_templates[static_cast<std::size_t>(index)]; }

	/*! Stores \a groove under its own name: replaces the template of that name
	 *  in place, or appends it.
	 *
	 *  False - and nothing changes - when the name is new and the pool is
	 *  already at MaxTemplates, or when \a groove is not valid().
	 *  \a replaced receives whether an existing template was overwritten.
	 */
	bool set(const GrooveTemplate& groove, bool* replaced = nullptr);

	//! Removes the template called \a name. False when there is none.
	bool remove(const QString& name);

	/*! Renames \a from to \a to, keeping the template's position.
	 *
	 *  False - and nothing changes - when \a from is absent, when \a to is
	 *  already taken by a DIFFERENT template, or when \a to is not a writable
	 *  name. Renaming a template to its own name succeeds and changes nothing.
	 */
	bool rename(const QString& from, const QString& to);

	//! Appends the whole pool as one <groove-pool> element.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;

	/*! Reads a <groove-pool> element.
	 *
	 *  The pool is CLEARED FIRST, unconditionally. That is the load-bearing
	 *  part: the element is written only when the pool is non-empty, so the
	 *  state a restore must be able to reach is "no pool at all", and an
	 *  absent or empty element is exactly that state. A reader that only
	 *  added would leave a restored project holding the previous project's
	 *  grooves (the trap docs/UNDO-BOUNDS.md records for the warp map and the
	 *  comp lanes).
	 *
	 *  False when \a element is not a groove pool at all; the pool is still
	 *  empty in that case, which is the same state an absent element leaves.
	 */
	bool loadSettings(const QDomElement& element);

	/*! The pool as a standalone XML document, for the SPEC A16 before-state of
	 *  a recorded action checkpoint and for the command group's read-back. */
	QString toXml() const;
	//! Restores the pool from toXml()'s output. False - pool empty - when the
	//! text does not parse or is not a groove pool.
	bool fromXml(const QString& xml);

private:
	//! Builds the pool's own <groove-pool> element in \a doc, NOT yet inserted,
	//! so the writer can append it to the song and toXml() can make it a
	//! document root (one builder, two homes - the element cannot diverge).
	QDomElement toElement(QDomDocument& doc) const;

	std::vector<GrooveTemplate> m_templates;
};

} // namespace lmms

#endif // LMMS_GROOVE_POOL_H
