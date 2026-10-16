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

#include "ArrangementEdits.h"
#include "BounceInPlace.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlBounceSupport.h"
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

} // namespace

std::vector<Track::FrozenTake::MutedClip> control::isolateFreezeRegion(Track* track,
		const BounceInPlace::Range& range, QJsonArray* cut)
{
	std::vector<std::pair<tick_t, tick_t>> split;
	std::vector<Track::FrozenTake::MutedClip> muted;
	for (const auto& piece : arrangement::isolateRegion(track, range.start, range.end, &split))
	{
		muted.push_back(Track::FrozenTake::MutedClip{piece.first, piece.second});
	}
	for (const auto& clip : split)
	{
		cut->append(QJsonObject{{QStringLiteral("start_ticks"), static_cast<qint64>(clip.first)},
			{QStringLiteral("length_ticks"), static_cast<qint64>(clip.second)}});
	}
	return muted;
}

void control::insertFrozenRecovery(Track* track, QJsonObject* report)
{
	if (!track->frozenTakeFileMissing()) { return; }
	report->insert(QStringLiteral("audio_missing"), true);
	report->insert(QStringLiteral("recovery"), QJsonObject{
		{QStringLiteral("relink"), QStringLiteral("project.missing_assets lists the take (track/frozenAudio); "
			"project.relink points it at the moved file, then reopen the project")},
		{QStringLiteral("unfreeze"), QStringLiteral("freeze.unfreeze gives the track its own clips back; "
			"control.undo restores the take")}});
}

namespace
{

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
	insertFrozenRecovery(track, &result);
	return result;
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
	track->addJournalCheckPoint();
	// A whole-track freeze mutes NOTHING: the play() substitution already
	// silences the source for the whole take. A region freeze first cuts the clips that cross
	// the region's edges at those edges (one undo step with the freeze), so the pieces inside are
	// muted and the pieces outside keep playing: the source is silent only inside the region.
	std::vector<Track::FrozenTake::MutedClip> muted;
	if (!wholeTrack)
	{
		muted = control::isolateFreezeRegion(track, range, &overlapping);
	}

	QString freezeError;
	if (!track->freezeTo(rendered.path, wholeTrack ? 0 : range.start, rendered.range.end,
			muted, &freezeError, !wholeTrack))
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
	// The inverse NAMED here is control.undo: the checkpoint this call took carries the take
	// that came off, and one undo puts that exact take back. freeze.track is NOT that - it
	// renders a NEW take for the WHOLE track, which cannot recreate a regional take.
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("control.undo"), {}, true,
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
		"nothing is journalled. The track menu's Bounce in place runs it; bounce.land is the verb "
		"that also puts the render on a new audio track. This command keeps its synchronous result "
		"contract for compatibility; use render.queue_bounce for a cancellable background job.");
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
		"takes it off (Track checkpoint). Reversible through the ProjectJournal. This command keeps "
		"its synchronous result contract for compatibility; use render.queue_bounce for a cancellable job.");
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
		{QStringLiteral("bytes"), numberProperty()},
		{QStringLiteral("muted_clips"), numberProperty()},
		{QStringLiteral("audio_missing"), booleanProperty()},
		{QStringLiteral("recovery"), objectProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("overlapping_clips"), arrayProperty()},
		{QStringLiteral("sample_rate"), numberProperty()},
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
		"and the take plays inside it, while the source keeps playing outside it. Clips that CROSS the "
		"region's edges are first cut at those edges (listed in 'overlapping_clips', as they were), then "
		"the pieces inside the region are muted (and recorded, so freeze.unfreeze unmutes exactly those "
		"and nothing else): the source is silent only inside the region. A take whose file has moved "
		"reports audio_missing and 'recovery' (project.missing_assets / project.relink, or "
		"freeze.unfreeze); a missing take never silences the source. "
		"Reversible through the ProjectJournal (Track checkpoint). This command keeps its synchronous "
		"result contract for compatibility; use render.queue_bounce for a cancellable job.");
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
		{QStringLiteral("audio_missing"), booleanProperty()},
		{QStringLiteral("recovery"), objectProperty()},
		{QStringLiteral("overlapping_clips"), arrayProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("audio_ready"), booleanProperty()},
		{QStringLiteral("bytes"), numberProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("sample_rate"), numberProperty()},
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
		{QStringLiteral("audio_missing"), booleanProperty()},
		{QStringLiteral("recovery"), objectProperty()},
		{QStringLiteral("audio_ready"), booleanProperty()},
		{QStringLiteral("end_ticks"), numberProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("start_ticks"), numberProperty()},
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
