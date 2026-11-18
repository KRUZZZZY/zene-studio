/*
 * ControlCommandsScoreExport.cpp - clip.export_musicxml
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
#include <QFileInfo>
#include <QSaveFile>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MeterModel.h"
#include "MidiClip.h"
#include "ScoreExport.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! ED-086: a MIDI clip as a MusicXML score file - for a notation program, or a player to read.
ControlResult exportMusicXml(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const auto* clip = dynamic_cast<const MidiClip*>(ref.clip);
	if (clip == nullptr) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is a MIDI clip")); }
	const QString path = args.value(QStringLiteral("path")).toString();
	// Case-insensitively: '.MUSICXML' and '.XML' are the same file types, and a
	// client that names its score 'score.XML' is not wrong.
	if (!path.endsWith(QLatin1String(".musicxml"), Qt::CaseInsensitive)
		&& !path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'path' is a .musicxml or .xml file"));
	}
	const MeterModel& meter = Engine::getSong()->getTimeSigModel();
	const QString title = clip->name().isEmpty() ? QFileInfo(path).completeBaseName() : clip->name();
	const QString score = musicXmlOf(*clip, meter.getNumerator(), meter.getDenominator(), title);
	// Written whole or not at all: a short write must not leave a truncated score reported as written.
	const QByteArray bytes = score.toUtf8();
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("could not write %1").arg(path));
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("path"), path},
		{QStringLiteral("notes"), static_cast<int>(clip->notes().size())}});
}

} // namespace

void registerScoreExportCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("clip.export_musicxml"), QStringLiteral("Write a MIDI clip as a MusicXML score to 'path' "
		"(.musicxml or .xml): one part in the song's time signature, quantised to a 64th, notes starting together as chords, "
		"long values tied across bar lines. Muted notes are left out. Writes a file; the project is untouched."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("path"), stringProperty()}},
		{QStringLiteral("clip"), QStringLiteral("path")}, false, exportMusicXml);
}

} // namespace lmms
