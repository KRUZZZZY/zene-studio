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
#ifdef _WIN32
#include <filesystem>
#include <system_error>
#else
#include <sys/stat.h>
#endif
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

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

//! True for a track element that carries audio.
bool carriesAudio(const QDomElement& element)
{
	return element.attribute(QStringLiteral("type")).toInt() < kFirstAutomationType;
}

//! True when \a element is the track the render is for.
bool isTheTarget(const QDomElement& element, const QSet<int>& targetIds)
{
	const QString idAttribute = element.attribute(QStringLiteral("id"));
	return !idAttribute.isEmpty() && targetIds.contains(idAttribute.toInt());
}

/*! Mutes one track element in place - the target is forced UNMUTED - and
 *  reports whether it was the target.
 *
 *  AUTOMATION TRACKS ARE DELIBERATELY LEFT ALONE. Song::updateLength() and the
 *  export path SKIP a muted track entirely, and an automation track carries no
 *  audio to mute - so muting one would silently drop the target's automation
 *  from the render instead of silencing anything.
 */
bool muteTrackElement(QDomElement element, const QSet<int>& targetIds)
{
	const bool isTarget = isTheTarget(element, targetIds);
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
bool muteOtherTracks(const QString& source, const QString& target, const QSet<int>& targetIds, QString* error)
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
		found = muteTrackElement(element, targetIds) || found;
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
	// Keep this plain blocking wait for the legacy result contract. Do not add
	// processEvents() or a nested event loop: ControlRegistry::invoke() has no
	// deferred reply path, and re-entering the project while its render is in
	// flight would make the synchronous command's target unsafe. A GUI caller
	// still blocks here; the cancellable RenderQueue is the responsive path.
	const bool finished = renderer.waitForStarted(30000) && renderer.waitForFinished(600000);
	if (!finished) { renderer.kill(); }
	*exitCode = finished ? renderer.exitCode() : -1;
	return finished;
}

//! Everything that can refuse, checked before anything is written (SPEC A16: a
//! refusal leaves no file behind). Empty when the request is usable.
QString validateRequest(const std::vector<Track*>& tracks, const QString& outPath)
{
	if (tracks.empty() || std::find(tracks.begin(), tracks.end(), nullptr) != tracks.end()) { return QStringLiteral("no track to render"); }
	if (outPath.isEmpty() || !BounceInPlace::isAbsoluteOutputPath(outPath))
	{
		return QStringLiteral("the output path must be absolute");
	}
	if (Engine::getSong() == nullptr) { return QStringLiteral("no engine to render with"); }
	if (std::none_of(tracks.begin(), tracks.end(), [](Track* track) { return track->numOfClips() > 0; }))
	{
		return QStringLiteral("the track has no clips: there is nothing to render");
	}
	return QString();
}

//! Serialises the session and mutes every track but @a tracks in the copy.
bool serialiseWithOnlyTracks(const std::vector<Track*>& tracks, const QString& projectPath,
		const QString& mutedProjectPath, QString* error)
{
	if (!Engine::getSong()->saveProjectFile(projectPath))
	{
		*error = QStringLiteral("could not serialise the session for rendering");
		return false;
	}
	QSet<int> ids;
	for (const Track* track : tracks) { ids.insert(track->id()); }
	return muteOtherTracks(projectPath, mutedProjectPath, ids, error);
}

//! True when the render follows a tempo map, so one frames-per-tick rate does not hold across the range.
bool tempoMapInForce(const TempoMap& map)
{
	return map.active() && !map.empty();
}

//! Frames of the render from tick @a from to tick @a to: at the project's one rate, or on the tempo map's
//! own timeline when it is in force - a range across a tempo change is not at one rate.
double framesBetween(tick_t from, tick_t to, float framesPerTick, const TempoMap& map,
		int globalTempo, int sampleRate)
{
	if (!tempoMapInForce(map)) { return (to - from) * framesPerTick; }
	return (map.secondsAtTick(to, globalTempo) - map.secondsAtTick(from, globalTempo)) * sampleRate;
}

