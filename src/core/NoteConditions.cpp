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
#include <cstdint>
#include <vector>

#include <QHash>
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
	bool validRecurMask = false;
	const int recurMaskValue = note.attribute(QStringLiteral("recm")).toInt(&validRecurMask);
	if (recurLength > 0 && validRecurMask)
	{
		recurMask = std::clamp(recurMaskValue, 0, (1 << recurLength) - 1);
	}
	else
	{
		recurLength = 0;
		recurMask = 0;
	}
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

QHash<QString, int>& lyricIndex()
{
	static QHash<QString, int> index;
	return index;
}

#ifdef LMMS_TESTING
std::uint64_t internLyricLookupWork = 0;
#endif

} // namespace

#ifdef LMMS_TESTING
LMMS_EXPORT void resetInternLyricLookupWorkForTest() noexcept
{
	internLyricLookupWork = 0;
}

LMMS_EXPORT std::uint64_t internLyricLookupWorkForTest() noexcept
{
	return internLyricLookupWork;
}
#endif

int internLyric(const QString& text)
{
	if (text.isEmpty()) { return 0; }
	QHash<QString, int>& index = lyricIndex();
#ifdef LMMS_TESTING
	++internLyricLookupWork;
#endif
	const auto found = index.constFind(text);
	if (found != index.cend()) { return found.value(); }
	std::vector<QString>& table = lyricTable();
	const int id = static_cast<int>(table.size());
	table.push_back(text);
	index.insert(text, id);
	return id;
}

QString lyricText(int id)
{
	const std::vector<QString>& table = lyricTable();
	return id > 0 && id < static_cast<int>(table.size()) ? table[static_cast<std::size_t>(id)] : QString();
}

} // namespace lmms
