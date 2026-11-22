/*
 * ControlCommandsRenderSections.cpp - `render.sections`, the batch section export
 *                                      (0.5.0 L12, Q050-009 / feature row 90).
 *
 * WHAT IT IS. The song's named sections (section.add/set/list/remove) are a
 * documentation of the arrangement: a verse, a chorus, a bridge, each a span of
 * ticks with a name. This verb renders EACH chosen section to its OWN file,
 * named after the section, bounded to the section's own start and end ticks - so
 * a finished arrangement becomes one file per part in a single call instead of
 * one render.render per section a caller has to name and bound itself.
 *
 * HOW IT RENDERS, and why that is not a second renderer: each section is
 * rendered through render.render, invoked with the section's ticks as
 * start_ticks/end_ticks. render.render already serialises the session to a
 * temporary project, runs the shipped CLI child and reports the written file;
 * this command is the BATCH over that one path (the same way section.select
 * drives range.set). The export settings in force reach every section because
 * render.render reads them - a preset applied through export.preset_apply sets
 * every section file's rate and bit depth.
 *
 * THE DECLARED BOUND. Every render runs in a child process and the dispatch
 * thread does not answer until each finishes, exactly as render.render and
 * render.stems declare (docs/RENDER-CHILD-WAIT.md, docs/KNOWN-LIMITATIONS.md);
 * here the wait is repeated once per section. The reply reports one entry per
 * section with the file it wrote, so a caller can check what landed without a
 * second round trip.
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

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <utility>

#include "ArrangementSections.h"
#include "ControlCommandDef.h"
#include "ControlExportPresetSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

/*! True for the DOS device names Windows will not take as a file name: CON, PRN,
 *  AUX, NUL, COM1-9 and LPT1-9, in any case, with or without an extension
 *  ("nul.wav" and "Nul.Mix" are as reserved as "NUL"). Decided the same on every
 *  platform: a project exported on Linux must give the file names a Windows
 *  collaborator can also create.
 */
