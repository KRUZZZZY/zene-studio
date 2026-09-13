/*
 * BounceInPlace.cpp - render a track's (or a region's) output to audio.
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

#include "BounceInPlace.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <sndfile.h>

#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace
{

//! Track::Type: Instrument, Pattern, Sample, Event, Video, Automation,
//! HiddenAutomation. The first five make audio; the last two do not.
constexpr int kFirstAutomationType = static_cast<int>(Track::Type::Automation);

//! How many frames of a WAV are copied per read when a range is trimmed.
constexpr int kTrimBlockFrames = 4096;

//! Temporary files this render owns; removed on every exit path.
struct TempFiles
{
	QString project;      //!< the session, serialised
	QString mutedProject; //!< the same session with every other track muted
	QString render;       //!< the untrimmed render, when a range is wanted

	~TempFiles()
	{
		for (const QString& path : { project, mutedProject, render })
		{
			if (!path.isEmpty()) { QFile::remove(path); }
		}
	}
};

QString tempPath(const QString& suffix)
{
	return QDir::tempPath()
		+ QStringLiteral("/zene-bounce-%1-%2.%3")
			.arg(QCoreApplication::applicationPid())
			.arg(QDateTime::currentMSecsSinceEpoch())
			.arg(suffix);
}

//! True for a track element that carries audio.
bool carriesAudio(const QDomElement& element)
{
	return element.attribute(QStringLiteral("type")).toInt() < kFirstAutomationType;
}

//! True when \a element is the track the render is for.
bool isTheTarget(const QDomElement& element, int targetId)
{
	const QString idAttribute = element.attribute(QStringLiteral("id"));
	return !idAttribute.isEmpty() && idAttribute.toInt() == targetId;
}

/*! Mutes one track element in place - the target is forced UNMUTED - and
 *  reports whether it was the target.
 *
 *  AUTOMATION TRACKS ARE DELIBERATELY LEFT ALONE. Song::updateLength() and the
 *  export path SKIP a muted track entirely, and an automation track carries no
 *  audio to mute - so muting one would silently drop the target's automation
 *  from the render instead of silencing anything.
 */
bool muteTrackElement(QDomElement element, int targetId)
{
	const bool isTarget = isTheTarget(element, targetId);
	if (!isTarget && !carriesAudio(element)) { return false; }
	element.setAttribute(QStringLiteral("muted"), isTarget ? 0 : 1);
	return isTarget;
}

bool writeXml(QDomDocument& document, const QString& path, QString* error)
{
	QFile out(path);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		*error = QStringLiteral("could not write the render's session copy");
		return false;
	}
	const QByteArray bytes = document.toByteArray();
	if (out.write(bytes) != bytes.size())
	{
		*error = QStringLiteral("could not write the render's session copy");
		return false;
	}
	return true;
}

/*! Mutes every audio-producing track but \a targetId in a serialised project.
 *
 *  The mute is what makes the render THIS track's contribution; the product
 *  already renders per track this way (RenderManager::renderNextTrack mutes
 *  everything but the track it is about to render), and the target is forced
 *  UNMUTED so a bounce of a muted track is its audio rather than silence.
 */
bool muteOtherTracks(const QString& source, const QString& target, int targetId, QString* error)
{
	QFile file(source);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("could not read the serialised session");
		return false;
	}
	QDomDocument document;
	if (!document.setContent(&file, false))
	{
		*error = QStringLiteral("the serialised session is not readable XML");
		return false;
	}
	file.close();

	const QDomNodeList tracks = document.elementsByTagName(QStringLiteral("track"));
	bool found = false;
	for (int i = 0; i < tracks.count(); ++i)
	{
		QDomElement element = tracks.at(i).toElement();
		if (element.isNull()) { continue; }
		found = muteTrackElement(element, targetId) || found;
	}
	if (!found)
	{
		*error = QStringLiteral("the serialised session does not carry the track");
		return false;
	}
	return writeXml(document, target, error);
}

