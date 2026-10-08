/*
 * ControlScaleSupport.cpp - the `scale.*` group's shared vocabulary
 *                           (include/ControlScaleShared.h): one definition of
 *                           "what a root and a scale name resolve to".
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
 */

#include <algorithm>
#include <cmath>

#include "ControlScaleShared.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "InstrumentFunctions.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteTransform.h"

namespace lmms
{
namespace control
{

using ChordTable = InstrumentFunctionNoteStacking::ChordTable;
using Chord = InstrumentFunctionNoteStacking::Chord;

const QStringList& scaleRootNames()
{
	static const QStringList names{
		QStringLiteral("C"), QStringLiteral("C#"), QStringLiteral("D"), QStringLiteral("D#"),
		QStringLiteral("E"), QStringLiteral("F"), QStringLiteral("F#"), QStringLiteral("G"),
		QStringLiteral("G#"), QStringLiteral("A"), QStringLiteral("A#"), QStringLiteral("B")};
	return names;
}

ScaleContext& scaleContext()
{
	static ScaleContext value;
	return value;
}

QJsonObject customScales()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(QStringLiteral("scales"), QStringLiteral("custom")).toUtf8()).object();
}

void setCustomScales(const QJsonObject& scales)
{
	ConfigManager::inst()->setValue(QStringLiteral("scales"), QStringLiteral("custom"),
		QString::fromUtf8(QJsonDocument(scales).toJson(QJsonDocument::Compact)));
}

//! A degree from a hand-edited scales/custom entry, or -1 when the entry is not a whole
//! semitone offset in the 0..11 window defineScale writes. toInt() alone would turn anything
//! it cannot read into 0 and make the scale a silent C.
static int customScaleDegree(const QJsonValue& degree)
{
	if (!degree.isDouble()) { return -1; }
	const double semitones = degree.toDouble();
	if (!std::isfinite(semitones) || semitones < 0.0 || semitones > 11.0) { return -1; }
	const int offset = static_cast<int>(semitones);
	return semitones == static_cast<double>(offset) ? offset : -1;
}

std::vector<int> scaleDegreesOf(const QString& scale)
{
	if (scale.isEmpty()) { return std::vector<int>(); }
	// 0.5.0 L07 (ED-115): a scale of the user's own, by its name, before the engine's table.
	const QJsonArray custom = customScales().value(scale).toArray();
	if (!custom.isEmpty())
	{
		// an entry with nothing usable falls through to the engine table rather than answering
		// with a one-note scale built out of whatever the file happened to carry.
		std::vector<int> degrees;
		for (const QJsonValue& degree : custom)
		{
			const int offset = customScaleDegree(degree);
			if (offset >= 0) { degrees.push_back(offset); }
		}
		if (!degrees.empty()) { return degrees; }
	}
	const Chord& chord = ChordTable::getInstance().getScaleByName(scale);
	// getByName returns an EMPTY chord for a name it does not carry, so isEmpty()
	// is the "not found" test - an empty vector out of a non-empty name means the
	// caller was refused before this point.
	std::vector<int> degrees;
	if (chord.isEmpty()) { return degrees; }
	degrees.reserve(static_cast<size_t>(chord.size()));
	for (int i = 0; i < chord.size(); ++i) { degrees.push_back(chord[i]); }
	return degrees;
}

std::vector<int> scalePitchClasses(int root, const std::vector<int>& degrees)
{
	std::vector<int> classes;
	classes.reserve(degrees.size());
	for (int degree : degrees) { classes.push_back(((root + degree) % 12 + 12) % 12); }
	std::sort(classes.begin(), classes.end());
	classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
	return classes;
}

QString scaleMask(const std::vector<int>& classes)
{
	QString mask;
	for (int pc = 0; pc < 12; ++pc)
	{
		mask.append(std::find(classes.begin(), classes.end(), pc) != classes.end()
			? QLatin1Char('1') : QLatin1Char('0'));
	}
	return mask;
}

QJsonArray scaleClassesJson(const std::vector<int>& classes)
{
	QJsonArray out;
	for (int pc : classes) { out.append(pc); }
	return out;
}

QJsonArray scaleDegreesJson(const std::vector<int>& degrees)
{
	QJsonArray out;
	for (int degree : degrees) { out.append(degree); }
	return out;
}

