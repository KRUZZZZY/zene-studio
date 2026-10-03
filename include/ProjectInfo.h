/*
 * ProjectInfo.h - a project's metadata and its document type
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

#ifndef LMMS_PROJECT_INFO_H
#define LMMS_PROJECT_INFO_H

#include <QHash>
#include <QString>
#include <QStringList>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L12: what a project says about itself.
 *  - WF-047: metadata fields - title, artist, album, genre, year, composer, copyright and a comment;
 *  - WF-048: the document type - production (the default), mastering or live - which the interface
 *    reads to open the view that kind of work starts in.
 *  Saved as <project-info> with one attribute per field that is set (and `type` only when it is not
 *  production), so a project that never set any re-saves byte for byte; an absent element loads as
 *  empty. UI-thread state, written through project.metadata / project.settings. */
class LMMS_EXPORT ProjectInfo : public ProjectSection
{
public:
	static constexpr int MaxFieldLength = 4096;

	static ProjectInfo& instance();
	//! The metadata field names, in the order they are shown.
	static const QStringList& fieldNames();
	//! The document types; the first is the default.
	static const QStringList& documentTypes();

	QString field(const QString& name) const { return m_fields.value(name); }
	//! Set (or with an empty value, clear) one field; false for a name that is not a field.
	bool setField(const QString& name, const QString& value);
	const QHash<QString, QString>& fields() const { return m_fields; }
	void setFields(const QHash<QString, QString>& fields) { m_fields = fields; }

	QString documentType() const { return m_type; }
	//! False (and unchanged) for a name that is not a document type.
	bool setDocumentType(const QString& type);

	QString elementName() const override { return QStringLiteral("project-info"); }
	bool shouldPersist() const override { return !m_fields.isEmpty() || m_type != documentTypes().first(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	ProjectInfo();
	QHash<QString, QString> m_fields;
	QString m_type;
};

} // namespace lmms

#endif // LMMS_PROJECT_INFO_H
