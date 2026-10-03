/*
 * NoteConditions.cpp - a note's play conditions and annotations, saved only when set
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
#include "NoteConditions.h"

#include <algorithm>
#include <vector>

#include <QDomElement>

namespace lmms
{

bool NoteConditions::isNeutral() const
{
	return *this == NoteConditions{};
}

bool NoteConditions::operator==(const NoteConditions& other) const
{
	return repeats == other.repeats && recurLength == other.recurLength && recurMask == other.recurMask
		&& articulation == other.articulation && group == other.group && lyric == other.lyric;
}

void NoteConditions::save(QDomElement& note) const
{
	if (repeats > 0) { note.setAttribute(QStringLiteral("rep"), repeats); }
	if (recurLength > 0)
	{
		note.setAttribute(QStringLiteral("recl"), recurLength);
		note.setAttribute(QStringLiteral("recm"), recurMask);
	}
	if (articulation > 0) { note.setAttribute(QStringLiteral("art"), articulation); }
	if (group > 0) { note.setAttribute(QStringLiteral("grp"), group); }
	if (lyric > 0) { note.setAttribute(QStringLiteral("lyr"), lyricText(lyric)); }
}

void NoteConditions::load(const QDomElement& note)
{
	const auto number = [&note](const char* name, int high) {
		return std::clamp(note.attribute(QLatin1String(name), QStringLiteral("0")).toInt(), 0, high);
	};
	repeats = number("rep", MaxRepeats);
	recurLength = number("recl", MaxRecurLength);
	recurMask = recurLength > 0 ? number("recm", (1 << recurLength) - 1) : 0;
	articulation = number("art", MaxArticulations);
	group = number("grp", 1 << 30);
	lyric = internLyric(note.attribute(QStringLiteral("lyr")));
}

namespace
{

std::vector<QString>& lyricTable()
{
	static std::vector<QString> table{QString()};
	return table;
}

} // namespace

int internLyric(const QString& text)
{
	if (text.isEmpty()) { return 0; }
	std::vector<QString>& table = lyricTable();
	const auto found = std::find(table.begin(), table.end(), text);
	if (found != table.end()) { return static_cast<int>(found - table.begin()); }
	table.push_back(text);
	return static_cast<int>(table.size()) - 1;
}

QString lyricText(int id)
{
	const std::vector<QString>& table = lyricTable();
	return id > 0 && id < static_cast<int>(table.size()) ? table[static_cast<std::size_t>(id)] : QString();
}

} // namespace lmms