bool readScaleRoot(const QJsonObject& args, const QString& key, int* out, ControlResult* error)
{
	if (!args.contains(key)) { return true; }
	const QJsonValue value = args.value(key);
	if (value.isDouble())
	{
		const double number = value.toDouble();
		// Clamped before the cast (casting a double outside int's range is
		// undefined); anything clamped is outside 0..11 and refused below.
		const int index = static_cast<int>(std::clamp(number, -1.0, 12.0));
		if (number != static_cast<double>(index) || index < 0 || index > 11)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'%1' is %2; a root is a key index in 0..11 (0 = C) or a name such "
					"as \"C\", \"C#\" or \"Db\"").arg(key).arg(number));
			return false;
		}
		*out = index;
		return true;
	}

	QString name = value.toString().trimmed();
	name.replace(QChar(0x266F), QLatin1Char('#')); // MUSIC SHARP SIGN
	name.replace(QChar(0x266D), QLatin1Char('b')); // MUSIC FLAT SIGN
	// The piano roll's own spelling is "C# / Db"; the first token is the key.
	const int slash = name.indexOf(QLatin1Char('/'));
	if (slash >= 0) { name = name.left(slash).trimmed(); }
	// "C#3" and "Db-1" name the same pitch class as "C#" and "Db".
	while (!name.isEmpty() && (name.at(name.size() - 1).isDigit()
		|| name.at(name.size() - 1) == QLatin1Char('-')))
	{
		name.chop(1);
	}

	static const QStringList flats{QStringLiteral("C"), QStringLiteral("Db"), QStringLiteral("D"),
		QStringLiteral("Eb"), QStringLiteral("E"), QStringLiteral("F"), QStringLiteral("Gb"),
		QStringLiteral("G"), QStringLiteral("Ab"), QStringLiteral("A"), QStringLiteral("Bb"),
		QStringLiteral("B")};
	for (int index = 0; index < scaleRootNames().size(); ++index)
	{
		if (name.compare(scaleRootNames().at(index), Qt::CaseInsensitive) == 0
			|| name.compare(flats.at(index), Qt::CaseInsensitive) == 0)
		{
			*out = index;
			return true;
		}
	}
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is '%2', which is not a key: the twelve are C, C#, D, D#, E, F, F#, "
			"G, G#, A, A#, B (or the same keys spelled with flats)").arg(key)
			.arg(value.toString()));
	return false;
}

bool readScaleName(const QJsonObject& args, QString* out, ControlResult* error)
{
	const QString name = args.value(QStringLiteral("scale")).toString().trimmed();
	*out = name;
	if (name.isEmpty()) { return true; }
	// 0.5.0 L07 (ED-115): one of the user's own - only when it has degrees, as scaleDegreesOf reads it.
	if (!customScales().value(name).toArray().isEmpty()) { return true; }
	const Chord& chord = ChordTable::getInstance().getScaleByName(name);
	if (chord.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no scale called '%1': scale.list publishes every one this engine knows "
				"(names are exact, e.g. \"Major\", \"Minor\", \"Major pentatonic\")").arg(name));
		return false;
	}
	*out = chord.getName();
	return true;
}

QJsonObject scaleContextState()
{
	const ScaleContext& ctx = scaleContext();
	const std::vector<int> degrees = scaleDegreesOf(ctx.scale);
	const std::vector<int> classes = scalePitchClasses(ctx.root, degrees);
	QJsonObject out;
	out.insert(QStringLiteral("root"), ctx.root);
	out.insert(QStringLiteral("root_name"), scaleRootNames().at(ctx.root));
	out.insert(QStringLiteral("scale"), ctx.scale);
	out.insert(QStringLiteral("degrees"), scaleDegreesJson(degrees));
	out.insert(QStringLiteral("pitch_classes"), scaleClassesJson(classes));
	out.insert(QStringLiteral("mask"), scaleMask(classes));
	out.insert(QStringLiteral("scale_set"), !ctx.scale.isEmpty());
	return out;
}

bool resolveScale(const QJsonObject& args, ResolvedScale* out, ControlResult* error)
{
	const ScaleContext& ctx = scaleContext();
	out->root = ctx.root;
	out->scale = ctx.scale;
	if (!readScaleRoot(args, QStringLiteral("root"), &out->root, error)) { return false; }
	if (args.contains(QStringLiteral("scale")) && !readScaleName(args, &out->scale, error))
	{
		return false;
	}
	out->fromArguments =
		args.contains(QStringLiteral("root")) || args.contains(QStringLiteral("scale"));
	out->degrees = scaleDegreesOf(out->scale);
	out->classes = scalePitchClasses(out->root, out->degrees);
	return true;
}

int countNotesInScale(const MidiClip& clip, const std::vector<int>& classes)
{
	if (classes.empty()) { return 0; }
	NoteTransform::Filter filter;
	filter.scaleMatch = NoteTransform::ScaleMatch::InScale;
	filter.scaleDegrees = classes;
	int inScale = 0;
	for (const Note* note : clip.notes())
	{
		if (note != nullptr && NoteTransform::matches(*note, filter)) { ++inScale; }
	}
	return inScale;
}

QJsonObject rootArgumentSchema()
{
	// No "type": the argument is a key index OR a name, and the schema subset's
	// type check is one type per property. The handler is what refuses a value
	// that is neither, typed (readScaleRoot).
	return QJsonObject{{QStringLiteral("description"),
		QStringLiteral("key index 0..11, or a name (\"C\", \"C#\", \"Db\")")}};
}

} // namespace control
} // namespace lmms
