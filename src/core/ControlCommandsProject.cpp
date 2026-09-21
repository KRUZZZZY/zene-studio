/*
 * ControlCommandsProject.cpp - the project.* and render.render commands.
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

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QtEndian>

#include "ControlExportPresetSupport.h"
#include "ControlRegistry.h"

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ExportRenderSettings.h"
#include "OutputSettings.h"
#include "ProjectRevisions.h"
#include "ProjectRenderer.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

bool formatFromName(const QString& name, ProjectRenderer::ExportFileFormat* format)
{
	if (name == QLatin1String("wav")) { *format = ProjectRenderer::ExportFileFormat::Wave; return true; }
	if (name == QLatin1String("flac")) { *format = ProjectRenderer::ExportFileFormat::Flac; return true; }
	if (name == QLatin1String("ogg")) { *format = ProjectRenderer::ExportFileFormat::Ogg; return true; }
	if (name == QLatin1String("mp3")) { *format = ProjectRenderer::ExportFileFormat::MP3; return true; }
	return false;
}

//! What a RIFF/WAVE header tells us. `-1`/`0` mean "not present".
struct WavInfo
{
	qint64 dataOffset = -1;      //!< file offset where the sample data starts
	qint64 declaredDataSize = -1;
	qint64 bytesPerFrame = 0;
};

void readFmtChunk(const QByteArray& fmt, WavInfo* info)
{
	if (fmt.size() < 16) { return; }
	const int channels = qFromLittleEndian<quint16>(fmt.constData() + 2);
	const int bits = qFromLittleEndian<quint16>(fmt.constData() + 14);
	info->bytesPerFrame = static_cast<qint64>(channels) * (bits / 8);
}

//! Reads one RIFF chunk header; false at end of file.
bool readChunkHeader(QFile& file, QByteArray* id, quint32* size)
{
	const QByteArray header = file.read(8);
	if (header.size() < 8) { return false; }
	*id = header.left(4);
	*size = qFromLittleEndian<quint32>(header.constData() + 4);
	return true;
}

//! Seeks past the payload of an unknown chunk (RIFF pads odd sizes).
bool skipChunk(QFile& file, qint64 chunkStart, quint32 size)
{
	return file.seek(chunkStart + 8 + size + (size % 2));
}

//! True when the file starts with a RIFF/WAVE header.
bool hasRiffHeader(QFile& file)
{
	const QByteArray header = file.read(12);
	return header.size() >= 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WAVE";
}

//! Consumes a fmt chunk payload and any padding byte.
void consumeFmtChunk(QFile& file, quint32 size, WavInfo* info)
{
	readFmtChunk(file.read(size), info);
	if (size % 2 == 1) { file.seek(file.pos() + 1); }
}

WavInfo readWavInfo(const QString& path)
{
	WavInfo info;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return info; }
	if (!hasRiffHeader(file)) { return info; }

	while (true)
	{
		const qint64 chunkStart = file.pos();
		QByteArray id;
		quint32 size = 0;
		if (!readChunkHeader(file, &id, &size)) { break; }
		if (id == "data")
		{
			info.dataOffset = file.pos();
			info.declaredDataSize = static_cast<qint64>(size);
			break;
		}
		if (id == "fmt ")
		{
			consumeFmtChunk(file, size, &info);
			continue;
		}
		if (!skipChunk(file, chunkStart, size)) { break; }
	}
	return info;
}

//! Frames in a WAV file, from the real file size rather than the RIFF data size
//! (the writer may still be finalising the header when the file is read).
qint64 wavFrameCount(const QString& path)
{
	const WavInfo info = readWavInfo(path);
	if (info.dataOffset < 0) { return -1; }
	QFileInfo fileInfo(path);
	const qint64 audioBytes = fileInfo.size() - info.dataOffset;
	const qint64 stride = info.bytesPerFrame > 0 ? info.bytesPerFrame : 4;
	return audioBytes > 0 ? audioBytes / stride : 0;
}

QString sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

} // namespace

namespace
{

// ---------------------------------------------------------------------------
// project.save
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// project.get_state
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// render.render
// ---------------------------------------------------------------------------

//! Validates the render args; empty string means they are usable.
QString parseRenderArgs(const QJsonObject& args, QString* out, QString* formatName,
	ProjectRenderer::ExportFileFormat* format, ControlExportPreset* settings)
{
	*out = args.value(QStringLiteral("out")).toString();
	if (!out->startsWith(QLatin1Char('/')))
	{
		return QStringLiteral("'out' must be an absolute path");
	}
	*formatName = args.value(QStringLiteral("format")).toString(QStringLiteral("wav"));
	if (!formatFromName(*formatName, format))
	{
		return QStringLiteral("unsupported format '%1'").arg(*formatName);
	}
	// The settings this render is started with: the preset that was applied
	// through export.preset_apply, or the render path's own defaults (44100 Hz,
	// 16-bit, joint stereo) when none was. Read HERE and passed to the child as
	// its own command line, so an apply changes the render's OPTIONS rather than
	// its renderer.
	*settings = ControlExportPresetSettings::effective();
	return QString();
}

/*! The render RANGE (feature row 71): both ends in ticks, or neither.
 *
 *  There is deliberately no "half a range" form: one end alone is a span with a
 *  missing side, and a caller that gets it wrong is told which end it is missing
 *  rather than being handed a render of something it did not select. The refusal
 *  is typed and happens before the session is serialised, so a bad range writes
 *  no temp project, spawns no child and leaves no file behind.
 */
