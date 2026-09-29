/*
 * ControlCommandsVcaEditMore.cpp - R3.5: the phase-locked split, trim, slip and fade
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

/*! Edit groups carry every edit type (relief plan R3.5). vca.edit_move propagated one media
 *  edit; docs/VCA-EDIT-GROUPS.md recorded "trim, slip, split and fades on a locked group are
 *  not propagated". These four close that, on vca.edit_move's own rules:
 *
 *   * the named clip is the ANCHOR; its PRE-edit span decides which member clips are locked
 *     to it (a member clip overlapping the span), a member with nothing there is reported
 *     `unlocked`, a member whose track is gone `skipped`;
 *   * every refusal - the lock's, and the edit's own (a split outside a clip, a trim that
 *     would empty one) - is checked for EVERY locked clip before anything is written, so a
 *     refused lock writes nothing at all;
 *   * each clip written takes its own live checkpoint first, and the registry merges one
 *     command's checkpoints into ONE undo step: one control.undo puts every member back.
 *
 *  What is locked is a relative edit. The split cuts every locked clip at the same song tick;
 *  the trim moves every locked clip's start (and end) edge by the anchor's own delta; the slip
 *  moves every locked clip's content offset by the anchor's delta; the fade sets the same fade
 *  lengths (and shapes) on every locked audio clip.
 */

#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "Clip.h"
#include "ClipEdits.h"
#include "ControlCommandsVcaShared.h"
#include "ControlRegistry.h"
#include "SampleClip.h"
#include "Track.h"

namespace lmms
{

using namespace control;
using namespace vcacontrol;

namespace
{

//! One locked clip and the track it is on.
struct Locked
{
	Track* track = nullptr;
	Clip* clip = nullptr;
};

//! The anchor and every member clip overlapping its pre-edit span, or false with the refusal.
bool collectLocked(const QJsonObject& args, VcaGroup** group, ClipRef* anchor,
	std::vector<Locked>* locked, QJsonArray* unlocked, QJsonArray* skipped, ControlResult* error)
{
	*group = resolveGroup(args, error);
	if (*group == nullptr) { return false; }
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), anchor, error)) { return false; }
	if (!checkLockable(*group, *anchor, error)) { return false; }
	const tick_t from = anchor->clip->startPosition().getTicks();
	const tick_t spanEnd = std::max(anchor->clip->endPosition().getTicks(), from + 1);
	for (int trackId : (*group)->editTracks())
	{
		ControlResult ignored;
		Track* member = resolveTrack(control::trackId(trackId), &ignored);
		if (member == nullptr) { skipped->append(control::trackId(trackId)); continue; }
		if (member == anchor->track) { locked->push_back({member, anchor->clip}); continue; }
		const std::vector<Clip*> inSpan = clipsInSpan(member, from, spanEnd);
		if (inSpan.empty()) { unlocked->append(trackIdOf(member)); continue; }
		for (Clip* clip : inSpan) { locked->push_back({member, clip}); }
	}
	return true;
}

QJsonObject editReply(const QJsonObject& args, VcaGroup* group, const ClipRef& anchor,
	const QJsonArray& edits, const QJsonArray& unlocked, const QJsonArray& skipped)
{
	QJsonObject result;
	result.insert(QStringLiteral("group"), vcaGroupId(group->id()));
	result.insert(QStringLiteral("clip"), args.value(QStringLiteral("clip")).toString());
	result.insert(QStringLiteral("anchor"), trackIdOf(anchor.track));
	result.insert(QStringLiteral("edits"), edits);
	result.insert(QStringLiteral("edited_count"), edits.size());
	result.insert(QStringLiteral("unlocked_tracks"), unlocked);
	result.insert(QStringLiteral("unlocked_count"), unlocked.size());
	result.insert(QStringLiteral("skipped_tracks"), skipped);
	result.insert(QStringLiteral("skipped_count"), skipped.size());
	return result;
}

QJsonObject lockedEditSchema()
{
	return objectSchema({
		{QStringLiteral("group"), stringProperty()},
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("anchor"), stringProperty()},
		{QStringLiteral("edits"), arrayProperty()},
		{QStringLiteral("edited_count"), integerProperty()},
		{QStringLiteral("unlocked_tracks"), arrayProperty()},
		{QStringLiteral("unlocked_count"), integerProperty()},
		{QStringLiteral("skipped_tracks"), arrayProperty()},
		{QStringLiteral("skipped_count"), integerProperty()},
	});
}

