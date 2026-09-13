/*
 * ControlCommandsFreeze.cpp - the bounce.* and freeze.* command groups
 *                              (SPEC A11-A16): render a track's (or a region's)
 *                              output to audio, then play the render instead of
 *                              the source.
 *
 * The engine half is in include/BounceInPlace.h (the offline render, a child
 * process for the reason ControlCommandsProject.cpp's render.render gives) and
 * on Track (the frozen take: include/Track.h's Track::FrozenTake, the play()
 * substitution InstrumentTrack and SampleTrack make, and the `frozen` element
 * of the track's own project XML). What was missing is the AGENT SURFACE: with
 * no commands a freeze can only be authored by editing the project file, which
 * is exactly the gap AGENT-TOOLING.md section 1 makes a defect.
 *
 * One file, two groups, because they are one operation: bounce.in_place is the
 * render alone (the session is not touched - the SPEC A16 class is
 * not_mutating, like render.render), and the three freeze.* verbs are the
 * render plus the state that makes the track play it.
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

#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "BounceInPlace.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString ClauseFrozenTake = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::restoreState re-loads the track's own XML, and Track::loadTrack resets the "
	"frozen take when the element carries none - which is what makes freeze/unfreeze "
	"undoable in both directions)");

//! The tick range a mutating verb must be given, or an empty Range with
//! \a problem filled. `start` and `end` are both required for a region; a
//! whole-track verb passes neither.
QString checkRangeArgs(const QJsonObject& args, bool required)
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
	range->start = static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble());
	range->end = static_cast<tick_t>(args.value(QStringLiteral("end")).toDouble());
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
//! (and the tick range, for a region), so two bounces never overwrite each
//! other and a reader can tell what produced the file.
QString defaultBouncePath(Track* track, const QString& verb, const BounceInPlace::Range& range)
{
	const QString project = Engine::getSong()->projectFileName();
	const QDir base = project.isEmpty() ? QDir(QDir::tempPath()) : QFileInfo(project).dir();
	const QString name = project.isEmpty()
		? QStringLiteral("untitled")
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

//! The take the track carries right now - which is what a caller needs to see
//! after any freeze verb, and what `track.get_state` reports as well.
QJsonObject frozenStateJson(Track* track)
{
	QJsonObject result;
	result.insert(QStringLiteral("track"), control::trackIdOf(track));
	result.insert(QStringLiteral("name"), track->name());
	result.insert(QStringLiteral("frozen"), track->isFrozen());
	result.insert(QStringLiteral("audio"), track->frozenTake().path);
	// False when the render's file has moved or cannot be decoded: the state is
	// still frozen (the file is what the user asked for) but nothing sounds,
	// and this field is how a caller can tell the two apart.
	result.insert(QStringLiteral("audio_ready"), track->frozenAudioReady());
	result.insert(QStringLiteral("start_ticks"),
		static_cast<qint64>(track->frozenTake().startTicks));
	result.insert(QStringLiteral("end_ticks"),
		static_cast<qint64>(track->frozenTake().endTicks));
	result.insert(QStringLiteral("muted_clips"),
		static_cast<int>(track->frozenTake().mutedClips.size()));
	return result;
}

//! Every clip of the track that starts inside [\a start, \a end) and is not
//! already muted: the clips a region freeze can disable. A clip that merely
//! crosses the region's start is NOT in here - it is reported as overlapping
//! instead, because muting it would silence audio outside the region - and a clip
//! the user had muted themselves is not recorded either, because unfreeze must
//! unmute exactly what the freeze muted and nothing else.
std::vector<Track::FrozenTake::MutedClip> clipsInside(Track* track, const BounceInPlace::Range& range,
		QJsonArray* overlapping)
{
	std::vector<Track::FrozenTake::MutedClip> inside;
	for (const Clip* clip : track->getClips())
	{
		if (clip == nullptr || clip->isMuted()) { continue; }
		const tick_t start = clip->startPosition().getTicks();
		const tick_t end = clip->endPosition();
		if (start >= range.start && start < range.end)
		{
			inside.push_back(Track::FrozenTake::MutedClip{start, clip->length().getTicks()});
		}
		else if (start < range.start && end > range.start)
		{
			QJsonObject entry;
			entry.insert(QStringLiteral("start_ticks"), static_cast<qint64>(start));
			entry.insert(QStringLiteral("length_ticks"), static_cast<qint64>(clip->length().getTicks()));
			overlapping->append(entry);
		}
	}
	return inside;
}

// ---------------------------------------------------------------------------
// bounce.in_place
// ---------------------------------------------------------------------------
ControlResult bounceInPlace(const QJsonObject& args)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }

	BounceInPlace::Range range;
	const QString problem = parseRange(args, false, &range);
	if (!problem.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, problem);
	}

	const BounceInPlace::Result rendered = BounceInPlace::renderTrack(track,
		resolveOutPath(args, track, QStringLiteral("bounce"), range), range);
	if (!rendered.ok)
	{
		return ControlResult::failure(ControlErrorKind::Refused, rendered.error);
	}
	return ControlResult::success(renderJson(rendered));
}

// ---------------------------------------------------------------------------
// freeze.track / freeze.region
// ---------------------------------------------------------------------------

//! The shared body of the two freeze verbs: render, then install the take.
//! \a regionRequired tells the two apart (freeze.region needs 'start'/'end',
//! freeze.track refuses them as a range and freezes the whole track).
//!
//! The render happens BEFORE the journal checkpoint, so a render that fails
//! writes nothing and records nothing (SPEC A16: a refusal must leave the
//! journal untouched).
ControlResult freezeRange(const QJsonObject& args, bool regionRequired)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	if (track->isFrozen())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already frozen; freeze.unfreeze first - replacing a take "
				"would render the take that is already playing")
				.arg(control::trackIdOf(track)));
	}

	BounceInPlace::Range range;
	const QString problem = parseRange(args, regionRequired, &range);
	if (!problem.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, problem);
	}
	const bool wholeTrack = range.coversWholeTrack();

	const BounceInPlace::Result rendered = BounceInPlace::renderTrack(track,
		resolveOutPath(args, track,
			wholeTrack ? QStringLiteral("frozen") : QStringLiteral("frozen-region"), range),
		range);
	if (!rendered.ok)
	{
		return ControlResult::failure(ControlErrorKind::Refused, rendered.error);
	}

	QJsonArray overlapping;
	// A whole-track freeze mutes NOTHING: the play() substitution already
	// silences the source for the whole take, and leaving the clips untouched is
	// what keeps a whole-track freeze reversible by clearing one element.
	std::vector<Track::FrozenTake::MutedClip> muted;
	if (!wholeTrack)
	{
		muted = clipsInside(track, range, &overlapping);
	}

	track->addJournalCheckPoint();
	QString freezeError;
	if (!track->freezeTo(rendered.path, wholeTrack ? 0 : range.start, rendered.range.end,
			muted, &freezeError))
	{
		return ControlResult::failure(ControlErrorKind::Refused, freezeError);
	}

	QJsonObject before;
	before.insert(QStringLiteral("track"), control::trackIdOf(track));
	before.insert(QStringLiteral("frozen"), false);

	QJsonObject result = renderJson(rendered);
	const QJsonObject take = frozenStateJson(track);
	for (auto entry = take.constBegin(); entry != take.constEnd(); ++entry)
	{
		result.insert(entry.key(), entry.value());
	}
	result.insert(QStringLiteral("overlapping_clips"), overlapping);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("freeze.unfreeze"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(track)}}, true,
			ClauseFrozenTake));
	return ControlResult::success(result);
}

ControlResult freezeTrack(const QJsonObject& args)
{
	return freezeRange(args, false);
}

ControlResult freezeRegion(const QJsonObject& args)
{
	return freezeRange(args, true);
}

// ---------------------------------------------------------------------------
// freeze.unfreeze
// ---------------------------------------------------------------------------
ControlResult freezeUnfreeze(const QJsonObject& args)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	if (!track->isFrozen())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not frozen").arg(control::trackIdOf(track)));
	}

	QJsonObject before = frozenStateJson(track);
	track->addJournalCheckPoint();
	track->unfreeze();

	QJsonObject result = frozenStateJson(track);
	// The inverse NAMED here is the command a caller would run to freeze again;
	// the take that comes back off `control.undo` is the exact one the
	// checkpoint captured. Re-running freeze.track would render a NEW take.
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("freeze.track"),
			QJsonObject{{QStringLiteral("track"), control::trackIdOf(track)}}, true,
			ClauseFrozenTake));
	return ControlResult::success(result);
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------
void registerBounceInPlace(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("bounce.in_place");
	cmd.group = QStringLiteral("bounce");
	cmd.verb = QStringLiteral("in_place");
	cmd.description = QStringLiteral("Render one track's own output - its devices, fader, pan and "
		"sends, which is what a stem is - to a WAV file and return the file, its frame count and "
		"its sha256. With 'start' and 'end' (ticks) the file covers that region instead of the "
		"whole track. The session is NOT modified: the render runs in a child process against a "
		"serialised copy with every other track muted, so nothing in this instance changes and "
		"nothing is journalled. There is no interface for this (docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("out"), stringProperty()},
		{QStringLiteral("start"), integerProperty()},
		{QStringLiteral("end"), integerProperty()},
	}, {QStringLiteral("track")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
		{QStringLiteral("end_ticks"), integerProperty()},
	});
	// It writes an OUTPUT ARTEFACT, never project state - the same class, for
	// the same reason, as render.render.
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return bounceInPlace(args); };
	registry.registerCommand(cmd);
}

void registerFreezeTrack(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("freeze.track");
	cmd.group = QStringLiteral("freeze");
	cmd.verb = QStringLiteral("track");
	cmd.description = QStringLiteral("Bounce a track and make it PLAY THE RENDER instead of its "
		"clips: the engine queues the take for every pass inside its window and schedules none of "
		"the track's own playback, so the source is disabled for as long as this holds. The take "
		"carries the track's devices, fader, pan and sends, so it is summed at the mix level and "
		"not through the track's chain a second time, and the track's volume and effects controls "
		"are inert until freeze.unfreeze. The state is saved with the project and one control.undo "
		"takes it off (Track checkpoint). Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("out"), stringProperty()},
	}, {QStringLiteral("track")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("frozen"), booleanProperty()},
		{QStringLiteral("audio"), stringProperty()},
		{QStringLiteral("audio_ready"), booleanProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
		{QStringLiteral("end_ticks"), integerProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return freezeTrack(args); };
	registry.registerCommand(cmd);
}

void registerFreezeRegion(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("freeze.region");
	cmd.group = QStringLiteral("freeze");
	cmd.verb = QStringLiteral("region");
	cmd.description = QStringLiteral("Freeze one tick range of a track: the region is rendered "
		"and the take plays inside it, while the source keeps playing outside it. The clips that "
		"START inside the region are muted (and recorded, so freeze.unfreeze unmutes exactly those "
		"and nothing else); a clip that starts before the region and runs into it is left alone "
		"and named in 'overlapping_clips', because muting it would silence audio outside the "
		"region - that clip sounds twice inside the region (docs/KNOWN-LIMITATIONS.md). "
		"Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("start"), integerProperty()},
		{QStringLiteral("end"), integerProperty()},
		{QStringLiteral("out"), stringProperty()},
	}, {QStringLiteral("track"), QStringLiteral("start"), QStringLiteral("end")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("frozen"), booleanProperty()},
		{QStringLiteral("audio"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
		{QStringLiteral("end_ticks"), integerProperty()},
		{QStringLiteral("muted_clips"), integerProperty()},
		{QStringLiteral("overlapping_clips"), arrayProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return freezeRegion(args); };
	registry.registerCommand(cmd);
}

void registerFreezeUnfreeze(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("freeze.unfreeze");
	cmd.group = QStringLiteral("freeze");
	cmd.verb = QStringLiteral("unfreeze");
	cmd.description = QStringLiteral("Drop a track's frozen take: the track plays its own clips "
		"again, and the clips the freeze muted (freeze.region) are unmuted - exactly those, so a "
		"clip the user had muted themselves stays muted. The rendered file is left on disk: this "
		"is not a delete. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
	}, {QStringLiteral("track")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("frozen"), booleanProperty()},
		{QStringLiteral("audio"), stringProperty()},
		{QStringLiteral("muted_clips"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return freezeUnfreeze(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerFreezeCommands(ControlRegistry& registry)
{
	registerBounceInPlace(registry);
	registerFreezeTrack(registry);
	registerFreezeRegion(registry);
	registerFreezeUnfreeze(registry);
}

} // namespace lmms
