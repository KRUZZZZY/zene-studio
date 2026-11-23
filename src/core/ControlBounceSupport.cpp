/*
 * ControlBounceSupport.cpp - what the bounce and freeze commands share
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

// Moved out of ControlCommandsFreeze.cpp (0.5.0 L01) when bounce.land needed the same range and
// output rules: one definition, used by bounce.in_place, freeze.* and bounce.land.

#include "ControlBounceSupport.h"

#include <algorithm>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVector>

#include <sndfile.h>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms::control
{

static QString checkRangeArgs(const QJsonObject& args, bool required)
{
	const bool hasStart = args.contains(QStringLiteral("start"));
	const bool hasEnd = args.contains(QStringLiteral("end"));
	if (hasStart != hasEnd)
	{
		return QStringLiteral("'start' and 'end' go together: give both, or neither for the "
			"whole track");
	}
	if (required && !hasStart)
	{
		return QStringLiteral("a region needs both 'start' and 'end' (ticks)");
	}
	return QString();
}

//! A tick argument as tick_t, clamped to one tick past either end of the timeline first: casting a
//! double outside tick_t's range is undefined, and a clamped value is refused below just as the
//! original would have been.
static tick_t clampedTicks(const QJsonObject& args, const QString& key)
{
	return static_cast<tick_t>(std::clamp(args.value(key).toDouble(), -1.0, MaxSongLength + 1.0));
}

QString parseRange(const QJsonObject& args, bool required, BounceInPlace::Range* range)
{
	const QString problem = checkRangeArgs(args, required);
	if (!problem.isEmpty()) { return problem; }
	if (!args.contains(QStringLiteral("start"))) { return QString(); }

	if (!args.value(QStringLiteral("start")).isDouble()
		|| !args.value(QStringLiteral("end")).isDouble())
	{
		return QStringLiteral("'start' and 'end' are tick positions, in integers");
	}
	range->start = clampedTicks(args, QStringLiteral("start"));
	range->end = clampedTicks(args, QStringLiteral("end"));
	if (range->start < 0) { return QStringLiteral("'start' must not be negative"); }
	if (range->end <= range->start) { return QStringLiteral("'end' must be past 'start'"); }
	if (range->end > MaxSongLength)
	{
		return QStringLiteral("'end' is past the end of the timeline");
	}
	return QString();
}

//! Where a bounce lands when the caller does not say: beside the project, in a
//! "<project>-bounces" directory, named after the track's stable id and the verb
//! (and the tick range, for a region), so bounces of different tracks, verbs or
//! ranges never overwrite each other and a reader can tell what produced the
//! file. The same bounce made again lands on the same path and replaces it.
static QString defaultBouncePath(Track* track, const QString& verb, const BounceInPlace::Range& range)
{
	const Song* song = Engine::getSong();
	const QString project = song->projectFileName();
	const QDir base = project.isEmpty() ? QDir(QDir::tempPath()) : QFileInfo(project).dir();
	// Keep repeated bounces stable in one document while isolating different unsaved projects.
	const QString name = project.isEmpty()
		? QStringLiteral("untitled-%1").arg(song->property("_zeneUnsavedProjectId").toString())
		: QFileInfo(project).completeBaseName();
	const QDir directory(base.filePath(name + QStringLiteral("-bounces")));

	QString file = QStringLiteral("%1-%2").arg(control::trackIdOf(track), verb);
	if (!range.coversWholeTrack())
	{
		file += QStringLiteral("-%1-%2").arg(range.start).arg(range.end);
	}
	return directory.filePath(file + QStringLiteral(".wav"));
}

QString resolveOutPath(const QJsonObject& args, Track* track, const QString& verb,
		const BounceInPlace::Range& range)
{
	const QString requested = args.value(QStringLiteral("out")).toString();
	return requested.isEmpty() ? defaultBouncePath(track, verb, range) : requested;
}

QString flattenRefusal(const Track* track)
{
	// The same test BounceInPlace's carriesAudio applies: an automation track or a folder has no audio of its own, so
	// its render is silence and flattening it would replace its data with that silence.
	if (static_cast<int>(track->type()) < static_cast<int>(Track::Type::Automation)) { return QString(); }
	return QStringLiteral("%1 carries no audio: a flatten needs an instrument, pattern, sample, event or video track")
		.arg(trackIdOf(track));
}

namespace
{

//! True when every audible clip of @a track lies inside @a range, so muting the track silences nothing outside it.
bool audibleClipsInside(const Track* track, const BounceInPlace::Range& range)
{
	for (const Clip* clip : track->getClips())
	{
		if (clip == nullptr || clip->isMuted()) { continue; }
		if (clip->startPosition().getTicks() < range.start || clip->endPosition().getTicks() > range.end) { return false; }
	}
	return true;
}

//! Copies \a source to \a target, writing digital silence for frames [\a gapStart, \a gapEnd) and the original
//! samples everywhere else. The format (rate, channels, sample format) is the source's own.
bool writeTakeWithSilentWindow(const QString& source, const QString& target, qint64 gapStart, qint64 gapEnd,
		QString* error)
{
	SF_INFO info{};
	SNDFILE* in = sf_open(source.toLocal8Bit().constData(), SFM_READ, &info);
	if (in == nullptr)
	{
		*error = QStringLiteral("could not read the frozen take '%1'").arg(source);
		return false;
	}
	SNDFILE* out = sf_open(target.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (out == nullptr)
	{
		sf_close(in);
		*error = QStringLiteral("could not write the range-muted frozen take '%1'").arg(target);
		return false;
	}
	std::vector<float> block(static_cast<size_t>(std::max(1, info.channels)) * 4096);
	qint64 position = 0;
	bool copied = true;
	for (;;)
	{
		const sf_count_t got = sf_readf_float(in, block.data(), 4096);
		if (got <= 0) { break; }
		for (sf_count_t frame = 0; frame < got; ++frame)
		{
			const qint64 at = position + frame;
			if (at < gapStart || at >= gapEnd) { continue; }
			for (int channel = 0; channel < info.channels; ++channel)
			{
				block[static_cast<size_t>(frame) * static_cast<size_t>(info.channels) + static_cast<size_t>(channel)] = 0.0f;
			}
		}
		if (sf_writef_float(out, block.data(), got) != got) { copied = false; break; }
		position += got;
	}
	sf_close(in);
	const bool closed = sf_close(out) == 0;
	if (!copied || !closed)
	{
		*error = QStringLiteral("could not write the range-muted frozen take '%1'").arg(target);
		return false;
	}
	return true;
}

/*! Writes the frozen take of \a track with its overlap with \a range silent, returning the file in
 *  \a target (empty when the take does not overlap the range at all). Only a file is produced here; the track
 *  is not touched, so a failure refuses the command before the journal or the mix changes. */