//! Runs the shipped CLI render path as a child process, exactly as
//! render.render does, and for the reason its comment gives.
bool runCliRender(const QString& projectPath, const QString& out, int sampleRate, int* exitCode)
{
	QProcess renderer;
	renderer.setStandardOutputFile(QProcess::nullDevice());
	renderer.setStandardErrorFile(QProcess::nullDevice());
	renderer.start(QCoreApplication::applicationFilePath(),
		{QStringLiteral("render"), projectPath, QStringLiteral("-o"), out,
			QStringLiteral("-f"), QStringLiteral("wav"), QStringLiteral("-s"),
			QString::number(sampleRate)});
	const bool finished = renderer.waitForStarted(30000) && renderer.waitForFinished(600000);
	if (!finished) { renderer.kill(); }
	*exitCode = finished ? renderer.exitCode() : -1;
	return finished;
}

//! Everything that can refuse, checked before anything is written (SPEC A16: a
//! refusal leaves no file behind). Empty when the request is usable.
QString validateRequest(Track* track, const QString& outPath)
{
	if (track == nullptr) { return QStringLiteral("no track to render"); }
	if (outPath.isEmpty() || !outPath.startsWith(QLatin1Char('/')))
	{
		return QStringLiteral("the output path must be absolute");
	}
	if (Engine::getSong() == nullptr) { return QStringLiteral("no engine to render with"); }
	if (track->numOfClips() == 0)
	{
		return QStringLiteral("the track has no clips: there is nothing to render");
	}
	return QString();
}

//! Serialises the session and mutes every other track in the copy.
bool serialiseWithOnlyTrack(Track* track, TempFiles* temps, QString* error)
{
	temps->project = tempPath(QStringLiteral("mmp"));
	if (!Engine::getSong()->saveProjectFile(temps->project))
	{
		*error = QStringLiteral("could not serialise the session for rendering");
		return false;
	}
	temps->mutedProject = tempPath(QStringLiteral("mmp"));
	return muteOtherTracks(temps->project, temps->mutedProject, track->id(), error);
}

//! Writes the range's frames out of a whole-track render.
bool writeRange(const QString& rendered, const QString& outPath,
		const BounceInPlace::Range& range, float framesPerTick,
		qint64* framesWritten, QString* error)
{
	const qint64 startFrame = static_cast<qint64>(std::llround(range.start * framesPerTick));
	const qint64 wanted = static_cast<qint64>(std::llround((range.end - range.start) * framesPerTick));
	return BounceInPlace::trimWav(rendered, outPath, startFrame, wanted, framesWritten, error);
}

//! Measures the written file and fills the result's range, bytes and hash.
BounceInPlace::Result collectResult(const QString& outPath,
		const BounceInPlace::Range& range, float framesPerTick, int sampleRate,
		qint64 framesWritten, bool trimmed)
{
	BounceInPlace::Result result;
	result.sampleRate = sampleRate;
	result.range.start = trimmed ? range.start : 0;
	result.frames = trimmed ? framesWritten : BounceInPlace::wavFrameCount(outPath);
	result.range.end = result.range.start
		+ static_cast<tick_t>(std::llround(result.frames / framesPerTick));
	result.path = outPath;
	result.bytes = QFileInfo(outPath).size();
	result.sha256 = BounceInPlace::sha256OfFile(outPath);
	if (result.frames <= 0 || result.sha256.isEmpty())
	{
		return BounceInPlace::Result::failure(QStringLiteral("the render produced no audio"));
	}
	result.ok = true;
	return result;
}

//! Copies \a frameCount frames of \a in to \a out, in bounded blocks.
bool copyFrames(SNDFILE* in, SNDFILE* out, int channels, qint64 frameCount, qint64* framesWritten)
{
	std::vector<float> block(static_cast<size_t>(channels) * kTrimBlockFrames);
	qint64 remaining = frameCount;
	while (remaining > 0)
	{
		const sf_count_t want = static_cast<sf_count_t>(
			std::min<qint64>(remaining, kTrimBlockFrames));
		const sf_count_t got = sf_readf_float(in, block.data(), want);
		if (got <= 0) { break; }
		if (sf_writef_float(out, block.data(), got) != got) { return false; }
		remaining -= got;
		*framesWritten += got;
	}
	return true;
}

//! True when the child process did not produce a usable render.
bool renderFailed(bool finished, int exitCode, const QString& renderTarget)
{
	const QFileInfo rendered(renderTarget);
	return !finished || exitCode != 0 || !rendered.exists() || rendered.size() == 0;
}

