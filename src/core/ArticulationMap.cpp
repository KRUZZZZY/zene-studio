/*
 * ArticulationMap.cpp - a track's articulations, as JSON and as one attribute of the track
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
#include "ArticulationMap.h"

#include <algorithm>

#include <QDebug>
#include <QDomElement>
#include <QJsonDocument>
#include <QJsonObject>

namespace lmms
{

const Articulation* ArticulationMap::at(int index) const
{
	return index >= 1 && index <= static_cast<int>(m_entries.size()) ? &m_entries[static_cast<std::size_t>(index - 1)] : nullptr;
}

int ArticulationMap::indexOf(const QString& name) const
{
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		if (m_entries[i].name.compare(name, Qt::CaseInsensitive) == 0) { return static_cast<int>(i) + 1; }
	}
	return 0;
}

QJsonArray ArticulationMap::toJson() const
{
	QJsonArray out;
	for (const Articulation& entry : m_entries)
	{
		QJsonObject item{{QStringLiteral("name"), entry.name}};
		if (entry.keyswitch >= 0) { item.insert(QStringLiteral("keyswitch"), entry.keyswitch); }
		if (entry.program >= 0) { item.insert(QStringLiteral("program"), entry.program); }
		if (entry.controller >= 0)
		{
			item.insert(QStringLiteral("controller"), entry.controller);
			item.insert(QStringLiteral("value"), entry.value);
		}
		out.append(item);
	}
	return out;
}

namespace
{

//! @a key of @a item as a MIDI number 0..127, -1 when absent; false when it is out of range.
bool midiNumber(const QJsonObject& item, const char* key, int* out)
{
	*out = item.value(QLatin1String(key)).toInt(-1);
	return !item.contains(QLatin1String(key)) || (*out >= 0 && *out <= 127);
}

} // namespace

bool ArticulationMap::fromJson(const QJsonArray& array, std::vector<Articulation>* out, QString* error)
{
	out->clear();
	for (int i = 0; i < array.size(); ++i)
	{
		const QJsonObject item = array.at(i).toObject();
		Articulation entry;
		entry.name = item.value(QStringLiteral("name")).toString().trimmed();
		const bool numbers = midiNumber(item, "keyswitch", &entry.keyswitch) && midiNumber(item, "program", &entry.program)
			&& midiNumber(item, "controller", &entry.controller) && midiNumber(item, "value", &entry.value);
		// A 'value' is the controller's: alone it would be dropped without a word (and gone on reload).
		const bool orphanValue = item.contains(QStringLiteral("value")) && entry.controller < 0;
		if (entry.name.isEmpty() || !numbers || orphanValue)
		{
			*error = QStringLiteral("articulation %1 needs a 'name', its keyswitch, program, controller and value are 0..127, "
				"and a 'value' needs a 'controller'").arg(i);
			return false;
		}
		entry.value = std::max(0, entry.value);
		out->push_back(entry);
	}
	return true;
}

void ArticulationMap::save(QDomElement& trackElement) const
{
	if (m_entries.empty()) { return; }
	trackElement.setAttribute(QStringLiteral("articulations"), QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact)));
}

void ArticulationMap::load(const QDomElement& trackElement)
{
	std::vector<Articulation> entries;
	QString why;
	if (!fromJson(QJsonDocument::fromJson(trackElement.attribute(QStringLiteral("articulations")).toUtf8()).array(), &entries, &why))
	{
		qWarning("ArticulationMap: the track's articulations could not be read (%s); it loads with none", qUtf8Printable(why));
	}
	m_entries = std::move(entries);
}

} // namespace lmms
