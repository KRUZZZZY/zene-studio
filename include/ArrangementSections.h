/*
 * ArrangementSections.h - the song's named sections (verse, chorus) as spans of time
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
#ifndef LMMS_ARRANGEMENT_SECTIONS_H
#define LMMS_ARRANGEMENT_SECTIONS_H

#include <QString>
#include <QVector>

#include "LmmsTypes.h"
#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

//! One section: a stable id ("sec-<id>" on the wire), its span [start, end), a name and a colour.
struct ArrangementSection
{
	int id = 0;
	tick_t start = 0;
	tick_t end = 0;
	QString name;
	QString color;  //!< "#rrggbb", or empty
};

/*! 0.5.0 L04 (ED-047): the song's sections - spans with a name, drawn as a lane over the arranger and
 *  selected as a range. Saved as <sections> only when there is one (an absent element loads as
 *  none); UI-thread state edited through the section.* commands, one undo step each. */
class LMMS_EXPORT ArrangementSections : public ProjectSection
{
public:
	static constexpr int MaxSections = 256;

	static ArrangementSections& instance();

	//! Every section, ordered by start.
	const QVector<ArrangementSection>& sections() const { return m_sections; }
	const ArrangementSection* find(int id) const;
	//! The section that holds @a tick, or nullptr.
	const ArrangementSection* at(tick_t tick) const;
	//! Add a section; its id, or -1 when full or the span is empty.
	int add(tick_t start, tick_t end, const QString& name, const QString& color);
	bool replace(int id, const ArrangementSection& section);
	bool remove(int id);
	void restore(const QVector<ArrangementSection>& sections);

	QString elementName() const override { return QStringLiteral("sections"); }
	bool shouldPersist() const override { return !m_sections.isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	ArrangementSections() = default;
	void sort();
	QVector<ArrangementSection> m_sections;
	int m_nextId = 1;
};

} // namespace lmms

#endif // LMMS_ARRANGEMENT_SECTIONS_H
