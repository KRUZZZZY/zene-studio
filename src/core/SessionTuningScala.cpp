/*
 * SessionTuningScala.cpp - the .scl / .kbm file parse half of SessionTuning
 *                          (board card #712)
 *
 * The SAME line rules src/gui/MicrotunerConfig.cpp's dialog applies (the
 * dialog and this core parser feed the same table - gui may call core, core
 * never calls gui). Split out of SessionTuning.cpp for the file-length
 * ratchet, and split AGAIN inside itself for the CCN ratchet: reading the
 * lines, filtering comments and building the Keymap are three small
 * functions, not one switch of eleven branches.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SessionTuning.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "Keymap.h"
#include "Scale.h"

namespace lmms
{

namespace
{

//! The n/d half of an interval line. A ratio is two POSITIVE integers: the
//! denominator is parsed with its own ok check (3/nope is not 3/1), and neither
//! half is clamped up to 1 (that silently rewrote 3/0 as 3/1 and 0/2 as 1/2).
//! Split out so intervalFromLine's CCN counts only its two shapes.
bool ratioFromLine(const QString& line, int numerator, Interval* out)
{
	int denominator = 1;
	const QString value = line.section(QRegularExpression(QStringLiteral("\\s+")),
		0, 0, QString::SectionSkipEmpty);
	if (value.contains(QLatin1Char('/')))
	{
		if (value.count(QLatin1Char('/')) != 1) { return false; }
		bool denOk = false;
		denominator = value.section(QLatin1Char('/'), 1, 1).toInt(&denOk);
		if (!denOk || denominator <= 0) { return false; }
	}
	if (numerator <= 0) { return false; }
	*out = Interval(static_cast<uint32_t>(numerator), static_cast<uint32_t>(denominator));
	return true;
}

//! The .scl interval line MicrotunerConfig::applyScale() accepts: the first
//! whitespace-or-slash section is either cents (contains '.') or a ratio.
bool intervalFromLine(const QString& line, Interval* out)
{
	const QString first = line.section(QRegularExpression(QStringLiteral("\\s+|/")),
		0, 0, QString::SectionSkipEmpty);
	if (first.isEmpty()) { return false; }
	if (first.contains(QLatin1Char('.')))
	{
		bool ok = false;
		const float cents = first.toFloat(&ok);
		if (!ok) { return false; }
		*out = Interval(cents);
		return true;
	}
	bool numOk = false;
	const int num = first.toInt(&numOk);
	if (!numOk) { return false; }
	return ratioFromLine(line, num, out);
}

//! Record a typed parse failure; a null \a error means "the caller does not
//! want the text" (every current caller wants it) - a helper so each parse
//! function's CCN counts its decisions, not its error plumbing.
void failWith(QString* error, const QString& message)
{
	if (error != nullptr) { *error = message; }
}

//! The .scl body: first non-comment line = name, second = degree count, then
//! up to that many interval lines (the dialog's while/i<limit loop, with the
//! comments skipping i++ exactly as its `continue` does).
bool readScaleLines(QTextStream& stream, QString* name, QStringList* lines, QString* error)
{
	int limit = 0;
	int i = -2;
	while (!stream.atEnd() && i < limit)
	{
		const QString line = stream.readLine();
		if (line.isEmpty() || line.startsWith(QLatin1Char('!'))) { continue; }
		if (i == -2) { *name = line; }
		else if (i == -1)
		{
			limit = line.toInt();
			if (limit <= 0)
			{
				failWith(error, QStringLiteral("no degree count in the .scl file"));
				return false;
			}
		}
		else { lines->append(line); }
		++i;
	}
	// The two header lines are a PROMISE about the scale that follows: a
	// stream that never reached the degree count never said how many degrees
	// it has, and the count check below compares zero against zero - so an
	// empty file (or one that only names a scale) passed it. The scale built
	// from it would hold only the 1/1 interval loadScaleFile() starts with:
	// every key at the same pitch. A file that describes no scale is refused.
	if (i < 0)
	{
		failWith(error, QStringLiteral("the .scl file must contain a name and a positive degree count"));
		return false;
	}
	// The count the file declared is a PROMISE about how many degrees follow:
	// a file that ends early has not described the scale it carries, and
	// building one from the lines that DID arrive silently substituted a
	// different scale for it. \a limit is positive by now, so an empty or short
	// list is a truncated file, not a scale of zero degrees.
	if (lines->size() != limit)
	{
		failWith(error, QStringLiteral("the .scl file declares %1 degrees but carries %2 "
			"interval lines").arg(limit).arg(lines->size()));
		return false;
	}
	return true;
}

//! A .kbm body with the comments already filtered (comments never consume a
//! field - the dialog's loop skips its i++ the same way; "!!" before any
//! value is the LMMS description extension a .kbM carries no other way).
//! Positions: 0 size, 1..4 first/last/middle/base, 5 base frequency,
//! 6 octave degree (ignored, as the dialog ignores it), 7.. mapping lines.
bool keymapFields(const QStringList& raw, QString* description, QStringList* fields,
	QString* error)
{
	fields->clear();
	for (const QString& line : raw)
	{
		if (line.isEmpty() || line.startsWith(QLatin1Char('!')))
		{
			if (line.startsWith(QStringLiteral("!!")) && fields->isEmpty())
			{
				*description = line.mid(2);
			}
			continue;
		}
		fields->append(line);
	}
	if (fields->size() < 8)
	{
		if (error != nullptr) { *error = QStringLiteral("no keymap size / fields in the .kbm file"); }
		return false;
	}
	return true;
}

//! One mapping line: "x" = not mapped (the dialog's -1), else the degree
//! from its FIRST whitespace section (a trailing comment must not zero it).
//! A mapping line's degree: -1 for "x" (unmapped). A degree that is not a
//! whole number, or is negative, is refused (*ok false), as the Microtuner
//! dialog's validation refuses it (MicrotunerConfig.cpp).
int mapDegreeOf(const QString& line, bool* ok)
{
	const QString first = line.section(QRegularExpression(QStringLiteral("\\s+")),
		0, 0, QString::SectionSkipEmpty);
	if (first == QLatin1String("x")) { *ok = true; return -1; }
	const int degree = first.toInt(ok);
	*ok = *ok && degree >= 0;
	return degree;
}

//! The mapping lines after the seven header fields, or false with *error set.
bool mappingOf(const QStringList& fields, std::vector<int>* map, QString* error)
{
	for (int index = 7; index < fields.size(); ++index)
	{
		bool degreeOk = false;
		map->push_back(mapDegreeOf(fields.at(index), &degreeOk));
		if (!degreeOk)
		{
			*error = QStringLiteral("mapping line %1 must be x or a non-negative whole scale degree")
				.arg(index - 6);
			return false;
		}
	}
	return true;
}

//! The four key fields (first / last / middle / base), each read with its own
//! conversion result checked: QString::toInt() returns 0 for text that is not a
//! number at all, so an unchecked read made "oops" into key 0 and the clamp
//! then passed it on as a real key. A field that does not convert is refused
//! (*ok false); a whole number out of keyboard range is still clamped, the way
//! the dialog's spin boxes clamp it.
int keyFieldOf(const QString& field, bool* ok)
{
	int key = 0;
	*ok = false;
	key = field.toInt(ok);
	return std::clamp(key, 0, NumKeys - 1);
}

//! The key bounds and base frequency of a .kbm header.
struct KeyBounds
{
	int first;
	int last;
	int middle;
	int baseKey;
	double baseFreq;
};

//! The base frequency field: finite as a double AND inside the range a float
//! can carry, because noteToFreq() hands the built table to the render path as
//! a float.
double baseFreqOf(const QString& field, QString* error)
{
	bool ok = false;
	const double baseFreq = field.toDouble(&ok);
	if (!ok || !std::isfinite(baseFreq) || baseFreq <= 0.0
		|| baseFreq > std::numeric_limits<float>::max()
		|| static_cast<float>(baseFreq) == 0.0f)
	{
		*error = QStringLiteral("field 6 must be a positive base frequency the render path can use");
		return 0.0;
	}
	return baseFreq;
}

//! Fields 1..6 of a .kbm header: the four key bounds and the base frequency,
//! every one of them read with its conversion checked. *error names the field.
bool keyBoundsOf(const QStringList& fields, KeyBounds* bounds, QString* error)
{
	bool firstOk = false;
	bool lastOk = false;
	bool middleOk = false;
	bool baseKeyOk = false;
	bounds->first = keyFieldOf(fields.at(1), &firstOk);
	bounds->last = keyFieldOf(fields.at(2), &lastOk);
	bounds->middle = keyFieldOf(fields.at(3), &middleOk);
	bounds->baseKey = keyFieldOf(fields.at(4), &baseKeyOk);
	if (!firstOk || !lastOk || !middleOk || !baseKeyOk)
	{
		*error = QStringLiteral("the first / last / middle / base key fields must all be "
			"whole numbers");
		return false;
	}
	bounds->baseFreq = baseFreqOf(fields.at(5), error);
	if (bounds->baseFreq <= 0.0) { return false; }
	if (bounds->first > bounds->last)
	{
		*error = QStringLiteral("first key %1 is above last key %2")
			.arg(bounds->first).arg(bounds->last);
		return false;
	}
	return true;
}

//! The validated Keymap out of the filtered fields: the declared size, the key
//! bounds, and the mapping lines after the seven header fields.
std::shared_ptr<Keymap> keymapOf(const QStringList& fields, const QString& description,
	bool* ok, QString* error)
{
	bool sizeOk = false;
	const int size = fields.at(0).toInt(&sizeOk);
	if (!sizeOk || size <= 0)
	{
		*ok = false;
		*error = QStringLiteral("no keymap size");
		return nullptr;
	}
	KeyBounds bounds{0, 0, 0, 0, 0.0};
	if (!keyBoundsOf(fields, &bounds, error))
	{
		*ok = false;
		return nullptr;
	}
	// fields 7.. are the mapping lines (index 6 is the octave degree).
	std::vector<int> map;
	if (!mappingOf(fields, &map, error))
	{
		*ok = false;
		return nullptr;
	}
	// The size field PROMISES how many entries the map has, and getDegree()
	// takes every key modulo that size: a map built from fewer lines than were
	// declared repeats with the wrong period and answers with the wrong degree
	// for most of the keyboard. A file that disagrees with itself is refused.
	if (map.size() != static_cast<std::size_t>(size))
	{
		*ok = false;
		*error = QStringLiteral("the .kbm file declares a map of %1 entries but carries %2 "
			"mapping lines").arg(size).arg(map.size());
		return nullptr;
	}
	*ok = true;
	return std::make_shared<Keymap>(description, std::move(map),
		bounds.first, bounds.last, bounds.middle, bounds.baseKey,
		static_cast<float>(bounds.baseFreq));
}

} // namespace

bool SessionTuning::loadScaleFile(const QString& path, QString* error)
{
	if (path.size() > MaxSourceLength)
	{
		failWith(error, QStringLiteral("the source path is %1 characters; the cap is %2 - "
			"nothing was written").arg(path.size()).arg(MaxSourceLength));
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		failWith(error, QStringLiteral("cannot read the scale file '%1'").arg(path));
		return false;
	}
	QTextStream stream(&file);
	QString name;
	QStringList lines;
	QString reason;
	if (!readScaleLines(stream, &name, &lines, &reason))
	{
		failWith(error, QStringLiteral("'%1' does not look like a .scl file (%2)")
			.arg(path, reason));
		return false;
	}

	// applyScale() starts the interval vector with a 1/1 and appends every
	// parsed line - the same vector, therefore the same table.
	std::vector<Interval> intervals;
	intervals.emplace_back(1, 1);
	for (const QString& line : std::as_const(lines))
	{
		Interval interval;
		if (!intervalFromLine(line, &interval))
		{
			failWith(error, QStringLiteral("the interval line '%1' in '%2' is neither a "
				"ratio nor a cents value").arg(line, path));
			return false;
		}
		intervals.push_back(interval);
	}

	auto scale = std::make_shared<Scale>(name.isEmpty() ? QFileInfo(path).fileName() : name,
		std::move(intervals));
	Table next;
	QString buildError;
	if (!buildTable(scale.get(), *m_keymap, &next, &buildError))
	{
		failWith(error, buildError);
		return false;
	}
	m_scale = std::move(scale);
	// Loading a .scl ACTIVATES the session table: that is what makes every
	// track follow the file (the card's proof), dialog-fed or socket-fed.
	commit(next, true, path);
	return true;
}

bool SessionTuning::loadKeymapFile(const QString& path, QString* error)
{
	if (path.size() > MaxSourceLength)
	{
		failWith(error, QStringLiteral("the source path is %1 characters; the cap is %2 - "
			"nothing was written").arg(path.size()).arg(MaxSourceLength));
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		failWith(error, QStringLiteral("cannot read the keymap file '%1'").arg(path));
		return false;
	}
	QTextStream stream(&file);
	QString description = QFileInfo(path).baseName();
	QStringList raw;
	while (!stream.atEnd()) { raw.append(stream.readLine()); }

	QStringList fields;
	QString reason;
	if (!keymapFields(raw, &description, &fields, &reason))
	{
		failWith(error, QStringLiteral("'%1' does not look like a .kbm file (%2)")
			.arg(path, reason));
		return false;
	}
	bool built = false;
	auto keymap = keymapOf(fields, description, &built, &reason);
	if (!built)
	{
		failWith(error, QStringLiteral("'%1': %2").arg(path, reason));
		return false;
	}

	Table next;
	QString buildError;
	if (!buildTable(m_scale.get(), *keymap, &next, &buildError))
	{
		failWith(error, buildError);
		return false;
	}
	m_keymap = std::move(keymap);
	// Content only: a keymap edit rearranges whatever table is loaded, and
	// does NOT switch the session table on by itself.
	const bool wasActive = isActive();
	commit(next, wasActive, path);
	return true;
}

} // namespace lmms