//! The tick @a frames of the render after tick @a start: framesBetween()'s inverse.
tick_t tickAfter(tick_t start, qint64 frames, float framesPerTick, const TempoMap& map,
		int globalTempo, int sampleRate)
{
	if (!tempoMapInForce(map)) { return start + static_cast<tick_t>(std::llround(frames / framesPerTick)); }
	const double seconds = map.secondsAtTick(start, globalTempo) + static_cast<double>(frames) / sampleRate;
	return map.tickAtSeconds(seconds, globalTempo);
}

//! Measures the written file and fills the result's range, bytes and hash.
BounceInPlace::Result collectResult(const QString& outPath,
		const BounceInPlace::PreparedRender& prepared,
		qint64 framesWritten, bool trimmed)
{
	BounceInPlace::Result result;
	result.sampleRate = prepared.sampleRate;
	result.range.start = trimmed ? prepared.range.start : 0;
	result.frames = trimmed ? framesWritten : BounceInPlace::wavFrameCount(outPath);
	result.range.end = tickAfter(result.range.start, result.frames, prepared.framesPerTick,
		prepared.tempoMap, prepared.globalTempo, prepared.sampleRate);
	result.path = prepared.outputPath;
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

bool BounceInPlace::isAbsoluteOutputPath(const QString& outPath)
{
	return QDir::isAbsolutePath(outPath);
}

BounceInPlace::Result BounceInPlace::Result::failure(const QString& why)
{
	Result result;
	result.error = why;
	return result;
}

bool BounceInPlace::prepareRenderTracks(const std::vector<Track*>& tracks, const QString& outPath,
		const Range& range, int sampleRate, PreparedRender* prepared, QString* error)
{
	if (prepared == nullptr || error == nullptr) { return false; }
	const QString problem = validateRequest(tracks, outPath);
	if (!problem.isEmpty()) { *error = problem; return false; }
	if (sampleRate <= 0) { *error = QStringLiteral("the sample rate must be positive"); return false; }
	if (const QString noDir = prepareOutputDir(outPath); !noDir.isEmpty()) { *error = noDir; return false; }

	PreparedRender next;
	next.scratch = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/zene-bounce-XXXXXX"));
	if (!next.scratch->isValid())
	{
		*error = QStringLiteral("could not create a temporary directory for the render");
		return false;
	}
	next.outputPath = outPath;
	next.range = range;
	next.sampleRate = sampleRate;
	next.projectPath = next.scratch->filePath(QStringLiteral("session.mmp"));
	next.mutedProjectPath = next.scratch->filePath(QStringLiteral("muted-session.mmp"));
	next.trimmed = !range.coversWholeTrack();
	next.stagedPath = outPath + QStringLiteral(".part.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
	next.renderPath = next.trimmed ? next.scratch->filePath(QStringLiteral("render.wav")) : next.stagedPath;
	Song* song = Engine::getSong();
	next.tempoMap = song->tempoMap().map();
	next.globalTempo = static_cast<int>(song->getTempo());
	next.framesPerTick = Engine::framesPerTick(static_cast<sample_rate_t>(sampleRate));
	if (!serialiseWithOnlyTracks(tracks, next.projectPath, next.mutedProjectPath, error)) { return false; }
	if (next.trimmed)
	{
		next.startFrame = static_cast<qint64>(std::llround(
			framesBetween(0, range.start, next.framesPerTick, next.tempoMap, next.globalTempo, sampleRate)));
		next.wantedFrames = static_cast<qint64>(std::llround(
			framesBetween(range.start, range.end, next.framesPerTick, next.tempoMap, next.globalTempo, sampleRate)));
	}
	*prepared = std::move(next);
	return true;
}

QStringList BounceInPlace::renderArguments(const PreparedRender& prepared)
{
	return {QStringLiteral("render"), prepared.mutedProjectPath, QStringLiteral("-o"), prepared.renderPath,
		QStringLiteral("-f"), QStringLiteral("wav"), QStringLiteral("-s"), QString::number(prepared.sampleRate)};
}

BounceInPlace::Result BounceInPlace::finishPreparedRender(const PreparedRender& prepared, QString* error)
{
	const QFileInfo rendered(prepared.renderPath);
	if (!rendered.exists() || rendered.size() == 0)
	{
		*error = QStringLiteral("the render produced no audio");
		return Result::failure(*error);
	}
	qint64 written = 0;
	if (prepared.trimmed && !trimWav(prepared.renderPath, prepared.stagedPath,
			prepared.startFrame, prepared.wantedFrames, &written, error))
	{
		return Result::failure(*error);
	}
	Result result = collectResult(prepared.stagedPath, prepared, written, prepared.trimmed);
	if (!result.ok) { *error = result.error; }
	return result;
}

bool BounceInPlace::installPreparedOutput(PreparedRender* prepared, QString* error)
{
	if (prepared == nullptr || error == nullptr) { return false; }
	if (QFileInfo::exists(prepared->outputPath))
	{
		prepared->backupPath = prepared->outputPath + QStringLiteral(".backup.")
			+ QUuid::createUuid().toString(QUuid::WithoutBraces);
		if (!QFile::rename(prepared->outputPath, prepared->backupPath))
		{
			*error = QStringLiteral("could not preserve the existing output file");
			return false;
		}
		prepared->replacedPrevious = true;
	}
	if (!QFile::rename(prepared->stagedPath, prepared->outputPath))
	{
		if (prepared->replacedPrevious) { QFile::rename(prepared->backupPath, prepared->outputPath); }
		prepared->replacedPrevious = false;
		prepared->backupPath.clear();
		*error = QStringLiteral("could not install the completed render");
		return false;
	}
	prepared->installed = true;
	return true;
}

void BounceInPlace::commitPreparedOutput(PreparedRender* prepared)
{
	if (prepared == nullptr) { return; }
	if (prepared->replacedPrevious) { QFile::remove(prepared->backupPath); }
	prepared->backupPath.clear();
	prepared->replacedPrevious = false;
	prepared->installed = false;
}

void BounceInPlace::rollbackPreparedOutput(PreparedRender* prepared)
{
	if (prepared == nullptr || !prepared->installed) { return; }
	QFile::remove(prepared->outputPath);
	if (prepared->replacedPrevious) { QFile::rename(prepared->backupPath, prepared->outputPath); }
	prepared->backupPath.clear();
	prepared->replacedPrevious = false;
	prepared->installed = false;
}

void BounceInPlace::discardPreparedRender(PreparedRender* prepared)
{
	if (prepared == nullptr) { return; }
	if (!prepared->installed) { QFile::remove(prepared->stagedPath); }
	if (prepared->scratch) { prepared->scratch->remove(); }
	prepared->scratch.reset();
}

BounceInPlace::Result BounceInPlace::renderTrack(Track* track, const QString& outPath)
{
	return renderTrack(track, outPath, Range{});
}

BounceInPlace::Result BounceInPlace::renderTrack(Track* track, const QString& outPath,
		const Range& range, int sampleRate)
{
	return renderTracks({track}, outPath, range, sampleRate);
}

BounceInPlace::Result BounceInPlace::renderTracks(const std::vector<Track*>& tracks, const QString& outPath,
		const Range& range, int sampleRate)
{
	PreparedRender prepared;
	QString error;
	if (!prepareRenderTracks(tracks, outPath, range, sampleRate, &prepared, &error)) { return Result::failure(error); }

	int exitCode = -1;
	const bool finished = runCliRender(prepared.mutedProjectPath, prepared.renderPath, sampleRate, &exitCode);
	if (renderFailed(finished, exitCode, prepared.renderPath))
	{
		discardPreparedRender(&prepared);
		return Result::failure(QStringLiteral("the render failed (exit %1)").arg(exitCode));
	}

	Result result = finishPreparedRender(prepared, &error);
	if (!result.ok)
	{
		discardPreparedRender(&prepared);
		return Result::failure(error);
	}
	if (!installPreparedOutput(&prepared, &error))
	{
		discardPreparedRender(&prepared);
		return Result::failure(error);
	}
	commitPreparedOutput(&prepared);
	discardPreparedRender(&prepared);
	return result;
}

//! True when \a target names the file \a source is read from. The target is
//! opened for WRITING, which truncates it, so a target that resolves to the
//! source destroys the audio before a frame is copied - a symlink, a "./" prefix
//! or a relative spelling all name the same file. A path that does not resolve
//! yet falls back to its absolute form, which is enough: a target that does not
//! exist cannot be the file being read. A HARD link has no shared spelling at
//! all (its canonical path is its own), so the last test asks the filesystem
//! whether the two names are one file (same device and inode, or the Windows
//! equivalent), which is also what a symlink to the source reports.
bool namesTheSameFile(const QString& source, const QString& target)
{
	const QFileInfo sourceInfo(source);
	const QFileInfo targetInfo(target);
	const QString sourceKey =
		sourceInfo.canonicalFilePath().isEmpty() ? sourceInfo.absoluteFilePath() : sourceInfo.canonicalFilePath();
	const QString targetKey =
		targetInfo.canonicalFilePath().isEmpty() ? targetInfo.absoluteFilePath() : targetInfo.canonicalFilePath();
	if (sourceKey == targetKey) { return true; }
#ifdef _WIN32
	// equivalent() reports an error (and false) when either name does not exist, which is the no-op case.
	std::error_code ec;
	const bool same = std::filesystem::equivalent(
		std::filesystem::path(source.toStdWString()), std::filesystem::path(target.toStdWString()), ec);
	return !ec && same;
#else
	// Same device and inode. std::filesystem is unavailable below the macOS 10.15 deployment target the
	// hosted macos-x86_64 job builds for; stat() is the same test there and on Linux. A name that does
	// not exist fails stat(), which is the no-op case.
	struct stat sourceStat {};
	struct stat targetStat {};
	if (::stat(QFile::encodeName(source).constData(), &sourceStat) != 0
		|| ::stat(QFile::encodeName(target).constData(), &targetStat) != 0)
	{
		return false;
	}
	return sourceStat.st_dev == targetStat.st_dev && sourceStat.st_ino == targetStat.st_ino;
#endif
}

/*! Opens \a source for reading, seeks it to \a startFrame and reports in
 *  \a toWrite how many frames of the requested window the render actually has.
 *
 *  The window is clamped to the source: a range whose end is past the track's own
 *  end renders the part that exists, and the caller is told how much that was
 *  through framesWritten. Returns false with \a error set, having closed whatever
 *  it opened, when the range is empty or the source cannot be read or seeked.
 */
bool openTrimWindow(const QString& source, qint64 startFrame, qint64 frameCount,
		SNDFILE** in, SF_INFO* sourceInfo, qint64* toWrite, QString* error)
{
	*in = sf_open(source.toLocal8Bit().constData(), SFM_READ, sourceInfo);
	if (*in == nullptr)
	{
		*error = QStringLiteral("could not open the render for trimming");
		return false;
	}
	const qint64 available =
		sourceInfo->frames > startFrame ? sourceInfo->frames - startFrame : 0;
	*toWrite = std::min(frameCount, available);
	if (*toWrite == 0)
	{
		sf_close(*in);
		*error = QStringLiteral("the range starts past the end of the render");
		return false;
	}
	if (sf_seek(*in, startFrame, SEEK_SET) < 0)
	{
		sf_close(*in);
		*error = QStringLiteral("could not seek to the start of the range");
		return false;
	}
	return true;
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
	if (namesTheSameFile(source, target))
	{
		*error = QStringLiteral("the trimmed render must not be written onto the render it is trimmed from");
		return false;
	}

	SF_INFO sourceInfo{};
	SNDFILE* in = nullptr;
	qint64 toWrite = 0;
	if (!openTrimWindow(source, startFrame, frameCount, &in, &sourceInfo, &toWrite, error))
	{
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
