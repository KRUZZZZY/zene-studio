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

#include "ControlRegistry.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "Song.h"

namespace lmms
{

namespace
{

QJsonObject schemaObject(QJsonObject properties, QJsonArray required = {})
{
	QJsonObject schema;
	schema.insert(QStringLiteral("type"), QStringLiteral("object"));
	schema.insert(QStringLiteral("properties"), std::move(properties));
	schema.insert(QStringLiteral("required"), std::move(required));
	schema.insert(QStringLiteral("additionalProperties"), false);
	return schema;
}

QJsonObject stringProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
}

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

// ---------------------------------------------------------------------------
// project.open
// ---------------------------------------------------------------------------

namespace
{

void registerProjectOpen(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.open");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("open");
	cmd.description = QStringLiteral("Load a project file into this running instance.");
	cmd.argsSchema = schemaObject({{QStringLiteral("path"), stringProperty()}}, {QStringLiteral("path")});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("track_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString path = args.value(QStringLiteral("path")).toString();
		if (!QFileInfo::exists(path))
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no such project file: %1").arg(path));
		}
		Song* song = Engine::getSong();
		song->loadProject(path);

		QJsonObject result;
		result.insert(QStringLiteral("file"), song->projectFileName());
		result.insert(QStringLiteral("track_count"), static_cast<int>(song->tracks().size()));
		result.insert(QStringLiteral("tempo"), song->getTempo());
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), QJsonObject());
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("UNIMPLEMENTED: reopen the previous project")}});
		// Loading a project replaces the whole session; the engine keeps no
		// pre-load snapshot, so this transaction is documented, not reversible.
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot only: no pre-load project snapshot is kept in this slice"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.save
// ---------------------------------------------------------------------------

void registerProjectSave(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.save");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("save");
	cmd.description = QStringLiteral("Save the session. With no path, saves over the project's own file.");
	cmd.argsSchema = schemaObject({{QStringLiteral("path"), stringProperty()}});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("saved"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		QString target = args.value(QStringLiteral("path")).toString();
		if (target.isEmpty()) { target = song->projectFileName(); }
		if (target.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("no 'path' given and the session has no project file yet"));
		}
		if (!song->saveProjectFile(target))
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the engine refused to write %1").arg(target));
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), target);
		result.insert(QStringLiteral("saved"), true);
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("file"), song->projectFileName()}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("UNIMPLEMENTED: restore the previous file revision")}});
		// Song::saveProjectFile() writes in place; the engine keeps no previous
		// revision, so SPEC A16's file-level fallback is not available here.
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("snapshot only: the engine keeps no previous file revision"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.get_state
// ---------------------------------------------------------------------------

void registerProjectGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.get_state");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Project file, modified flag, tempo and track count.");
	cmd.argsSchema = schemaObject({});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("modified"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("track_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
	});
	cmd.handler = [](const QJsonObject&) {
		Song* song = Engine::getSong();
		QJsonObject result;
		result.insert(QStringLiteral("file"), song->projectFileName());
		result.insert(QStringLiteral("modified"), song->isModified());
		result.insert(QStringLiteral("tempo"), song->getTempo());
		result.insert(QStringLiteral("track_count"), static_cast<int>(song->tracks().size()));
		result.insert(QStringLiteral("playing"), song->isPlaying());
		QJsonArray trackIds;
		for (int i = 0; i < static_cast<int>(song->tracks().size()); ++i)
		{
			trackIds.append(control::trackId(i));
		}
		result.insert(QStringLiteral("tracks"), trackIds);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// render.render
// ---------------------------------------------------------------------------

//! Validates the render args; empty string means they are usable.
QString parseRenderArgs(const QJsonObject& args, QString* out, QString* formatName,
	ProjectRenderer::ExportFileFormat* format)
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
	return QString();
}

//! Runs the shipped CLI render path as a child process.
bool runCliRender(const QString& projectPath, const QString& out, const QString& formatName, int* exitCode)
{
	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(QProcess::nullDevice());
	renderer.start(QCoreApplication::applicationFilePath(),
		{QStringLiteral("render"), projectPath, QStringLiteral("-o"), out,
			QStringLiteral("-f"), formatName, QStringLiteral("-s"), QStringLiteral("44100")});
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
	const QString invalid = parseRenderArgs(args, &out, &formatName, &format);
	if (!invalid.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, invalid);
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
	const bool finished = runCliRender(tempProject, out, formatName, &exitCode);
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
	result.insert(QStringLiteral("sample_rate"), 44100);
	result.insert(QStringLiteral("bytes"), static_cast<qint64>(info.size()));
	const bool wave = format == ProjectRenderer::ExportFileFormat::Wave;
	result.insert(QStringLiteral("frames"), wave ? wavFrameCount(out) : -1);
	result.insert(QStringLiteral("sha256"), sha256OfFile(out));
	return ControlResult::success(result);
}

void registerRenderRender(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("render.render");
	cmd.group = QStringLiteral("render");
	cmd.verb = QStringLiteral("render");
	cmd.description = QStringLiteral("Render the current session to a file and return its hash (headless).");
	cmd.argsSchema = schemaObject(
		{{QStringLiteral("out"), stringProperty()},
			{QStringLiteral("format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("wav"), QStringLiteral("flac"),
					QStringLiteral("ogg"), QStringLiteral("mp3")}}}}},
		{QStringLiteral("out")});
	cmd.resultSchema = schemaObject({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("sha256"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return renderSession(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerProjectCommands(ControlRegistry& registry)
{
	registerProjectOpen(registry);
	registerProjectSave(registry);
	registerProjectGetState(registry);
	registerRenderRender(registry);
}

} // namespace lmms