bool isWindowsReservedStem(const QString& stem)
{
	static const QStringList reserved{QStringLiteral("CON"), QStringLiteral("PRN"),
		QStringLiteral("AUX"), QStringLiteral("NUL"), QStringLiteral("COM1"), QStringLiteral("COM2"),
		QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"), QStringLiteral("COM6"),
		QStringLiteral("COM7"), QStringLiteral("COM8"), QStringLiteral("COM9"), QStringLiteral("LPT1"),
		QStringLiteral("LPT2"), QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
		QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"), QStringLiteral("LPT9")};
	const int dot = stem.indexOf(QLatin1Char('.'));
	const QString base = (dot < 0 ? stem : stem.left(dot)).trimmed().toUpper();
	return reserved.contains(base);
}

//! The name with the characters a filesystem will not take removed, trimmed, and
//! without leading dots (never a hidden file, never a path that could escape).
QString cleanedSectionName(const QString& name)
{
	// The characters a filesystem will not take, in one place rather than a chain of comparisons.
	static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
	QString stem;
	for (const QChar c : name)
	{
		if (c.unicode() >= 0x20 && !forbidden.contains(c)) { stem.append(c); }
	}
	stem = stem.trimmed();
	while (stem.startsWith(QLatin1Char('.'))) { stem.remove(0, 1); }
	return stem.trimmed();
}

//! A trailing run of dots and spaces (which Windows silently drops) becomes one
//! "_"; a name of nothing else becomes empty.
QString withoutTrailingDotsAndSpaces(QString stem)
{
	if (!stem.endsWith(QLatin1Char('.')) && !stem.endsWith(QLatin1Char(' '))) { return stem; }
	while (stem.endsWith(QLatin1Char('.')) || stem.endsWith(QLatin1Char(' '))) { stem.chop(1); }
	return stem.isEmpty() ? stem : stem + QLatin1Char('_');
}

/*! A section's name as a file name. The section's own name is used, with the
 *  characters a filesystem will not take removed; a name that is empty once
 *  cleaned falls back to the section's id (sec-<n>). Never a path separator and
 *  never a leading dot, so a section name can never escape the output directory.
 *  Portable by construction (no host-OS branch, the same name everywhere):
 *  trailing dots and spaces become one "_", and a Windows-reserved base name
 *  (CON, NUL, COM1, ... even with an extension) gets a "_" appended to that base
 *  ("CON" -> "CON_", "nul.mix" -> "nul_.mix").
 */
QString sectionFileStem(const ArrangementSection& section)
{
	QString stem = withoutTrailingDotsAndSpaces(cleanedSectionName(section.name));
	if (stem.isEmpty()) { stem = QStringLiteral("sec-%1").arg(section.id); }
	if (isWindowsReservedStem(stem))
	{
		const int dot = stem.indexOf(QLatin1Char('.'));
		stem.insert(dot < 0 ? stem.size() : dot, QLatin1Char('_'));
	}
	return stem;
}

//! The key two file names are compared by: the canonical caseless form, NFD, then case
//! folding, then NFD again (the Unicode canonical caseless match: folding can produce
//! characters that decompose again, so one normalisation on either side is not enough).
//! "Intro" and "intro" (one file on Windows and macOS), and a precomposed "\u015b" and
//! "\u017f" + combining acute (the same name to APFS), count as the same name on every
//! platform. Qt's fold is the simple one, so the full folds that change the length are
//! applied by hand: sharp s and the Latin ligatures.
QString sectionFileKey(const QString& stem)
{
	QString key = stem.normalized(QString::NormalizationForm_D).toCaseFolded();
	static const std::pair<QChar, const char*> fullFolds[] = {
		{QChar(0x00DF), "ss"}, {QChar(0xFB00), "ff"}, {QChar(0xFB01), "fi"}, {QChar(0xFB02), "fl"},
		{QChar(0xFB03), "ffi"}, {QChar(0xFB04), "ffl"}, {QChar(0xFB05), "st"}, {QChar(0xFB06), "st"}};
	for (const auto& [from, to] : fullFolds) { key.replace(from, QLatin1String(to)); }
	return key.normalized(QString::NormalizationForm_D);
}

//! One file name per section, made unique so two sections with the same name -
//! or names that differ only by case - cannot overwrite each other: the second
//! gets "-2", the third "-3", and so on.
QStringList sectionFileStems(const QVector<ArrangementSection>& sections)
{
	QStringList stems;
	QSet<QString> used;
	for (const ArrangementSection& section : sections)
	{
		const QString base = sectionFileStem(section);
		QString stem = base;
		for (int n = 2; used.contains(sectionFileKey(stem)); ++n)
		{
			stem = QStringLiteral("%1-%2").arg(base).arg(n);
		}
		used.insert(sectionFileKey(stem));
		stems.append(stem);
	}
	return stems;
}

//! The first generated output path that already holds something, or empty when every path is free. A
//! symbolic link counts even when it dangles: the writer opens the path and would follow the link.
QString existingOutputPath(const QString& directory, const QStringList& stems, const QString& extension)
{
	const QDir dir(directory);
	for (const QString& stem : stems)
	{
		const QString path = dir.filePath(stem + extension);
		const QFileInfo info(path);
		if (info.exists() || info.isSymLink()) { return path; }
	}
	return QString();
}

//! The sections this call renders: every section, or the named ones in the
//! order they were asked for. A named id the song does not hold is a typed
//! not_found here, before any render is started.
QString sectionArgError(const QJsonObject& args, QVector<ArrangementSection>* chosen)
{
	const QJsonValue requested = args.value(QStringLiteral("sections"));
	if (requested.isUndefined() || requested.isNull())
	{
		*chosen = ArrangementSections::instance().sections();
		return QString();
	}
	if (!requested.isArray())
	{
		return QStringLiteral("'sections' is an array of section ids (sec-<n>), or left out for "
			"every section");
	}
	ArrangementSections& store = ArrangementSections::instance();
	for (const QJsonValue& value : requested.toArray())
	{
		const QString id = value.toString();
		bool ok = false;
		const int index = id.startsWith(QStringLiteral("sec-")) ? id.mid(4).toInt(&ok) : -1;
		const ArrangementSection* found = ok ? store.find(index) : nullptr;
		if (found == nullptr)
		{
			return QStringLiteral("no section '%1' in the song").arg(id);
		}
		chosen->append(*found);
	}
	return QString();
}

//! Validates the directory and format arguments; an empty string means usable.
QString parseSectionArgs(const QJsonObject& args, QString* directory, QString* formatName)
{
	*directory = args.value(QStringLiteral("out")).toString();
	if (!QFileInfo(*directory).isAbsolute())
	{
		return QStringLiteral("'out' must be an absolute directory path (a section export writes "
			"many files, so it never guesses a destination)");
	}
	if (QFileInfo(*directory).exists() && !QFileInfo(*directory).isDir())
	{
		return QStringLiteral("'out' names an existing file, not a directory");
	}
	*formatName = args.value(QStringLiteral("format")).toString(QStringLiteral("wav"));
	if (!controlRenderableFormats().contains(*formatName))
	{
		return QStringLiteral("unsupported format '%1'").arg(*formatName);
	}
	return QString();
}

ControlResult renderSections(const QJsonObject& args)
{
	QString directory;
	QString formatName;
	const QString invalid = parseSectionArgs(args, &directory, &formatName);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}
	if (Engine::getSong()->isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the session is empty: there is nothing to export"));
	}

	QVector<ArrangementSection> chosen;
	const QString badSections = sectionArgError(args, &chosen);
	if (!badSections.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, badSections);
	}
	if (chosen.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the song has no sections; add one with section.add before exporting"));
	}

	if (!QDir().mkpath(directory))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot create the output directory %1").arg(directory));
	}

	const QStringList stems = sectionFileStems(chosen);
	const QString extension = QStringLiteral(".") + formatName;
	const QString existing = existingOutputPath(directory, stems, extension);
	if (!existing.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already exists: render.sections refuses the whole batch before it renders "
				"anything; remove that file or choose another 'out' directory").arg(existing));
	}
	QJsonArray files;
	for (int i = 0; i < chosen.size(); ++i)
	{
		const ArrangementSection& section = chosen.at(i);
		const QString out = QDir(directory).filePath(stems.at(i) + extension);
		const ControlResult rendered = ControlRegistry::instance()->invoke(
			QStringLiteral("render.render"),
			{{QStringLiteral("out"), out}, {QStringLiteral("format"), formatName},
				{QStringLiteral("start_ticks"), static_cast<int>(section.start)},
				{QStringLiteral("end_ticks"), static_cast<int>(section.end)}});
		if (!rendered.ok)
		{
			return ControlResult::failure(rendered.errorKind,
				QStringLiteral("section '%1' (sec-%2, ticks %3..%4) failed to render: %5")
					.arg(section.name).arg(section.id).arg(section.start).arg(section.end)
					.arg(rendered.errorMessage));
		}
		QJsonObject entry;
		entry.insert(QStringLiteral("section"), QStringLiteral("sec-%1").arg(section.id));
		entry.insert(QStringLiteral("name"), section.name);
		entry.insert(QStringLiteral("start_ticks"), static_cast<int>(section.start));
		entry.insert(QStringLiteral("end_ticks"), static_cast<int>(section.end));
		entry.insert(QStringLiteral("path"), rendered.result.value(QStringLiteral("path")));
		entry.insert(QStringLiteral("frames"), rendered.result.value(QStringLiteral("frames")));
		entry.insert(QStringLiteral("sha256"), rendered.result.value(QStringLiteral("sha256")));
		files.append(entry);
	}

	const ControlExportPreset settings = ControlExportPresetSettings::effective();
	QJsonObject result;
	result.insert(QStringLiteral("directory"), directory);
	result.insert(QStringLiteral("format"), formatName);
	result.insert(QStringLiteral("count"), files.size());
	result.insert(QStringLiteral("files"), files);
	result.insert(QStringLiteral("sample_rate"), static_cast<int>(settings.sampleRate));
	result.insert(QStringLiteral("bit_depth"),
		QString::fromLatin1(controlExportPresetBitDepthName(settings.bitDepth)));
	result.insert(QStringLiteral("applied_preset"), ControlExportPresetSettings::activeName());
	return ControlResult::success(result);
}

