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
	int den = 1;
	if (line.contains(QLatin1Char('/')))
	{
		den = line.split(QLatin1Char('/')).at(1)
			.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0, QString::SectionSkipEmpty)
			.toInt();
	}
	*out = Interval(static_cast<uint32_t>(std::max(num, 1)),
		static_cast<uint32_t>(std::max(den, 1)));
	return true;
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
	if (lines->isEmpty())
	{
		failWith(error, QStringLiteral("no interval lines in the .scl file"));
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
int mapDegreeOf(const QString& line)
{
	const QString first = line.section(QRegularExpression(QStringLiteral("\\s+")),
		0, 0, QString::SectionSkipEmpty);
	if (first == QLatin1String("x")) { return -1; }
	return first.toInt();
}

//! The validated Keymap out of the filtered fields: size, the four key
//! bounds (clamped the way the dialog's models clamp), the base frequency,
//! and the mapping lines after the seven header fields.
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
	const int first = std::clamp(fields.at(1).toInt(), 0, NumKeys - 1);
	const int last = std::clamp(fields.at(2).toInt(), 0, NumKeys - 1);
	const int middle = std::clamp(fields.at(3).toInt(), 0, NumKeys - 1);
	const int baseKey = std::clamp(fields.at(4).toInt(), 0, NumKeys - 1);
	bool freqOk = false;
	const double baseFreq = fields.at(5).toDouble(&freqOk);
	if (!freqOk || !std::isfinite(baseFreq) || baseFreq <= 0.0)
	{
		*ok = false;
		*error = QStringLiteral("field 6 must be a positive base frequency");
		return nullptr;
	}
	if (first > last)
	{
		*ok = false;
		*error = QStringLiteral("first key %1 is above last key %2").arg(first).arg(last);
		return nullptr;
	}
	// fields 7.. are the mapping lines (index 6 is the octave degree).
	std::vector<int> map;
	for (int index = 7; index < fields.size(); ++index)
	{
		map.push_back(mapDegreeOf(fields.at(index)));
	}
	if (map.empty())
	{
		*ok = false;
		*error = QStringLiteral("no mapping lines");
		return nullptr;
	}
	*ok = true;
	return std::make_shared<Keymap>(description, std::move(map),
		first, last, middle, baseKey, baseFreq);
}

} // namespace

bool SessionTuning::loadScaleFile(const QString& path, QString* error)
{
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