QString parseRenderRange(const QJsonObject& args, int* begin, int* end)
{
	const bool hasBegin = args.contains(QStringLiteral("start_ticks"));
	const bool hasEnd = args.contains(QStringLiteral("end_ticks"));
	if (!hasBegin && !hasEnd)
	{
		*begin = -1;
		*end = -1;
		return QString();
	}
	if (hasBegin != hasEnd)
	{
		return QStringLiteral("a render range needs BOTH start_ticks and end_ticks ('%1' was "
			"given alone); a selection render is a span, not a start")
			.arg(hasBegin ? QStringLiteral("start_ticks") : QStringLiteral("end_ticks"));
	}
	*begin = args.value(QStringLiteral("start_ticks")).toInt();
	*end = args.value(QStringLiteral("end_ticks")).toInt();
	if (*begin < 0 || *end < 0)
	{
		return QStringLiteral("start_ticks and end_ticks are positions in the song, so they "
			"cannot be negative (got %1 and %2)").arg(*begin).arg(*end);
	}
	if (*end <= *begin)
	{
		return QStringLiteral("empty render range: start_ticks %1 is not before end_ticks %2")
			.arg(*begin).arg(*end);
	}
	return QString();
}

//! Runs the shipped CLI render path as a child process. The loudness report
//! (feature row 24) travels with it when the process-wide selection asks for
//! one: the child is a fresh process whose own ExportRenderSettings start at
//! the defaults, so a report that was turned on through
//! export.set_loudness_report would otherwise be silently absent from the file
//! the agent just rendered. Measure-only: the flag changes the report and the
//! sidecar, never the audio. The applied preset (feature row 70) and the render
//! range (row 71) travel the same way, as command-line arguments: the preset
//! through controlExportPresetRenderArgs (which carries the `-s <rate>` this
//! call used to pass as a literal), the range through --range-start/--range-end.
bool runCliRender(const QString& projectPath, const QString& out, const QString& formatName,
	const ControlExportPreset& settings, int rangeBegin, int rangeEnd, int* exitCode)
{
	QStringList arguments{QStringLiteral("render"), projectPath, QStringLiteral("-o"), out,
		QStringLiteral("-f"), formatName};
	arguments += controlExportPresetRenderArgs(settings);
	if (rangeBegin >= 0)
	{
		arguments << QStringLiteral("--range-start") << QString::number(rangeBegin)
			<< QStringLiteral("--range-end") << QString::number(rangeEnd);
	}
	if (ExportRenderSettings::loudnessReport())
	{
		arguments << QStringLiteral("--loudness-report");
	}

	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(QProcess::nullDevice());
	renderer.start(QCoreApplication::applicationFilePath(), arguments);
	const bool finished = renderer.waitForStarted(30000) && renderer.waitForFinished(600000);
	if (!finished) { renderer.kill(); }
	*exitCode = finished ? renderer.exitCode() : -1;
	return finished;
}

