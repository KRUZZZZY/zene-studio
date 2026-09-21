/*
 * RevisionTimelineCompare.cpp - the STRUCTURAL comparison of two project
 *                              documents (feature-list row 76's `compare`).
 *
 * One pass over each document's text with the opening element tags counted by
 * name, and the two counts differenced. Deliberately NOT a semantic diff: the
 * musical diff of two project documents is tools/mmpz-git's (`mmpz-git diff`), a
 * Python tool outside this process, and re-implementing it here would be a second
 * implementation of a contract that already has one. Deliberately not an XML
 * parser either: a document this engine wrote is well-formed, and a caller
 * comparing a hand-edited file gets a count of what it can read rather than a
 * parse error - with `readable: false` when there is nothing to count at all.
 *
 * Its own translation unit because the timeline's file sits at this fork's
 * 500-line file ratchet and this half is the half that does not touch the disk.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "RevisionTimeline.h"

#include "ProjectContainer.h"

#include <algorithm>

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace lmms
{
namespace control
{

namespace
{

using Bounds = RevisionTimelineBounds;

bool isNameChar(QChar c)
{
	return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
		|| c == QLatin1Char('.') || c == QLatin1Char(':');
}

//! The index just past the '>' that closes the construct starting at \a open.
int indexPastTagEnd(const QString& text, int open)
{
	const int close = text.indexOf(QLatin1Char('>'), open);
	return (close < 0) ? text.size() : close + 1;
}

//! Counts the element one '<' construct holds, if any, and returns the index the
//! scan continues from. Split out of elementCounts() because the construct's
//! kinds (a close tag, a declaration, a comment, an element, a stray '<') are
//! one decision each and the ratchet's CCN budget is ten per function.
int countOneConstruct(const QString& text, int open, QHash<QString, int>* counts)
{
	const int nameStart = open + 1;
	if (nameStart >= text.size()) { return text.size(); }
	const QChar first = text.at(nameStart);
	if (first == QLatin1Char('/') || first == QLatin1Char('?') || first == QLatin1Char('!'))
	{
		// A close tag, a declaration or a comment: skipped whole. A comment
		// carries no element, and skipping to the next '>' is what keeps
		// "<note>" quoted inside one from counting as a note.
		return indexPastTagEnd(text, nameStart);
	}
	int end = nameStart;
	while (end < text.size() && isNameChar(text.at(end))) { ++end; }
	if (end == nameStart) { return nameStart; }
	(*counts)[text.mid(nameStart, end - nameStart)] += 1;
	return end;
}

//! Every OPENING element tag of \a document, counted by name.
QHash<QString, int> elementCounts(const QByteArray& document)
{
	QHash<QString, int> counts;
	const QString text = QString::fromUtf8(document);
	int index = text.indexOf(QLatin1Char('<'));
	while (index >= 0)
	{
		index = text.indexOf(QLatin1Char('<'), countOneConstruct(text, index, &counts));
	}
	return counts;
}

QJsonObject emptySide()
{
	QJsonObject out;
	out.insert(QStringLiteral("elements"), 0);
	out.insert(QStringLiteral("tags"), 0);
	return out;
}

QJsonObject sideTotals(const QHash<QString, int>& counts, int* elements)
{
	int total = 0;
	for (int count : counts) { total += count; }
	*elements = total;
	QJsonObject out;
	out.insert(QStringLiteral("elements"), total);
	out.insert(QStringLiteral("tags"), counts.size());
	return out;
}

//! Every tag either side holds, sorted, so the reported order is reproducible.
QStringList unionOfTags(const QHash<QString, int>& left, const QHash<QString, int>& right)
{
	QStringList tags = left.keys();
	for (const QString& tag : right.keys())
	{
		if (!tags.contains(tag)) { tags.append(tag); }
	}
	std::sort(tags.begin(), tags.end());
	return tags;
}

QJsonArray differingTags(const QHash<QString, int>& left, const QHash<QString, int>& right,
	int* total)
{
	QJsonArray differing;
	for (const QString& tag : unionOfTags(left, right))
	{
		const int a = left.value(tag);
		const int b = right.value(tag);
		if (a == b) { continue; }
		differing.append(QJsonObject{{QStringLiteral("tag"), tag}, {QStringLiteral("a"), a},
			{QStringLiteral("b"), b}, {QStringLiteral("delta"), b - a}});
	}
	*total = differing.size();
	QJsonArray reported;
	for (int i = 0; i < differing.size() && i < Bounds::MaxComparedTags; ++i)
	{
		reported.append(differing.at(i));
	}
	return reported;
}

} // namespace

QByteArray projectDocumentText(const QByteArray& raw)
{
	if (raw.isEmpty()) { return QByteArray(); }
	// A `.mmpz` v2 container must be refused BEFORE the `<?xml` probe below, and
	// that order is the whole of this guard rather than a precaution. The
	// container STORES its entries - DawProjectZip.cpp:8-15 records why it cannot
	// compress them - so its bytes contain a LITERAL `<?xml` and the probe below
	// matches them. Without this, the ZIP was returned as if it were the document
	// text, the tag counter read the archive's own headers, and a comparison
	// reported `readable: true` with counts that mean nothing (measured
	// 2026-09-21: `contains("<?xml")` is true of a container written by
	// projectcontainer::writeContainer).
	//
	// The refusal is silent, exactly as the `readable: false` answer already was
	// for a body that will not inflate. The two causes are deliberately NOT told
	// apart, for the reason DocumentIndex.h:236-238 gives for the same choice:
	// naming one of them would invite a caller to branch on a distinction it has
	// no use for.
	if (projectcontainer::isContainer(raw)) { return QByteArray(); }
	// An `.mmp` IS its XML text; an `.mmpz` is a qCompress container, the shape
	// DataFile::writeFile writes and qUncompress reads back.
	if (raw.contains("<?xml")) { return raw; }
	const QByteArray text = qUncompress(raw);
	if (text.isEmpty() || text.size() > Bounds::MaxDocumentBytes) { return QByteArray(); }
	return text;
}

QJsonObject compareRevisionDocuments(const QByteArray& left, const QByteArray& right)
{
	const QByteArray leftText = projectDocumentText(left);
	const QByteArray rightText = projectDocumentText(right);
	const bool readable = !leftText.isEmpty() && !rightText.isEmpty();

	QJsonObject out;
	out.insert(QStringLiteral("identical"), left == right);
	out.insert(QStringLiteral("readable"), readable);
	out.insert(QStringLiteral("bytes_a"), static_cast<qint64>(left.size()));
	out.insert(QStringLiteral("bytes_b"), static_cast<qint64>(right.size()));
	out.insert(QStringLiteral("size_delta"), static_cast<qint64>(right.size() - left.size()));
	if (!readable)
	{
		out.insert(QStringLiteral("a"), emptySide());
		out.insert(QStringLiteral("b"), emptySide());
		out.insert(QStringLiteral("element_delta"), 0);
		out.insert(QStringLiteral("differing_tags"), QJsonArray());
		out.insert(QStringLiteral("differing_tag_count"), 0);
		return out;
	}

	const QHash<QString, int> countsA = elementCounts(leftText);
	const QHash<QString, int> countsB = elementCounts(rightText);
	int elementsA = 0;
	int elementsB = 0;
	out.insert(QStringLiteral("a"), sideTotals(countsA, &elementsA));
	out.insert(QStringLiteral("b"), sideTotals(countsB, &elementsB));
	out.insert(QStringLiteral("element_delta"), elementsB - elementsA);
	int differing = 0;
	out.insert(QStringLiteral("differing_tags"), differingTags(countsA, countsB, &differing));
	out.insert(QStringLiteral("differing_tag_count"), differing);
	return out;
}

} // namespace control
} // namespace lmms
