/*
 * Keymap.cpp - implementation of keymap class
 *
 * Copyright (c) 2020 Martin Pavelek <he29.HS/at/gmail.com>
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

#include "Keymap.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <QDomElement>

#include "Note.h"


namespace lmms
{


Keymap::Keymap() :
	m_description(tr("empty")),
	m_firstKey(0),
	m_lastKey(NumKeys - 1),
	m_middleKey(DefaultMiddleKey),
	m_baseKey(DefaultBaseKey),
	m_baseFreq(DefaultBaseFreq)
{
}


Keymap::Keymap(
	QString description,
	std::vector<int> newMap,
	int newFirst,
	int newLast,
	int newMiddle,
	int newBaseKey,
	float newBaseFreq
) :
	m_description(description),
	m_map(std::move(newMap)),
	m_firstKey(newFirst),
	m_lastKey(newLast),
	m_middleKey(newMiddle),
	m_baseKey(newBaseKey),
	m_baseFreq(newBaseFreq)
{
}


/**
 * \brief Return scale degree for a given key, based on current map and first/middle/last notes
 * \param MIDI key to be mapped
 * \return Scale degree defined by the mapping on success, -1 if key isn't mapped
 */
int Keymap::getDegree(int key) const
{
	if (key < m_firstKey || key > m_lastKey) {return -1;}
	if (m_map.empty()) {return key;}	// exception: empty mapping table means linear (1:1) mapping

	const std::int64_t keyOffset = static_cast<std::int64_t>(key) - m_middleKey;
	const std::int64_t mapSize = static_cast<std::int64_t>(m_map.size());
	const std::int64_t remainder = keyOffset % mapSize;
	const std::int64_t mapIndex = remainder >= 0 ? remainder : remainder + mapSize;
	return m_map[static_cast<std::size_t>(mapIndex)];
}


/**
 * \brief Return octave offset for a given key, based on current map and the middle note
 * \param MIDI key to be mapped
 * \return Octave offset defined by the mapping on success, 0 if key isn't mapped
 */
int Keymap::getOctave(int key) const
{
	// The keymap wraparound cannot cause an octave transition if a key isn't mapped or the map is empty → return 0
	if (m_map.empty() || getDegree(key) == -1) {return 0;}

	const std::int64_t keyOffset = static_cast<std::int64_t>(key) - m_middleKey;
	const std::int64_t mapSize = static_cast<std::int64_t>(m_map.size());
	std::int64_t octave;
	if (keyOffset >= 0)
	{
		octave = keyOffset / mapSize;
	}
	else
	{
		octave = (keyOffset + 1) / mapSize - 1;
	}
	return static_cast<int>(std::clamp(octave,
		static_cast<std::int64_t>(std::numeric_limits<int>::min()),
		static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}


QString Keymap::getDescription() const
{
	return m_description;
}


void Keymap::setDescription(QString description)
{
	m_description = description;
}


void Keymap::saveSettings(QDomDocument &document, QDomElement &element)
{
	element.setAttribute("description", m_description);

	element.setAttribute("first_key", m_firstKey);
	element.setAttribute("last_key", m_lastKey);
	element.setAttribute("middle_key", m_middleKey);
	element.setAttribute("base_key", m_baseKey);
	element.setAttribute("base_freq", m_baseFreq);

	for (int value : m_map)
	{
		QDomElement degree = document.createElement("degree");
		element.appendChild(degree);
		degree.setAttribute("value", value);
	}
}


void Keymap::loadSettings(const QDomElement &element)
{
	m_description = element.attribute("description");

	m_firstKey	= std::clamp(element.attribute("first_key").toInt(), 0, NumKeys - 1);
	m_lastKey	= std::clamp(element.attribute("last_key").toInt(), 0, NumKeys - 1);
	m_middleKey	= std::clamp(element.attribute("middle_key").toInt(), 0, NumKeys - 1);
	m_baseKey	= std::clamp(element.attribute("base_key").toInt(), 0, NumKeys - 1);
	m_baseFreq	= element.attribute("base_freq").toDouble();

	QDomNode node = element.firstChild();
	m_map.clear();

	while (!node.isNull())
	{
		m_map.push_back(node.toElement().attribute("value").toInt());
		node = node.nextSibling();
	}
}


} // namespace lmms