QJsonObject transactionFor(const QString& op, VcaGroup* group, const ClipRef& anchor, int count)
{
	QJsonObject before;
	before.insert(QStringLiteral("group"), vcaGroupId(group->id()));
	before.insert(QStringLiteral("anchor"), trackIdOf(anchor.track));
	before.insert(QStringLiteral("edited_count"), count);
	return transactionPayload(before,
		QStringLiteral("UNIMPLEMENTED: one call that reverses every locked edit"), QJsonObject(), true,
		QStringLiteral("composite checkpoint: a LIVE checkpoint (Clip, or Track for a split) was "
			"taken before every clip the lock wrote, and the registry merges one command's "
			"checkpoints into ONE undo step (ControlRegistry::runHandler -> "
			"ProjectJournal::mergeCheckpointsFrom), so one control.undo puts every member back - %1")
			.arg(op));
}

QJsonObject clipEdge(Track* track, Clip* clip)
{
	return QJsonObject{{QStringLiteral("track"), trackIdOf(track)},
		{QStringLiteral("position"), clip->startPosition().getTicks()},
		{QStringLiteral("length"), clip->length().getTicks()},
		{QStringLiteral("offset"), clip->startTimeOffset().getTicks()}};
}

// ---- vca.edit_split -------------------------------------------------------

ControlResult editSplit(const QJsonObject& args)
{
	VcaGroup* group = nullptr;
	ClipRef anchor;
	std::vector<Locked> locked;
	QJsonArray unlocked, skipped;
	ControlResult error;
	if (!collectLocked(args, &group, &anchor, &locked, &unlocked, &skipped, &error)) { return error; }
	const tick_t at = static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble());
	// A locked clip the cut does not fall strictly inside is left whole: the
	// lock is "every clip under the cut", and the anchor must be one of them.
	const auto inside = [at](const Clip* clip) {
		return at > clip->startPosition().getTicks() && at < clip->endPosition().getTicks();
	};
	if (!inside(anchor.clip))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'position' %1 is not strictly inside the anchor clip (%2..%3)")
				.arg(at).arg(anchor.clip->startPosition().getTicks()).arg(anchor.clip->endPosition().getTicks()));
	}
	QJsonArray edits;
	for (const Locked& entry : locked)
	{
		if (!inside(entry.clip)) { continue; }
		const tick_t start = entry.clip->startPosition().getTicks();
		const tick_t end = entry.clip->endPosition().getTicks();
		// clip.split's own sequence (ClipView::splitClip): checkpoint the track,
		// keep the new half from journalling itself, clone, resize both halves.
		entry.track->addJournalCheckPoint();
		entry.track->saveJournallingState(false);
		Clip* right = entry.clip->clone();
		entry.clip->changeLength(TimePos(at - start));
		entry.clip->setAutoResize(false);
		right->movePosition(TimePos(at));
		right->changeLength(TimePos(end - at));
		right->setStartTimeOffset(entry.clip->startTimeOffset() - entry.clip->length());
		right->setAutoResize(false);
		entry.track->restoreJournallingState();
		edits.append(QJsonObject{{QStringLiteral("track"), trackIdOf(entry.track)},
			{QStringLiteral("from"), start}, {QStringLiteral("at"), at}, {QStringLiteral("to"), end}});
	}
	QJsonObject result = editReply(args, group, anchor, edits, unlocked, skipped);
	result.insert(QStringLiteral("__transaction"), transactionFor(QStringLiteral("vca.edit_split"),
		group, anchor, edits.size()));
	return ControlResult::success(result);
}

// ---- vca.edit_trim --------------------------------------------------------

