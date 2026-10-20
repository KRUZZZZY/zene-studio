/*
 * CommandMetadata.cpp - how a registered command and its arguments read to a musician
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

#include "CommandMetadata.h"

#include <cmath>

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMessageBox>
#include <QSet>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "GuiApplication.h"
#include "ScriptCommandBridge.h"

namespace lmms::gui
{

namespace
{

//! The spelling a word of an id or a key takes in prose: an acronym, a unit, an expansion.
const QHash<QString, QString>& wordForms()
{
	static const QHash<QString, QString> forms{
		{QStringLiteral("db"), QStringLiteral("dB")}, {QStringLiteral("hz"), QStringLiteral("Hz")},
		{QStringLiteral("khz"), QStringLiteral("kHz")}, {QStringLiteral("bpm"), QStringLiteral("BPM")},
		{QStringLiteral("id"), QStringLiteral("ID")}, {QStringLiteral("ids"), QStringLiteral("IDs")},
		{QStringLiteral("midi"), QStringLiteral("MIDI")}, {QStringLiteral("smf"), QStringLiteral("MIDI file")},
		{QStringLiteral("mts"), QStringLiteral("MTS")}, {QStringLiteral("vca"), QStringLiteral("VCA")},
		{QStringLiteral("oop"), QStringLiteral("out-of-process")}, {QStringLiteral("pdc"), QStringLiteral("latency compensation")},
		{QStringLiteral("cpu"), QStringLiteral("CPU")}, {QStringLiteral("ui"), QStringLiteral("UI")},
		{QStringLiteral("fx"), QStringLiteral("FX")}, {QStringLiteral("lufs"), QStringLiteral("LUFS")},
		{QStringLiteral("mpe"), QStringLiteral("MPE")}, {QStringLiteral("lua"), QStringLiteral("Lua")},
		{QStringLiteral("json"), QStringLiteral("JSON")}, {QStringLiteral("url"), QStringLiteral("URL")},
		{QStringLiteral("dawproject"), QStringLiteral("DAWproject")}, {QStringLiteral("wasm"), QStringLiteral("WASM")},
		{QStringLiteral("vst"), QStringLiteral("VST")}, {QStringLiteral("vst3"), QStringLiteral("VST3")},
		{QStringLiteral("clap"), QStringLiteral("CLAP")}, {QStringLiteral("lv2"), QStringLiteral("LV2")},
		{QStringLiteral("lfo"), QStringLiteral("LFO")}, {QStringLiteral("eq"), QStringLiteral("EQ")},
		{QStringLiteral("rms"), QStringLiteral("RMS")}, {QStringLiteral("osc"), QStringLiteral("OSC")},
		{QStringLiteral("wav"), QStringLiteral("WAV")}, {QStringLiteral("flac"), QStringLiteral("FLAC")},
		{QStringLiteral("mp3"), QStringLiteral("MP3")}, {QStringLiteral("ogg"), QStringLiteral("OGG")},
		{QStringLiteral("aiff"), QStringLiteral("AIFF")}, {QStringLiteral("tpdf"), QStringLiteral("TPDF")},
		{QStringLiteral("livecode"), QStringLiteral("live code")}, {QStringLiteral("safestart"), QStringLiteral("safe start")},
		{QStringLiteral("pct"), QStringLiteral("percent")}, {QStringLiteral("bak"), QStringLiteral("backup")},
	};
	return forms;
}

//! The unit a key's last word names ("gain_db" -> dB). Only for numeric fields (fieldUnit).
const QHash<QString, QString>& unitWords()
{
	static const QHash<QString, QString> units{
		{QStringLiteral("db"), QStringLiteral("dB")}, {QStringLiteral("ms"), QStringLiteral("ms")},
		{QStringLiteral("hz"), QStringLiteral("Hz")}, {QStringLiteral("khz"), QStringLiteral("kHz")},
		{QStringLiteral("bpm"), QStringLiteral("BPM")}, {QStringLiteral("tempo"), QStringLiteral("BPM")},
		{QStringLiteral("pct"), QStringLiteral("%")}, {QStringLiteral("percent"), QStringLiteral("%")},
		{QStringLiteral("tick"), QStringLiteral("ticks")}, {QStringLiteral("ticks"), QStringLiteral("ticks")},
		{QStringLiteral("sec"), QStringLiteral("s")}, {QStringLiteral("secs"), QStringLiteral("s")},
		{QStringLiteral("seconds"), QStringLiteral("s")}, {QStringLiteral("semitones"), QStringLiteral("semitones")},
		{QStringLiteral("semis"), QStringLiteral("semitones")}, {QStringLiteral("cents"), QStringLiteral("cents")},
		{QStringLiteral("frames"), QStringLiteral("frames")}, {QStringLiteral("samples"), QStringLiteral("samples")},
		{QStringLiteral("bars"), QStringLiteral("bars")}, {QStringLiteral("beats"), QStringLiteral("beats")},
		{QStringLiteral("lufs"), QStringLiteral("LUFS")}, {QStringLiteral("bytes"), QStringLiteral("bytes")},
		{QStringLiteral("octaves"), QStringLiteral("octaves")}, {QStringLiteral("steps"), QStringLiteral("steps")},
	};
	return units;
}

//! Verbs that read as nothing on their own: "remove" names its object, the command's group.
const QSet<QString>& objectVerbs()
{
	static const QSet<QString> verbs{QStringLiteral("add"), QStringLiteral("remove"), QStringLiteral("create"),
		QStringLiteral("delete"), QStringLiteral("rename"), QStringLiteral("move"), QStringLiteral("select"),
		QStringLiteral("duplicate"), QStringLiteral("split"), QStringLiteral("clear"), QStringLiteral("load"),
		QStringLiteral("save"), QStringLiteral("open"), QStringLiteral("import"), QStringLiteral("export"),
		QStringLiteral("reset"), QStringLiteral("enable"), QStringLiteral("disable"), QStringLiteral("assign"),
		QStringLiteral("unassign"), QStringLiteral("apply"), QStringLiteral("list"), QStringLiteral("get"),
		QStringLiteral("set"), QStringLiteral("restart"), QStringLiteral("bypass"), QStringLiteral("merge"),
		QStringLiteral("diff"), QStringLiteral("run"), QStringLiteral("read"), QStringLiteral("unload")};
	return verbs;
}

//! A trailing action word that a name puts first: "tempo_map_add" reads "Add Tempo Map".
const QSet<QString>& trailingActions()
{
	static const QSet<QString> actions{QStringLiteral("add"), QStringLiteral("remove"), QStringLiteral("set"),
		QStringLiteral("get"), QStringLiteral("list"), QStringLiteral("clear"), QStringLiteral("apply"),
		QStringLiteral("save"), QStringLiteral("delete"), QStringLiteral("create"), QStringLiteral("rename"),
		QStringLiteral("load"), QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("generate"),
		QStringLiteral("arm"), QStringLiteral("reset"), QStringLiteral("sync"), QStringLiteral("move"),
		QStringLiteral("enable"), QStringLiteral("disable"), QStringLiteral("discard"), QStringLiteral("restore")};
	return actions;
}

//! The few ids the rules above would name badly.
const QHash<QString, const char*>& nameOverrides()
{
	static const QHash<QString, const char*> names{
		{QStringLiteral("render.render"), QT_TRANSLATE_NOOP("CommandMetadata", "Render Project")},
		{QStringLiteral("control.ping"), QT_TRANSLATE_NOOP("CommandMetadata", "Check the Control Connection")},
		{QStringLiteral("control.commands_list"), QT_TRANSLATE_NOOP("CommandMetadata", "List Commands")},
		{QStringLiteral("control.id_contract"), QT_TRANSLATE_NOOP("CommandMetadata", "Show the ID Contract")},
		{QStringLiteral("control.surface_report"), QT_TRANSLATE_NOOP("CommandMetadata", "Show the Control Surface Report")},
		{QStringLiteral("control.transactions"), QT_TRANSLATE_NOOP("CommandMetadata", "Show Recent Changes")},
		{QStringLiteral("control.undo_depth"), QT_TRANSLATE_NOOP("CommandMetadata", "Show Undo Depth")},
	};
	return names;
}

QString tr(const char* text)
{
	return QCoreApplication::translate("CommandMetadata", text);
}

//! A word as prose spells it: its form, else itself lower-cased.
QString wordForm(const QString& word)
{
	const QString lower = word.toLower();
	return wordForms().value(lower, lower);
}

//! "set tempo map" -> "Set Tempo Map": a lower-case word gets a capital; an acronym keeps its own.
QString titleCase(const QStringList& words)
{
	QStringList out;
	for (const QString& word : words)
	{
		for (QString part : wordForm(word).split(QLatin1Char(' ')))
		{
			if (!part.isEmpty() && part == part.toLower()) { part[0] = part.at(0).toUpper(); }
			out.append(part);
		}
	}
	return out.join(QLatin1Char(' '));
}

//! "missing count" -> "Missing count": the first word gets a capital, the rest keep their form.
QString sentenceCase(const QStringList& words)
{
	QStringList out;
	for (const QString& word : words) { out.append(wordForm(word)); }
	QString text = out.join(QLatin1Char(' '));
	if (!text.isEmpty() && text.at(0).isLower()) { text[0] = text.at(0).toUpper(); }
	return text;
}

QStringList wordsOf(const QString& text)
{
	return text.split(QLatin1Char('_'), Qt::SkipEmptyParts);
}

//! A whole number in full (a tick, a size - never "1.23457e+06"), else the shortest text that reads back as the same
//! number: a value typed into a form or taken from a default must not change on its way to the command.
QString numberText(double number)
{
	constexpr double MaxExactWhole = 9007199254740992.0;  // 2^53: every whole number below it is exact in a double
	const bool whole = std::abs(number) < MaxExactWhole && number == std::floor(number);
	if (whole) { return QString::number(static_cast<qint64>(number)); }
	return QString::number(number, 'g', QLocale::FloatingPointShortest);
}

bool isPrimitiveArray(const QJsonArray& array)
{
	for (const QJsonValue& item : array)
	{
		if (item.isArray() || item.isObject()) { return false; }
	}
	return true;
}

//! A key's words without the unit they end in, or the "id" a reference ends in ("track_id" -> track).
QStringList labelWords(const QString& key, QString* unit)
{
	QStringList words = wordsOf(key);
	*unit = words.isEmpty() ? QString() : unitWords().value(words.last().toLower());
	const bool reference = !words.isEmpty() && words.last().toLower() == QStringLiteral("id");
	if (words.size() > 1 && (!unit->isEmpty() || reference)) { words.removeLast(); }
	else if (words.size() == 1) { unit->clear(); }  // "tempo" is the field, its unit beside the editor
	return words;
}

const control::ReversibilityEntry* reversibilityOf(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

PathRole declaredRole(const QString& declared)
{
	if (declared == QStringLiteral("output")) { return PathRole::SaveFile; }
	if (declared == QStringLiteral("folder")) { return PathRole::OpenFolder; }
	if (declared == QStringLiteral("output_folder")) { return PathRole::SaveFolder; }
	return PathRole::OpenFile;
}

//! Whether a path argument is one the command WRITES: by its key ("out_dir", "report") or its verb.
bool writesPath(const QString& id, const QString& key)
{
	static const QSet<QString> outputKeys{QStringLiteral("out"), QStringLiteral("output"), QStringLiteral("destination"),
		QStringLiteral("dest"), QStringLiteral("report"), QStringLiteral("export"), QStringLiteral("save")};
	static const QSet<QString> outputVerbs{QStringLiteral("save"), QStringLiteral("export"), QStringLiteral("render"),
		QStringLiteral("bounce"), QStringLiteral("write"), QStringLiteral("stems"), QStringLiteral("backup"),
		QStringLiteral("archive"), QStringLiteral("collect"), QStringLiteral("freeze"), QStringLiteral("consolidate"),
		QStringLiteral("record"), QStringLiteral("extract"), QStringLiteral("generate")};
	for (const QString& word : wordsOf(key)) { if (outputKeys.contains(word.toLower())) { return true; } }
	for (const QString& word : wordsOf(id.section(QLatin1Char('.'), 1))) { if (outputVerbs.contains(word)) { return true; } }
	return false;
}

//! "tempo_map_add" -> add tempo map: a trailing action word goes first (not after a leading verb).
void putActionFirst(QStringList& words)
{
	if (words.size() > 1 && trailingActions().contains(words.last()) && !objectVerbs().contains(words.first()))
	{
		words.prepend(words.takeLast());
	}
}

//! A one-word verb that needs its object named ("remove" of the track group), unless the group IS the verb.
bool isBareVerb(const QStringList& words, const QString& group)
{
	return words.size() == 1 && objectVerbs().contains(words.first()) && words.first() != group;
}

} // namespace

QString commandName(const QString& id)
{
	if (const auto named = nameOverrides().find(id); named != nameOverrides().end()) { return tr(named.value()); }
	const QString group = id.section(QLatin1Char('.'), 0, 0);
	QStringList words = wordsOf(id.section(QLatin1Char('.'), 1));
	if (words.isEmpty()) { return commandCategory(id); }
	if (words == QStringList{QStringLiteral("get"), QStringLiteral("state")})
	{
		return tr("Show %1 State").arg(commandCategory(id));
	}
	putActionFirst(words);
	const bool bare = isBareVerb(words, group);
	if (words.first() == QStringLiteral("get")) { words.first() = QStringLiteral("show"); }
	return bare ? titleCase(words) + QLatin1Char(' ') + commandCategory(id) : titleCase(words);
}

QString commandCategory(const QString& id)
{
	return titleCase(wordsOf(id.section(QLatin1Char('.'), 0, 0)));
}

QString commandDescription(const QString& id)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	return command != nullptr ? command->description : QString();
}

bool commandIsIrreversible(const QString& id)
{
	const control::ReversibilityEntry* entry = reversibilityOf(id);
	return entry != nullptr && entry->cls != control::ReversibilityClass::NotMutating && !entry->reversible;
}

QString commandUndoWarning(const QString& id)
{
	if (!commandIsIrreversible(id)) { return QString(); }
	// A manual-fallback row is partly undoable (plugin.load of an effect is, of an instrument is not), so
	// it is told as "may not", never as the flat "cannot" a verified irreversible command earns.
	return reversibilityOf(id)->cls == control::ReversibilityClass::Irreversible ? tr("This cannot be undone.")
		: tr("Undo may not fully reverse this.");
}

QJsonObject commandProperties(const QString& id)
{
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	return command != nullptr ? command->argsSchema.value(QStringLiteral("properties")).toObject() : QJsonObject();
}

QStringList commandRequired(const QString& id)
{
	QStringList out;
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	if (command == nullptr) { return out; }
	for (const QJsonValue& key : command->argsSchema.value(QStringLiteral("required")).toArray()) { out.append(key.toString()); }
	return out;
}

QString fieldLabel(const QString& key, const QJsonObject& spec)
{
	const QString title = spec.value(QStringLiteral("title")).toString();
	if (!title.isEmpty()) { return title; }
	QString unit;
	const QString label = sentenceCase(labelWords(key, &unit));
	return unit.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, unit);
}

QString fieldUnit(const QString& key, const QJsonObject& spec)
{
	const QString declared = spec.value(QStringLiteral("unit")).toString();
	if (!declared.isEmpty()) { return declared; }
	const QString type = schemaType(spec);
	if (!spec.isEmpty() && type != QStringLiteral("integer") && type != QStringLiteral("number")) { return QString(); }
	const QStringList words = wordsOf(key);
	return words.isEmpty() ? QString() : unitWords().value(words.last().toLower());
}

QString fieldRange(const QJsonObject& spec)
{
	const QJsonValue low = spec.value(QStringLiteral("minimum"));
	const QJsonValue high = spec.value(QStringLiteral("maximum"));
	if (low.isDouble() && high.isDouble()) { return tr("%1 to %2").arg(numberText(low.toDouble()), numberText(high.toDouble())); }
	if (low.isDouble()) { return tr("%1 or more").arg(numberText(low.toDouble())); }
	if (high.isDouble()) { return tr("up to %1").arg(numberText(high.toDouble())); }
	return QString();
}

QString fieldHelp(const QString& id, const QString& key, const QJsonObject& spec, bool required)
{
	QStringList parts;
	const QString description = spec.value(QStringLiteral("description")).toString();
	if (!description.isEmpty()) { parts.append(description); }
	const QString range = fieldRange(spec);
	const QString unit = fieldUnit(key, spec);
	if (!range.isEmpty()) { parts.append(tr("Range: %1.").arg(unit.isEmpty() ? range : range + QLatin1Char(' ') + unit)); }
	const QString role = pathRoleText(pathRole(id, key, spec));
	if (!role.isEmpty()) { parts.append(role); }
	const QJsonValue fallback = spec.value(QStringLiteral("default"));
	if (!fallback.isUndefined()) { parts.append(tr("Default: %1.").arg(choiceLabel(fallback))); }
	if (required) { parts.append(tr("Required.")); }
	else { parts.append(tr("Optional: leave it blank and the command uses its own default.")); }
	return parts.join(QLatin1Char(' '));
}

PathRole pathRole(const QString& id, const QString& key, const QJsonObject& spec)
{
	const QString declared = spec.value(QStringLiteral("path")).toString();
	if (!declared.isEmpty()) { return declaredRole(declared); }
	if (!ScriptCommandBridge::isPathArgument(id, key)) { return PathRole::None; }
	static const QSet<QString> folderWords{QStringLiteral("dir"), QStringLiteral("folder"), QStringLiteral("directory"),
		QStringLiteral("root")};
	const QStringList words = wordsOf(key);
	const bool folder = !words.isEmpty() && folderWords.contains(words.last().toLower());
	const bool writes = writesPath(id, key);
	if (folder) { return writes ? PathRole::SaveFolder : PathRole::OpenFolder; }
	return writes ? PathRole::SaveFile : PathRole::OpenFile;
}

QString pathRoleText(PathRole role)
{
	switch (role)
	{
	case PathRole::OpenFile: return tr("Input: an existing file this command reads.");
	case PathRole::SaveFile: return tr("Output: the file this command writes (it is created or replaced).");
	case PathRole::OpenFolder: return tr("Input: an existing folder this command reads.");
	case PathRole::SaveFolder: return tr("Output: the folder this command writes into.");
	case PathRole::None: break;
	}
	return QString();
}

QString choiceLabel(const QJsonValue& value)
{
	if (value.isBool()) { return value.toBool() ? tr("On") : tr("Off"); }
	if (!value.isString()) { return valueText(value); }
	QString text = value.toString();
	text.replace(QLatin1Char('-'), QLatin1Char('_'));
	const QStringList words = wordsOf(text);
	return words.isEmpty() ? value.toString() : sentenceCase(words);
}

QString valueText(const QJsonValue& value)
{
	if (value.isString()) { return value.toString(); }
	if (value.isBool()) { return value.toBool() ? tr("yes") : tr("no"); }
	if (value.isDouble()) { return numberText(value.toDouble()); }
	if (value.isArray() && isPrimitiveArray(value.toArray()))
	{
		QStringList items;
		for (const QJsonValue& item : value.toArray()) { items.append(valueText(item)); }
		return items.join(QStringLiteral(", "));
	}
	if (value.isArray()) { return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)); }
	if (value.isObject()) { return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)); }
	return QString();
}

namespace
{

//! One result field as a summary line reads it: a nested list or record by its size, a number with its unit.
QString summaryValue(const QString& key, const QJsonValue& value)
{
	constexpr int MaxValueChars = 200;
	if (value.isArray() && !isPrimitiveArray(value.toArray()))
	{
		return QCoreApplication::translate("CommandMetadata", "%n item(s)", nullptr, static_cast<int>(value.toArray().size()));
	}
	if (value.isObject())
	{
		return QCoreApplication::translate("CommandMetadata", "%n value(s)", nullptr, static_cast<int>(value.toObject().size()));
	}
	const QString unit = value.isDouble() ? fieldUnit(key) : QString();
	QString text = unit.isEmpty() ? valueText(value) : valueText(value) + QLatin1Char(' ') + unit;
	if (text.size() > MaxValueChars) { text = text.left(MaxValueChars - 1) + QStringLiteral("…"); }
	return text;
}

} // namespace

QString resultSummary(const QJsonObject& result, int maxLines)
{
	QStringList lines;
	for (auto it = result.begin(); it != result.end(); ++it)
	{
		if (lines.size() >= maxLines) { lines.append(QStringLiteral("…")); break; }
		lines.append(QStringLiteral("%1: %2").arg(fieldLabel(it.key()), summaryValue(it.key(), it.value())));
	}
	return lines.isEmpty() ? tr("Done.") : lines.join(QLatin1Char('\n'));
}

bool confirmAction(QWidget* parent, const QString& label, const QString& target, const QString& caution)
{
	if (ControlRegistry::instance()->isHeadless() || getGUI() == nullptr) { return true; }
	const QString what = target.isEmpty() ? label : tr("%1 \"%2\"").arg(label, target);
	const QString question = caution.isEmpty() ? tr("%1 - are you sure?").arg(what)
		: tr("%1 - %2 Are you sure?").arg(what, caution);
	return QMessageBox::question(parent, label, question) == QMessageBox::Yes;
}

} // namespace lmms::gui
