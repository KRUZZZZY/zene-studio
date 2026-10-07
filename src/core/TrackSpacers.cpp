/*
 * TrackSpacers.cpp - visual gaps below tracks in the track list (WF-107)
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

#include "TrackSpacers.h"

#include <algorithm>

#include <QDomElement>

#include "Engine.h"
#include "Song.h"

namespace lmms
{

TrackSpacers& TrackSpacers::instance()
{
	static TrackSpacers* spacers = [] {
		auto* made = new TrackSpacers();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *spacers;
}

void TrackSpacers::setSpacer(int trackId, const Spacer& spacer)
{
	Spacer clamped{std::clamp(spacer.height, 0, MaxHeight), spacer.label.left(MaxLabelLength)};
	if (clamped.height == 0) { m_spacers.remove(trackId); }
	else { m_spacers.insert(trackId, clamped); }
	emit changed();
}

QHash<int, TrackSpacers::Spacer> TrackSpacers::live() const
{
	QHash<int, Spacer> out;
	Song* song = Engine::getSong();
	for (auto it = m_spacers.cbegin(); it != m_spacers.cend(); ++it)
	{
		if (song != nullptr && song->findTrackById(it.key()) != nullptr) { out.insert(it.key(), it.value()); }
	}
	return out;
}

void TrackSpacers::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	const QHash<int, Spacer> spacers = live();
	QList<int> ids = spacers.keys();
	std::sort(ids.begin(), ids.end());  // the same spacers always write the same bytes
	for (const int id : ids)
	{
		QDomElement one = doc.createElement(QStringLiteral("spacer"));
		one.setAttribute(QStringLiteral("track"), id);
		one.setAttribute(QStringLiteral("height"), spacers.value(id).height);
		if (!spacers.value(id).label.isEmpty()) { one.setAttribute(QStringLiteral("label"), spacers.value(id).label); }
		element.appendChild(one);
	}
	parent.appendChild(element);
}

void TrackSpacers::load(const QDomElement& element)
{
	m_spacers.clear();
	for (QDomElement one = element.firstChildElement(QStringLiteral("spacer")); !one.isNull();
		one = one.nextSiblingElement(QStringLiteral("spacer")))
	{
		bool idOk = false;
		const int trackId = one.attribute(QStringLiteral("track")).toInt(&idOk);
		const int height = std::clamp(one.attribute(QStringLiteral("height")).toInt(), 0, MaxHeight);
		if (idOk && height > 0)
		{
			const QString label = one.attribute(QStringLiteral("label")).left(MaxLabelLength);
			m_spacers.insert(trackId, {height, label});
		}
	}
	emit changed();
}

void TrackSpacers::clear()
{
	if (m_spacers.isEmpty()) { return; }
	m_spacers.clear();
	emit changed();
}

} // namespace lmms