bool prepareFrozenGap(Track* track, const BounceInPlace::Range& range, QString* target, QString* error)
{
	target->clear();
	if (!track->isFrozen()) { return true; }
	const Track::FrozenTake take = track->frozenTake();
	const tick_t from = std::max(range.start, take.startTicks);
	// A take with no end boundary plays to the end of its file; any range past its start overlaps it.
	const tick_t to = take.endTicks > take.startTicks ? std::min(range.end, take.endTicks) : range.end;
	if (to <= from) { return true; }

	SF_INFO info{};
	SNDFILE* probe = sf_open(take.path.toLocal8Bit().constData(), SFM_READ, &info);
	if (probe == nullptr)
	{
		*error = QStringLiteral("could not read the frozen take '%1'").arg(take.path);
		return false;
	}
	sf_close(probe);
	const qint64 totalFrames = info.frames;
	if (info.samplerate <= 0 || totalFrames <= 0) { return true; }

	const Song* song = Engine::getSong();
	const TempoMap map = song->tempoMap().map();
	const int tempo = static_cast<int>(song->getTempo());
	const float oneRate = Engine::framesPerTick(static_cast<sample_rate_t>(info.samplerate));
	const qint64 gapStart = std::max<qint64>(0, Track::frozenTakeSourceFrame(
		map, tempo, take.startTicks, from, info.samplerate, oneRate));
	const qint64 gapEnd = std::min<qint64>(totalFrames, Track::frozenTakeSourceFrame(
		map, tempo, take.startTicks, to, info.samplerate, oneRate));
	if (gapEnd <= gapStart) { return true; }

	// Beside the take it replaces, named by stable track id and range so a repeated bounce of the same range
	// reuses the one file instead of accumulating copies.
	const QString finalPath = QFileInfo(take.path).dir().filePath(
		QStringLiteral("%1-range-mute-%2-%3.wav").arg(trackIdOf(track)).arg(range.start).arg(range.end));
	const QString staging = finalPath + QStringLiteral(".part");
	if (!writeTakeWithSilentWindow(take.path, staging, gapStart, gapEnd, error)) { return false; }
	if (BounceInPlace::wavFrameCount(staging) <= 0)
	{
		QFile::remove(staging);
		*error = QStringLiteral("the range-muted frozen take '%1' is not readable audio").arg(staging);
		return false;
	}
	QFile::remove(finalPath);
	if (!QFile::rename(staging, finalPath))
	{
		QFile::remove(staging);
		*error = QStringLiteral("could not install the range-muted frozen take '%1'").arg(finalPath);
		return false;
	}
	*target = finalPath;
	return true;
}

