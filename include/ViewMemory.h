/*
 * ViewMemory.h - memory locations and saved selections, kept with the project
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

#ifndef LMMS_VIEW_MEMORY_H
#define LMMS_VIEW_MEMORY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 W3 L13: named places in a project - memory locations (WF-098: a play position with the range and
 *  the arranger zoom that went with it) and saved selections (WF-106: a range and the clips that were
 *  selected). Each entry is a JSON object the location.* / selection.* commands write; saved as
 *  <view-memory> only when there is an entry (a project that never used it re-saves byte for byte; a
 *  project without the element loads with none). UI thread. */
class LMMS_EXPORT ViewMemory : public ProjectSection
{
public:
	enum class Kind
	{
		Location,
		Selection
	};

	static ViewMemory& instance();

	const QJsonArray& entries(Kind kind) const { return kind == Kind::Location ? m_locations : m_selections; }
	void setEntries(Kind kind, const QJsonArray& entries);
	//! The entry of @a kind named @a name, or an empty object.
	QJsonObject entry(Kind kind, const QString& name) const;

	QString elementName() const override;
	bool shouldPersist() const override;
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	ViewMemory() = default;
	QJsonArray m_locations;
	QJsonArray m_selections;
};

} // namespace lmms

#endif // LMMS_VIEW_MEMORY_H