void registerRenderSections(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("render.sections");
	cmd.group = QStringLiteral("render");
	cmd.verb = QStringLiteral("sections");
	cmd.description = QStringLiteral("Export the song's named sections (section.add/set/list) as "
		"separate files: one file per section, named after the section, each rendered to exactly "
		"the section's own start..end ticks (no tail bar). The file name is the section's name "
		"with the characters a filesystem refuses (/ \\ : * ? \" < > |) removed and leading dots "
		"dropped; trailing dots and spaces become one '_', and a Windows-reserved base name (CON, "
		"PRN, AUX, NUL, COM1-9, LPT1-9, even with an extension) gets '_' appended to the base "
		"(CON -> CON_), the same on every platform. Names that are equal after Unicode canonical "
		"decomposition and case folding (Intro/intro) would be one file on Windows and macOS, so "
		"the later section gets '-2', then '-3' (intro-2.wav), skipping any name already taken; a "
		"name empty once cleaned is its id (sec-<n>). Before any render starts, every generated path is "
			"checked: a file, a directory or a symbolic link already there (even a dangling one) refuses the "
			"whole call with a typed refusal that names it, so no existing file is replaced and no part of "
			"the batch is written. 'out' is an absolute directory the files "
		"are written into; 'sections' picks specific sec-<n> ids (default: every section, in time "
		"order); 'format' is one of the build's renderable formats (default wav). Each file is "
		"rendered through render.render, so the export settings in force - the preset applied "
		"through export.preset_apply - set its rate and bit depth; the reply names them and one "
		"entry per written file. NOTE the declared bound: each render runs in a child process and "
		"this surface does not answer until it finishes (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("out"), stringProperty()},
		{QStringLiteral("format"), enumProperty(controlRenderableFormats())},
		{QStringLiteral("sections"), arrayProperty()},
	}, {QStringLiteral("out")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("directory"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("files"), arrayProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("bit_depth"), stringProperty()},
		{QStringLiteral("applied_preset"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return renderSections(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerRenderSectionsCommands(ControlRegistry& registry)
{
	registerRenderSections(registry);
}

} // namespace lmms
