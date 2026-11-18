/*
 * BrowserSearch.h - the browser's search expressions: OR, NOT, phrases and fields
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
#ifndef LMMS_BROWSER_SEARCH_H
#define LMMS_BROWSER_SEARCH_H

#include <QString>
#include <QStringList>
#include <vector>

#include "lmms_export.h"

namespace lmms
{

struct BrowserItem;

/*! 0.5.0 L11 (WF-003): what browser.query's 'text' can say beyond a plain substring.
 *  - words are ANDed: `kick 808` finds items with both;
 *  - `OR` (or `|`) between groups: `kick OR snare`;
 *  - a leading `-` excludes: `kick -acoustic`;
 *  - `"..."` is one phrase: `"bass drum"`;
 *  - a field narrows a word: `name:`, `path:`, `tag:`, `kind:` (audio / preset / project / midi /
 *    other), `ext:` (the extension); a word without a field matches the name, path, tags and - when
 *    the query probes - the file's own metadata.
 *  A text with none of these is matched exactly as before - one case-insensitive substring - so an
 *  existing caller sees no change. */
class LMMS_EXPORT BrowserSearch
{
public:
	explicit BrowserSearch(const QString& text);

	//! True when the text uses any of the operators above (else the old substring rule applies).
	bool isExpression() const { return m_expression; }
	bool isEmpty() const { return m_groups.empty(); }
	/*! True when a term excludes a word with no field (`kick -drum`, not
	 *  `-name:drum`). Such an exclusion is satisfied by the ABSENCE of the word,
	 *  so a name that does not contain it matches: only the item's own metadata
	 *  can rule it out, and a caller that decides from the cheap text alone is
	 *  reporting a match the expression itself excludes. */
	bool hasUnfieldedExclusion() const;
	//! Whether @a item matches; @a metadataText (lowercased) is the probed metadata, or empty.
	bool matches(const BrowserItem& item, const QStringList& metadataText = {}) const;

private:
	struct Term
	{
		QString field;  //!< empty: anywhere
		QString value;  //!< lowercased
		bool negate = false;
	};
	using Group = std::vector<Term>;  //!< every term must hold

	static bool termHolds(const Term& term, const BrowserItem& item, const QStringList& metadataText);
	//! One word of an expression: its '-', its field and its value.
	static Term termOf(QString word);
	//! Fills @a groups from @a words; drops a leading group and a trailing empty one.
	void parseExpression(const QStringList& words);

	std::vector<Group> m_groups;  //!< any group may hold
	bool m_expression = false;
};

} // namespace lmms

#endif // LMMS_BROWSER_SEARCH_H