//! Installs \a takePath as the track's take with the same window, muted-clip record and whole/region flag.
bool installFrozenGap(Track* track, const QString& takePath, QString* error)
{
	const Track::FrozenTake take = track->frozenTake();
	QString reason;
	if (!track->freezeTo(takePath, take.startTicks, take.endTicks, take.mutedClips, &reason, take.region))
	{
		*error = reason;
		return false;
	}
	return true;
}

//! Cuts the clips of \a track at the range's edges and mutes the pieces inside it.
void muteClipsInRange(Track* track, const BounceInPlace::Range& range)
{
	arrangement::isolateRegion(track, range.start, range.end, nullptr);
	for (Clip* clip : track->getClips())
	{
		if (clip == nullptr || clip->isMuted()) { continue; }
		const tick_t start = clip->startPosition().getTicks();
		if (start >= range.start && start < range.end) { clip->setMuted(true); }
	}
}

} // namespace

QString muteRangeSources(const std::vector<Track*>& tracks, const BounceInPlace::Range& range)
{
	// The frozen takes are rewritten first, while the track is untouched: a take that cannot be copied refuses
	// the command with no journal step and no model change.
	std::vector<std::pair<Track*, QString>> gaps;
	for (Track* track : tracks)
	{
		if (track == nullptr || !track->isFrozen()) { continue; }
		QString gapPath;
		QString gapError;
		if (!prepareFrozenGap(track, range, &gapPath, &gapError)) { return gapError; }
		if (!gapPath.isEmpty()) { gaps.emplace_back(track, gapPath); }
	}

	QVector<JournallingObject*> checkpoints;
	for (Track* track : tracks)
	{
		if (track != nullptr) { checkpoints.append(track); }
	}
	// One step before any cut, take or mute: undo puts the arrangement and the takes back exactly as they were.
	addUndoStep(checkpoints);

	for (const auto& gap : gaps)
	{
		QString installError;
		if (!installFrozenGap(gap.first, gap.second, &installError)) { return installError; }
	}

	for (Track* track : tracks)
	{
		if (track == nullptr) { continue; }
		const Track::Type type = track->type();
		// A folder or neutral row carries no audio of its own; its whole flag is the only mute it has.
		if (type == Track::Type::Folder || type == Track::Type::Neutral)
		{
			track->setMuted(true);
			continue;
		}
		const bool automation = type == Track::Type::Automation || type == Track::Type::HiddenAutomation;
		const bool audio = static_cast<int>(type) < static_cast<int>(Track::Type::Automation);
		// A frozen take's playback, and every automation lane, are region-replaced at clip level: muting the
		// whole lane would also stop what happens outside the range (cluster 3971 review).
		if (track->isFrozen() || automation || (audio && !audibleClipsInside(track, range)))
		{
			muteClipsInRange(track, range);
			continue;
		}
		track->setMuted(true);  // audio whose whole output lies inside the range
	}
	return QString();
}

//! The render facts every verb of this file reports.
QJsonObject renderJson(const BounceInPlace::Result& rendered)
{
	QJsonObject result;
	result.insert(QStringLiteral("path"), rendered.path);
	result.insert(QStringLiteral("sample_rate"), rendered.sampleRate);
	result.insert(QStringLiteral("frames"), rendered.frames);
	result.insert(QStringLiteral("bytes"), rendered.bytes);
	result.insert(QStringLiteral("sha256"), rendered.sha256);
	result.insert(QStringLiteral("start_ticks"), static_cast<qint64>(rendered.range.start));
	result.insert(QStringLiteral("end_ticks"), static_cast<qint64>(rendered.range.end));
	return result;
}

} // namespace lmms::control