ControlResult renderSession(const QJsonObject& args)
{
	QString out;
	QString formatName;
	ProjectRenderer::ExportFileFormat format = ProjectRenderer::ExportFileFormat::Wave;
	ControlExportPreset settings;
	const QString invalid = parseRenderArgs(args, &out, &formatName, &format, &settings);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
	}
	int rangeBegin = -1;
	int rangeEnd = -1;
	const QString badRange = parseRenderRange(args, &rangeBegin, &rangeEnd);
	if (!badRange.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, badRange);
	}
	if (Engine::getSong()->isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the session is empty: there is nothing to render"));
	}

	// Render through the same CLI path the product already ships
	// (`lmms render <file> -o <out>`), in a child process. Rendering in-process
	// would drive this instance's audio engine (ProjectRenderer calls
	// audioEngine()->startProcessing() / stopProcessing(), which owns the device
	// thread) and that leaves the running instance's event loop unable to quit
	// cleanly. The session is serialised first, so the render is the audio truth
	// of exactly the state an agent has built.
	const QString tempProject = QDir::tempPath() +
		QStringLiteral("/zene-render-%1-%2.mmp").arg(QCoreApplication::applicationPid())
			.arg(QDateTime::currentMSecsSinceEpoch());
	if (!Engine::getSong()->saveProjectFile(tempProject))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not serialise the session for rendering"));
	}

	int exitCode = -1;
	const bool finished = runCliRender(tempProject, out, formatName, settings, rangeBegin,
		rangeEnd, &exitCode);
	QFile::remove(tempProject);

	QFileInfo info(out);
	if (!finished || exitCode != 0 || !info.exists() || info.size() == 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the render failed (exit %1); the session is unchanged").arg(exitCode));
	}

	QJsonObject result;
	result.insert(QStringLiteral("path"), out);
	result.insert(QStringLiteral("format"), formatName);
	result.insert(QStringLiteral("sample_rate"), static_cast<int>(settings.sampleRate));
	result.insert(QStringLiteral("bit_depth"),
		QString::fromLatin1(controlExportPresetBitDepthName(settings.bitDepth)));
	result.insert(QStringLiteral("stereo_mode"),
		QString::fromLatin1(controlExportPresetStereoModeName(settings.stereoMode)));
	result.insert(QStringLiteral("applied_preset"),
		ControlExportPresetSettings::activeName());
	result.insert(QStringLiteral("bytes"), static_cast<qint64>(info.size()));
	const bool wave = format == ProjectRenderer::ExportFileFormat::Wave;
	result.insert(QStringLiteral("frames"), wave ? wavFrameCount(out) : -1);
	// The range is reported back as it was applied, so a caller compares the
	// frames it got against the span it asked for without a second round trip.
	// `range` is null for a whole-project render, which is the render this
	// command has always been.
	if (rangeBegin >= 0)
	{
		QJsonObject range;
		range.insert(QStringLiteral("start_ticks"), rangeBegin);
		range.insert(QStringLiteral("end_ticks"), rangeEnd);
		range.insert(QStringLiteral("ticks"), rangeEnd - rangeBegin);
		result.insert(QStringLiteral("range"), range);
	}
	else
	{
		result.insert(QStringLiteral("range"), QJsonValue());
	}
	result.insert(QStringLiteral("sha256"), sha256OfFile(out));
	return ControlResult::success(result);
}

void registerRenderRender(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("render.render");
	cmd.group = QStringLiteral("render");
	cmd.verb = QStringLiteral("render");
	cmd.description = QStringLiteral("Render the current session to a file and return its hash "
		"(headless). Whole project by default; pass start_ticks and end_ticks TOGETHER to render "
		"only that span of the song (a selection), in which case the span is rendered exactly - "
		"no tail bar and no loop repetition - and the reply reports the range and the frame "
		"count. The render is started with the export settings in force: the preset applied "
		"through export.preset_apply (sample rate, bit depth, stereo mode), or the render path's "
		"own defaults when none is applied. Runs in a CHILD process, so the declared bound "
		"applies: this surface does not answer - not even control.ping - until it finishes "
		"(docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("out"), stringProperty()},
			{QStringLiteral("format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("wav"), QStringLiteral("flac"),
					QStringLiteral("ogg"), QStringLiteral("mp3")}}}},
			{QStringLiteral("start_ticks"), tickProperty()},
			{QStringLiteral("end_ticks"), tickProperty()}},
		{QStringLiteral("out")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("bit_depth"), stringProperty()},
		{QStringLiteral("stereo_mode"), stringProperty()},
		{QStringLiteral("applied_preset"), stringProperty()},
		{QStringLiteral("range"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return renderSession(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerProjectCommands(ControlRegistry& registry)
{
	registerRenderRender(registry);
	registerProjectFilesCommands(registry);
}

} // namespace lmms