ControlResult editTrim(const QJsonObject& args)
{
	VcaGroup* group = nullptr;
	ClipRef anchor;
	std::vector<Locked> locked;
	QJsonArray unlocked, skipped;
	ControlResult error;
	if (!collectLocked(args, &group, &anchor, &locked, &unlocked, &skipped, &error)) { return error; }
	const tick_t anchorStart = anchor.clip->startPosition().getTicks();
	const tick_t anchorEnd = anchor.clip->endPosition().getTicks();
	const tick_t start = static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble());
	const tick_t end = args.contains(QStringLiteral("end"))
		? static_cast<tick_t>(args.value(QStringLiteral("end")).toDouble()) : anchorEnd;
	const tick_t dStart = start - anchorStart;
	const tick_t dEnd = end - anchorEnd;
	// The whole lock is planned before any clip moves: a trim that would empty (or
	// push before the song start) ANY locked clip refuses the lot.
	for (const Locked& entry : locked)
	{
		const tick_t s = entry.clip->startPosition().getTicks() + dStart;
		const tick_t e = entry.clip->endPosition().getTicks() + dEnd;
		if (e <= s || s < 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("the trim would leave %1's clip at %2..%3 - empty or before the song "
					"start - so the locked trim is refused and nothing moved")
					.arg(trackIdOf(entry.track)).arg(s).arg(e));
		}
	}
	QJsonArray edits;
	for (const Locked& entry : locked)
	{
		const tick_t s = entry.clip->startPosition().getTicks() + dStart;
		const tick_t e = entry.clip->endPosition().getTicks() + dEnd;
		// clip.trim's head-trim rule: the source offset moves against the start,
		// so the audio holds its song position.
		const tick_t offset = entry.clip->startTimeOffset().getTicks() - dStart;
		entry.clip->addJournalCheckPoint();
		entry.clip->movePosition(TimePos(s));
		entry.clip->changeLength(TimePos(e - s));
		entry.clip->setStartTimeOffset(TimePos(offset));
		entry.clip->setAutoResize(false);
		edits.append(clipEdge(entry.track, entry.clip));
	}
	QJsonObject result = editReply(args, group, anchor, edits, unlocked, skipped);
	result.insert(QStringLiteral("delta_start"), dStart);
	result.insert(QStringLiteral("delta_end"), dEnd);
	result.insert(QStringLiteral("__transaction"), transactionFor(QStringLiteral("vca.edit_trim"),
		group, anchor, edits.size()));
	return ControlResult::success(result);
}

// ---- vca.edit_slip --------------------------------------------------------

ControlResult editSlip(const QJsonObject& args)
{
	VcaGroup* group = nullptr;
	ClipRef anchor;
	std::vector<Locked> locked;
	QJsonArray unlocked, skipped;
	ControlResult error;
	if (!collectLocked(args, &group, &anchor, &locked, &unlocked, &skipped, &error)) { return error; }
	const tick_t delta = static_cast<tick_t>(args.value(QStringLiteral("offset")).toDouble())
		- anchor.clip->startTimeOffset().getTicks();
	QJsonArray edits;
	for (const Locked& entry : locked)
	{
		entry.clip->addJournalCheckPoint();
		entry.clip->setStartTimeOffset(TimePos(entry.clip->startTimeOffset().getTicks() + delta));
		entry.clip->setAutoResize(false);
		edits.append(clipEdge(entry.track, entry.clip));
	}
	QJsonObject result = editReply(args, group, anchor, edits, unlocked, skipped);
	result.insert(QStringLiteral("delta"), delta);
	result.insert(QStringLiteral("__transaction"), transactionFor(QStringLiteral("vca.edit_slip"),
		group, anchor, edits.size()));
	return ControlResult::success(result);
}

// ---- vca.edit_fade --------------------------------------------------------

//! The fit check for vca.edit_fade: empty when the fades fit every locked clip, else the refusal.
//! The whole lock is checked first, so a fade that does not fit ANY locked clip refuses the lot.
QString fadeMisfit(const std::vector<Locked>& locked, bool hasIn, int fadeIn, bool hasOut, int fadeOut)
{
	for (const Locked& entry : locked)
	{
		const ClipEdits current = entry.clip->clipEdits();
		const int in = hasIn ? fadeIn : current.fadeInTicks;
		const int out = hasOut ? fadeOut : current.fadeOutTicks;
		if (dynamic_cast<SampleClip*>(entry.clip) == nullptr || in + out > entry.clip->length().getTicks())
		{
			return QStringLiteral("the fades (%1 in, %2 out) do not fit %3's locked clip (an audio clip of "
				"%4 ticks is needed), so the locked fade is refused and nothing changed")
				.arg(in).arg(out).arg(trackIdOf(entry.track)).arg(entry.clip->length().getTicks());
		}
	}
	return QString();
}

ControlResult editFade(const QJsonObject& args)
{
	VcaGroup* group = nullptr;
	ClipRef anchor;
	std::vector<Locked> locked;
	QJsonArray unlocked, skipped;
	ControlResult error;
	if (!collectLocked(args, &group, &anchor, &locked, &unlocked, &skipped, &error)) { return error; }
	const bool hasIn = args.contains(QStringLiteral("fade_in"));
	const bool hasOut = args.contains(QStringLiteral("fade_out"));
	if (!hasIn && !hasOut)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("name 'fade_in', 'fade_out' or both"));
	}
	const int fadeIn = args.value(QStringLiteral("fade_in")).toInt();
	const int fadeOut = args.value(QStringLiteral("fade_out")).toInt();
	// Fades live on audio clips (clip.set_fade's own rule).
	if (const QString misfit = fadeMisfit(locked, hasIn, fadeIn, hasOut, fadeOut); !misfit.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, misfit);
	}
	QJsonArray edits;
	for (const Locked& entry : locked)
	{
		ClipEdits after = entry.clip->clipEdits();
		if (hasIn) { after.fadeInTicks = fadeIn; }
		if (hasOut) { after.fadeOutTicks = fadeOut; }
		entry.clip->addJournalCheckPoint();
		entry.clip->setClipEdits(after);
		edits.append(QJsonObject{{QStringLiteral("track"), trackIdOf(entry.track)},
			{QStringLiteral("fade_in"), after.fadeInTicks}, {QStringLiteral("fade_out"), after.fadeOutTicks}});
	}
	QJsonObject result = editReply(args, group, anchor, edits, unlocked, skipped);
	result.insert(QStringLiteral("__transaction"), transactionFor(QStringLiteral("vca.edit_fade"),
		group, anchor, edits.size()));
	return ControlResult::success(result);
}

