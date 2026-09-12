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

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectIds.h"
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
	cmd.argsSchema = objectSchema({{QStringLiteral("path"), stringProperty()}}, {QStringLiteral("path")});
	// SPEC A13: the load path must work with no display. A project that loads
	// with errors returns the per-item list here instead of stopping on the
	// "LMMS Error report" box, which in an agent instance nobody can click
	// (task #625).
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("track_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("loaded_with_errors"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		{QStringLiteral("error_count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("errors"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), objectSchema({
				{QStringLiteral("message"), stringProperty()},
				{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}})}}},
		// The id upgrade, reported rather than silent (SPEC-stable-ids.md 7, Q1).
		// `ids_assigned` counts the objects this load had to give an id to because
		// the file carried none (plus any duplicate-id repair); `format_upgraded`
		// is that count above zero. The upgrade is content-preserving and happens
		// in memory only - the file changes on the next project.save, which is
		// exactly why the caller is told.
		{QStringLiteral("ids_assigned"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		{QStringLiteral("format_upgraded"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
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
		// SPEC A16: the displaced session is NOT snapshotted, so the record says
		// what was replaced instead of pretending an inverse exists.
		const QString previousFile = song->projectFileName();
		const QString previousSha = previousFile.isEmpty() ? QString() : sha256OfFile(previousFile);
		song->loadProject(path);

		// A refused file (unparseable, or carrying local plugin paths) leaves
		// the session as it was; the reason is a typed refusal, not a modal.
		const QString refusal = song->loadRefusal();
		if (!refusal.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1: %2").arg(path, refusal));
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), song->projectFileName());
		result.insert(QStringLiteral("track_count"), static_cast<int>(song->tracks().size()));
		result.insert(QStringLiteral("tempo"), song->getTempo());
		// The per-item load errors, sorted so the answer is reproducible: each
		// entry is what failed (the sample or plugin path, in the message) and
		// why (the same sentence the "LMMS Error report" box used to show).
		QJsonArray errors;
		QStringList messages = song->errors().keys();
		messages.sort();
		for (const QString& message : messages)
		{
			errors.append(QJsonObject{{QStringLiteral("message"), message},
				{QStringLiteral("count"), song->errors().value(message)}});
		}
		result.insert(QStringLiteral("errors"), errors);
		result.insert(QStringLiteral("error_count"), errors.size());
		result.insert(QStringLiteral("loaded_with_errors"), !errors.isEmpty());
		// SPEC-stable-ids.md 7 Q1 (owner decision): a legacy file's first load is
		// a content-preserving one-time id UPGRADE, and the caller must be told
		// so rather than discover it when the file changes on the next save.
		const int idsAssigned = ProjectIds::loadAssignments();
		result.insert(QStringLiteral("ids_assigned"), idsAssigned);
		result.insert(QStringLiteral("format_upgraded"), idsAssigned > 0);
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("previous_file"), previousFile},
				{QStringLiteral("previous_sha256"), previousSha}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("project.open")},
				{QStringLiteral("args"), QJsonObject{{QStringLiteral("path"), previousFile}}}});
		// Loading a project replaces the whole session; the engine keeps no
		// pre-load snapshot, so this transaction is documented, not reversible -
		// and control.undo says exactly that instead of undoing an older step.
		transaction.insert(QStringLiteral("reversible"), false);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("none: the displaced session is not snapshotted; only the file path "
				"and its hash are recorded, so the caller can see what it replaced. UNSAVED "
				"changes to the previous session are lost"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

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
	cmd.argsSchema = objectSchema(
		{{QStringLiteral("out"), stringProperty()},
			{QStringLiteral("format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
				{QStringLiteral("enum"), QJsonArray{QStringLiteral("wav"), QStringLiteral("flac"),
					QStringLiteral("ogg"), QStringLiteral("mp3")}}}}},
		{QStringLiteral("out")});
	cmd.resultSchema = objectSchema({
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
	registerRenderRender(registry);
	registerProjectFilesCommands(registry);
}

} // namespace lmms