//! Creates the directory the output file lives in. Empty error means ready.
QString prepareOutputDir(const QString& outPath)
{
	const QString outDir = QFileInfo(outPath).absolutePath();
	if (QDir().mkpath(outDir)) { return QString(); }
	return QStringLiteral("could not create '%1'").arg(outDir);
}

} // namespace

BounceInPlace::Result BounceInPlace::Result::failure(const QString& why)
{
	Result result;
	result.error = why;
	return result;
}

BounceInPlace::Result BounceInPlace::renderTrack(Track* track, const QString& outPath)
{
	return renderTrack(track, outPath, Range{});
}

BounceInPlace::Result BounceInPlace::renderTrack(Track* track, const QString& outPath,
		const Range& range, int sampleRate)
{
	const QString problem = validateRequest(track, outPath);
	if (!problem.isEmpty()) { return Result::failure(problem); }

	const QString noDir = prepareOutputDir(outPath);
	if (!noDir.isEmpty()) { return Result::failure(noDir); }

	TempFiles temps;
	QString error;
	if (!serialiseWithOnlyTrack(track, &temps, &error)) { return Result::failure(error); }

	// A range is rendered as a trim of the track's own render: the engine's
	// render always starts at tick 0, and the window the caller asked for is
	// frames [startFrame, endFrame) of that file.
	const bool trimmed = !range.coversWholeTrack();
	const QString renderTarget = trimmed ? tempPath(QStringLiteral("wav")) : outPath;
	if (trimmed) { temps.render = renderTarget; }

	int exitCode = -1;
	const bool finished = runCliRender(temps.mutedProject, renderTarget, sampleRate, &exitCode);
	if (renderFailed(finished, exitCode, renderTarget))
	{
		return Result::failure(QStringLiteral("the render failed (exit %1)").arg(exitCode));
	}

	const float framesPerTick = Engine::framesPerTick(static_cast<sample_rate_t>(sampleRate));
	qint64 written = 0;
	if (trimmed && !writeRange(renderTarget, outPath, range, framesPerTick, &written, &error))
	{
		return Result::failure(error);
	}
	return collectResult(outPath, range, framesPerTick, sampleRate, written, trimmed);
}

bool BounceInPlace::trimWav(const QString& source, const QString& target,
		qint64 startFrame, qint64 frameCount, qint64* framesWritten, QString* error)
{
	*framesWritten = 0;
	if (startFrame < 0 || frameCount < 0)
	{
		*error = QStringLiteral("the range must not be negative");
		return false;
	}

	SF_INFO sourceInfo{};
	SNDFILE* in = sf_open(source.toLocal8Bit().constData(), SFM_READ, &sourceInfo);
	if (in == nullptr)
	{
		*error = QStringLiteral("could not open the render for trimming");
		return false;
	}
	// Clamp the window to the source: a range whose end is past the track's own
	// end renders the part that exists, and the caller is told how much that was
	// through framesWritten.
	const qint64 available = sourceInfo.frames > startFrame ? sourceInfo.frames - startFrame : 0;
	const qint64 toWrite = std::min(frameCount, available);
	if (toWrite == 0)
	{
		sf_close(in);
		*error = QStringLiteral("the range starts past the end of the render");
		return false;
	}
	if (sf_seek(in, startFrame, SEEK_SET) < 0)
	{
		sf_close(in);
		*error = QStringLiteral("could not seek to the start of the range");
		return false;
	}

	SF_INFO targetInfo = sourceInfo;
	targetInfo.frames = toWrite;
	SNDFILE* out = sf_open(target.toLocal8Bit().constData(), SFM_WRITE, &targetInfo);
	if (out == nullptr)
	{
		sf_close(in);
		*error = QStringLiteral("could not write the trimmed render");
		return false;
	}
	const bool copied = copyFrames(in, out, sourceInfo.channels, toWrite, framesWritten);
	sf_close(in);
	const bool closed = sf_close(out) == 0;
	if (!copied || !closed)
	{
		*error = QStringLiteral("could not write the trimmed render");
		return false;
	}
	return *framesWritten == toWrite;
}

qint64 BounceInPlace::wavFrameCount(const QString& path)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (file == nullptr) { return -1; }
	sf_close(file);
	return static_cast<qint64>(info.frames);
}

QString BounceInPlace::sha256OfFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha256);
	if (!hash.addData(&file)) { return QString(); }
	return QString::fromLatin1(hash.result().toHex());
}

} // namespace lmms