void registerOne(ControlRegistry& registry, const char* verb, const QString& description,
	QJsonObject argsProperties, QJsonArray required, QJsonObject extraResult,
	ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("vca");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	argsProperties.insert(QStringLiteral("group"), stringProperty());
	argsProperties.insert(QStringLiteral("clip"), stringProperty());
	required.prepend(QStringLiteral("clip"));
	required.prepend(QStringLiteral("group"));
	QJsonObject args{{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), argsProperties}, {QStringLiteral("required"), required},
		{QStringLiteral("additionalProperties"), false}};
	cmd.argsSchema = args;
	QJsonObject result = lockedEditSchema();
	QJsonObject properties = result.value(QStringLiteral("properties")).toObject();
	for (auto it = extraResult.begin(); it != extraResult.end(); ++it) { properties.insert(it.key(), it.value()); }
	result.insert(QStringLiteral("properties"), properties);
	cmd.resultSchema = result;
	cmd.mutating = true;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerVcaEditMoreCommands(ControlRegistry& registry)
{
	const QString rules = QStringLiteral(" The named clip is the anchor; a member track's clips that "
		"overlap the anchor's PRE-edit span are locked to it, a member with nothing there is "
		"reported in unlocked_tracks and a gone track in skipped_tracks. Refused, typed, when the "
		"group's phase lock is off, when the anchor's track is not in the edit set, or when the set "
		"has fewer than two live tracks - and every edit refusal is checked for every locked clip "
		"first, so a refusal writes nothing. One control.undo reverses every member.");
	registerOne(registry, "edit_split", QStringLiteral("Split every locked clip at one song tick "
		"(`position`), so the members are cut at exactly the same place - the phase-locked "
		"multitrack split. A locked clip the cut does not fall inside is left whole; the anchor must "
		"contain it.") + rules,
		QJsonObject{{QStringLiteral("position"), integerProperty(0, MaxSongLength)}},
		QJsonArray{QStringLiteral("position")}, QJsonObject(), &editSplit);
	registerOne(registry, "edit_trim", QStringLiteral("Trim the anchor's start edge to `start` (and "
		"its end to `end`) and move every locked clip's edges by the SAME deltas, holding each "
		"clip's audio at its song position (clip.trim's head-trim rule). `delta_start`/`delta_end` "
		"report the deltas.") + rules,
		QJsonObject{{QStringLiteral("start"), integerProperty(0, MaxSongLength)},
			{QStringLiteral("end"), integerProperty(1, MaxSongLength)}},
		QJsonArray{QStringLiteral("start")},
		QJsonObject{{QStringLiteral("delta_start"), integerProperty()}, {QStringLiteral("delta_end"), integerProperty()}},
		&editTrim);
	registerOne(registry, "edit_slip", QStringLiteral("Slip the anchor's content to `offset` and "
		"every locked clip's content by the SAME delta; no clip's position or length moves.") + rules,
		QJsonObject{{QStringLiteral("offset"), integerProperty(-MaxSongLength, MaxSongLength)}},
		QJsonArray{QStringLiteral("offset")},
		QJsonObject{{QStringLiteral("delta"), integerProperty()}}, &editSlip);
	registerOne(registry, "edit_fade", QStringLiteral("Set the same fade-in and/or fade-out length "
		"(ticks) on every locked AUDIO clip. A locked MIDI clip, or a fade longer than a locked "
		"clip, refuses the whole edit.") + rules,
		QJsonObject{{QStringLiteral("fade_in"), integerProperty(0, MaxSongLength)},
			{QStringLiteral("fade_out"), integerProperty(0, MaxSongLength)}},
		QJsonArray(), QJsonObject(), &editFade);
}

} // namespace lmms
