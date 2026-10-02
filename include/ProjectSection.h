/*
 * ProjectSection.h - a project-level element owned outside Song, saved and loaded with the project
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

#ifndef LMMS_PROJECT_SECTION_H
#define LMMS_PROJECT_SECTION_H

#include <vector>

#include <QString>

#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

/*! A project-level element that lives outside Song - markers first (0.5.0 ED-048). It registers
 *  itself once, and Song then writes it beside the tempo map and the groove pool (only when it
 *  holds something, so a project that never used it re-saves byte for byte), reads it back by its
 *  element name, and clears it whenever the project is cleared (the reset-on-absence rule: a
 *  project without the element loads into an empty one). Song's own long-standing sections keep
 *  their branches in SongSections.cpp; a NEW section is a subclass, not another branch. */
class LMMS_EXPORT ProjectSection
{
public:
	virtual ~ProjectSection() = default;

	virtual QString elementName() const = 0;
	virtual bool shouldPersist() const = 0;
	virtual void save(QDomDocument& doc, QDomElement& parent) const = 0;
	virtual void load(const QDomElement& element) = 0;
	virtual void clear() = 0;

	//! Add @a section (owned by its caller, alive for the process) to the registry; idempotent.
	static void registerSection(ProjectSection* section);
	static const std::vector<ProjectSection*>& sections();
	//! Song::clearProject's hook: every registered section back to empty.
	static void clearAll();
};

} // namespace lmms

#endif // LMMS_PROJECT_SECTION_H
