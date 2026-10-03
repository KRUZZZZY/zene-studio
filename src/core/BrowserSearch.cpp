/*
 * BrowserSearch.cpp - the browser's search expressions: OR, NOT, phrases and fields
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
#include "BrowserSearch.h"

#include <algorithm>

#include <QFileInfo>
#include <QRegularExpression>

#include "BrowserCatalog.h"

namespace lmms
{

namespace
{

const QStringList& fieldNames()
{
	static const QStringList names{QStringLiteral("name"), QStringLiteral("path"), QStringLiteral("tag"), QStringLiteral("kind"),
		QStringLiteral("ext")};
	return names;
}

//! The text split into words, a quoted phrase kept whole (its quotes dropped).
QStringList tokens(const QString& text)
{
	static const QRegularExpression word(QStringLiteral(R"((-?\w+:)?"[^"]*"|\S+)"));
	QStringList out;
	for (auto it = word.globalMatch(text); it.hasNext();) { out << it.next().captured(0); }
	return out;
}

bool anyContains(const QStringList& haystack, const QString& needle)
{
	for (const QString& text : haystack)
	{
		if (text.contains(needle)) { return true; }
	}
	return false;
}

//! Whether the item's @a field holds @a value (lowercased).
bool fieldHolds(const QString& field, const QString& value, const BrowserItem& item)
{
	if (field == QLatin1String("name")) { return item.name.toLower().contains(value); }
	if (field == QLatin1String("path")) { return item.path.toLower().contains(value); }
	if (field == QLatin1String("kind")) { return browserKindName(item.kind) == value; }
	if (field == QLatin1String("ext")) { return QFileInfo(item.path).suffix().toLower() == value; }
	for (const QString& tag : item.tags)  // tag:
	{
		if (tag.toLower().contains(value)) { return true; }
	}
	return false;
}

//! Whether word @a w is an operator or a field term (so the text is an expression).
bool isOperator(const QString& w)
{
	if (w == QLatin1String("OR") || w == QLatin1String("|") || w.startsWith(QLatin1Char('-')) || w.contains(QLatin1Char('"'))) { return true; }
	return w.contains(QLatin1Char(':')) && fieldNames().contains(w.section(QLatin1Char(':'), 0, 0));
}

} // namespace

BrowserSearch::BrowserSearch(const QString& text)
{
	const QStringList words = tokens(text.trimmed());
	m_expression = std::any_of(words.begin(), words.end(), isOperator);
	if (!m_expression)
	{
		// The rule before WF-003: the whole text is one substring.
		if (!text.trimmed().isEmpty()) { m_groups.push_back({Term{QString(), text.trimmed().toLower(), false}}); }
		return;
	}
	m_groups.emplace_back();
	for (const QString& w : words)
	{
		if (w == QLatin1String("OR") || w == QLatin1String("|"))
		{
			if (!m_groups.back().empty()) { m_groups.emplace_back(); }
			continue;
		}
		const Term term = termOf(w);
		if (!term.value.isEmpty()) { m_groups.back().push_back(term); }
	}
	if (m_groups.back().empty()) { m_groups.pop_back(); }
}

BrowserSearch::Term BrowserSearch::termOf(QString word)
{
	Term term;
	if (word.startsWith(QLatin1Char('-')) && word.size() > 1) { term.negate = true; word.remove(0, 1); }
	const QString field = word.section(QLatin1Char(':'), 0, 0);
	if (word.contains(QLatin1Char(':')) && fieldNames().contains(field)) { term.field = field; word = word.section(QLatin1Char(':'), 1); }
	word.remove(QLatin1Char('"'));
	term.value = word.toLower();
	return term;
}

bool BrowserSearch::termHolds(const Term& term, const BrowserItem& item, const QStringList& metadataText)
{
	const bool found = term.field.isEmpty()
		? anyContains(item.searchableText(), term.value) || anyContains(metadataText, term.value)
		: fieldHolds(term.field, term.value, item);
	return found != term.negate;
}

bool BrowserSearch::matches(const BrowserItem& item, const QStringList& metadataText) const
{
	if (m_groups.empty()) { return true; }
	for (const Group& group : m_groups)
	{
		bool all = true;
		for (const Term& term : group)
		{
			if (!termHolds(term, item, metadataText)) { all = false; break; }
		}
		if (all) { return true; }
	}
	return false;
}

} // namespace lmms
