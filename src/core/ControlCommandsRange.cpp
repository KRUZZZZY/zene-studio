/*
 * ControlCommandsRange.cpp - range.*: a time range over tracks and the edits it carries
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

/*! ED-011/ED-012 (0.5.0 L04): the arranger's time-range selection and the edits over it - split
 *  at its edges, delete (closing the gap with ripple), crop, copy and paste, duplicate, insert
 *  time. Each edit checkpoints every track the range covers first, so it is ONE undo step
 *  (Track checkpoints carry their clips). The range itself is interface state. */

#include <algorithm>

#include <QDomDocument>
#include <QJsonArray>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

#include <QPointer>

namespace lmms
{

using namespace control;

namespace
{

//! range.copy's clipboard: each clip's serialised state, its track's place in the range and its
//! position relative to the range start.
struct CopiedClip
{
	int trackIndex;
	tick_t offset;
	QString xml;
	Track::Type type;  //!< the source track's: a clip pastes only onto a track of the same kind
};
QVector<CopiedClip> g_clipboard;
tick_t g_clipboardLength = 0;

QJsonObject rangeJson()
{
	const ArrangerRange& range = ArrangerRange::instance();
	QJsonArray tracks;
	for (Track* track : range.tracks()) { tracks.append(trackIdOf(track)); }
	return {{QStringLiteral("active"), range.active()}, {QStringLiteral("start"), range.start()},
		{QStringLiteral("end"), range.end()}, {QStringLiteral("all_tracks"), range.allTracks()},
		{QStringLiteral("tracks"), tracks}, {QStringLiteral("clipboard"), g_clipboard.size()}};
}

//! The first of @a tracks a lock keeps range edits off (the track, or a clip on it, is locked), or nullptr. The
//! registry's lock check reads the ids a call names, and a range edit names none (WF-044).
const Track* lockedTrack(const QVector<Track*>& tracks)
{
	const EditLocks& locks = EditLocks::instance();
	for (const Track* track : tracks)
	{
		const auto& clips = track->getClips();
		if (locks.trackLocked(track->id()) || std::any_of(clips.begin(), clips.end(), [&locks](const Clip* c) { return locks.locks(c); }))
		{
			return track;
		}
	}
	return nullptr;
}

ControlResult lockedOut(const Track* track)
{
	return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 (or a clip on it) is locked against editing "
		"(track.set_locked / clip.set_locked unlocks it)").arg(trackIdOf(track)));
}

//! The active range's tracks, checkpointed as one undo step; false (with @a error) when there is
//! no range or a lock covers one of its tracks.
bool beginEdit(QVector<Track*>* tracks, ControlResult* error)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)"));
		return false;
	}
	*tracks = range.tracks();
	if (const Track* locked = lockedTrack(*tracks))
	{
		*error = lockedOut(locked);
		return false;
	}
	QVector<JournallingObject*> objects;
	for (Track* track : *tracks) { objects.append(track); }
	control::addUndoStep(objects);
	return true;
}

ControlResult finishEdit(const QString& command, QJsonObject result)
{
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("range"), rangeJson());
	// The inverse is control.undo, NOT this command with empty args: every range edit
	// checkpoints the tracks it covers as one step (beginEdit above), so replaying
	// control.undo puts them back, whereas replaying range.delete deleted again.
	result.insert(QStringLiteral("__transaction"), transactionPayload(rangeJson(), QStringLiteral("control.undo"), {}, true,
		QStringLiteral("Track checkpoints: every track the range covers was checkpointed before the edit, as one step")));
	return ControlResult::success(result);
}

ControlResult setRange(const QJsonObject& args)
{
	QVector<int> ids;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray())
	{
		ControlResult error;
		Track* track = resolveTrack(value.toString(), &error);
		if (track == nullptr) { return error; }
		ids.append(track->id());
	}
	ArrangerRange::instance().set(args.value(QStringLiteral("start")).toInt(), args.value(QStringLiteral("end")).toInt(), ids,
		args.value(QStringLiteral("all")).toBool(ids.isEmpty()));
	return ControlResult::success(rangeJson());
}

ControlResult splitRange(const QJsonObject&)
{
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	int split = 0;
	for (Track* track : tracks) { split += arrangement::splitTrackAt(track, range.start()) + arrangement::splitTrackAt(track, range.end()); }
	return finishEdit(QStringLiteral("range.split"), {{QStringLiteral("split"), split}});
}

//! The fades a range from @a from to @a to sets on a clip spanning @a start..@a end: in over the part
//! at its start, out over the part at its end. False for a clip wholly inside the range - it has no
//! edge at either end of it, so nothing is faded (review L05-L09-L08-1 #11: both rules used to fire
//! and the clamp dropped the fade-out).
bool rangeFades(ClipEdits* edits, tick_t start, tick_t end, tick_t from, tick_t to)
{
	if (start >= from && end <= to) { return false; }
	if (start >= from && start < to) { edits->fadeInTicks = static_cast<int>(std::min(to, end) - start); }
	if (end > from && end <= to) { edits->fadeOutTicks = static_cast<int>(end - std::max(from, start)); }
	// A clip spanning the whole range satisfies neither edge, so neither fade was touched:
	// reporting true counted it as faded (BUGS_FOUND 11.42).
	return edits->fadeInTicks > 0 || edits->fadeOutTicks > 0;
}

//! ED-178: the fades a range sets on one sample clip - fixed lengths when @a args name them (batch
//! fades), else in over the part of the range at the clip's start and out over the part at its end.
bool fadeFromRange(SampleClip* clip, tick_t from, tick_t to, const QJsonObject& args, const FadeShape* shape)
{
	ClipEdits edits = clip->clipEdits();
	const tick_t start = clip->startPosition().getTicks();
	const tick_t end = clip->endPosition().getTicks();
	const int length = static_cast<int>(end - start);
	const bool fixed = args.contains(QStringLiteral("fade_in")) || args.contains(QStringLiteral("fade_out"));
	if (fixed)
	{
		edits.fadeInTicks = args.value(QStringLiteral("fade_in")).toInt(edits.fadeInTicks);
		edits.fadeOutTicks = args.value(QStringLiteral("fade_out")).toInt(edits.fadeOutTicks);
	}
	else if (!rangeFades(&edits, start, end, from, to)) { return false; }
	if (shape != nullptr) { edits.fadeInShape = edits.fadeOutShape = *shape; }
	edits.fadeInTicks = std::clamp(edits.fadeInTicks, 0, length);
	edits.fadeOutTicks = std::clamp(edits.fadeOutTicks, 0, length - edits.fadeInTicks);
	clip->setClipEdits(edits);
	return true;
}

ControlResult fadeRange(const QJsonObject& args)
{
	FadeShape shape = FadeShape::Linear;
	const bool hasShape = args.contains(QStringLiteral("shape"));
	if (hasShape && !fadeShapeFromName(args.value(QStringLiteral("shape")).toString(), &shape))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'shape' is linear, exponential, logarithmic, s_curve or equal_power"));
	}
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	int faded = 0;
	for (Track* track : tracks)
	{
		for (Clip* clip : track->getClips())
		{
			auto* sample = dynamic_cast<SampleClip*>(clip);
			if (sample == nullptr || clip->endPosition().getTicks() <= range.start() || clip->startPosition().getTicks() >= range.end()) { continue; }
			faded += static_cast<int>(fadeFromRange(sample, range.start(), range.end(), args, hasShape ? &shape : nullptr));  // a clip wholly inside is not faded
		}
	}
	return finishEdit(QStringLiteral("range.fade"), {{QStringLiteral("faded"), faded}});
}

//! Ripple 'all' (ED-043): the tracks a range delete leaves alone, which close its gap too.
QVector<Track*> rippledOthers(const QVector<Track*>& covered)
{
	QVector<Track*> others;
	if (arrangement::modes().ripple != arrangement::Ripple::All) { return others; }
	for (Track* track : Engine::getSong()->tracks())
	{
		if (!covered.contains(track)) { others.append(track); }
	}
	return others;
}

//! Close the gap on @a others, checkpointed into the same step as the delete.
void rippleOthers(const QVector<Track*>& others, tick_t start, tick_t end)
{
	if (others.isEmpty()) { return; }
	QVector<JournallingObject*> objects;
	for (Track* track : others) { objects.append(track); }
	control::addUndoStep(objects);
	for (Track* track : others) { arrangement::shiftClipsFrom(track, end, start - end); }
}

ControlResult deleteRange(const QJsonObject& args)
{
	const ArrangerRange& range = ArrangerRange::instance();
	// Without 'ripple' the arranger's ripple mode decides (ED-043).
	const bool ripple = args.value(QStringLiteral("ripple")).toBool(arrangement::modes().ripple != arrangement::Ripple::Off);
	const QVector<Track*> others = ripple ? rippledOthers(range.tracks()) : QVector<Track*>{};
	if (const Track* locked = lockedTrack(others)) { return lockedOut(locked); }  // asked before anything is checkpointed
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	rippleOthers(others, range.start(), range.end());
	int removed = 0;
	for (Track* track : tracks) { removed += arrangement::deleteRange(track, range.start(), range.end(), ripple); }
	return finishEdit(QStringLiteral("range.delete"), {{QStringLiteral("removed"), removed}, {QStringLiteral("ripple"), ripple}});
}

ControlResult cropRange(const QJsonObject&)
{
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	int removed = 0;
	for (Track* track : tracks) { removed += arrangement::cropToRange(track, range.start(), range.end()); }
	return finishEdit(QStringLiteral("range.crop"), {{QStringLiteral("removed"), removed}});
}

ControlResult insertTime(const QJsonObject& args)
{
	const ArrangerRange& range = ArrangerRange::instance();
	const tick_t length = args.value(QStringLiteral("length")).toInt(range.end() - range.start());
	// No time to insert: refused before the edge split, which would cut clips for nothing.
	if (args.contains(QStringLiteral("length")) && length < 1)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'length' is at least one tick"));
	}
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	int moved = 0;
	for (Track* track : tracks)
	{
		arrangement::splitTrackAt(track, range.start());
		moved += arrangement::shiftClipsFrom(track, range.start(), length);
	}
	return finishEdit(QStringLiteral("range.insert_time"), {{QStringLiteral("moved"), moved}, {QStringLiteral("length"), length}});
}

ControlResult copyRange(const QJsonObject&)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)")); }
	g_clipboard.clear();
	const QVector<Track*> tracks = range.tracks();
	for (int i = 0; i < tracks.size(); ++i)
	{
		for (Clip* clip : arrangement::clipsWithin(tracks.at(i), range.start(), range.end()))
		{
			QDomDocument doc;
			QDomElement root = doc.createElement(QStringLiteral("clip"));
			doc.appendChild(root);
			clip->saveState(doc, root);
			g_clipboard.append({i, clip->startPosition().getTicks() - range.start(), doc.toString(), tracks.at(i)->type()});
		}
	}
	g_clipboardLength = range.end() - range.start();
	return ControlResult::success({{QStringLiteral("copied"), g_clipboard.size()}, {QStringLiteral("length"), g_clipboardLength}});
}

//! Recreate the clipboard's clips at @a position on @a tracks (by their place in the range).
int pasteAt(const QVector<Track*>& tracks, tick_t position)
{
	int pasted = 0;
	for (const CopiedClip& copied : g_clipboard)
	{
		if (copied.trackIndex >= tracks.size() || tracks.at(copied.trackIndex)->type() != copied.type) { continue; }
		QDomDocument doc;
		doc.setContent(copied.xml);
		Track* track = tracks.at(copied.trackIndex);
		Clip* clip = track->createClip(TimePos(position + copied.offset));
		clip->restoreState(doc.documentElement().firstChildElement());
		// A range-pasted clip is an independent ordinary clip, not another
		// member of the source take group.
		clip->clearTakeGroupId();
		control::assignTakeLane(clip, 0);  // the lane tag's one writer (ControlCommandsComp.cpp)
		clip->movePosition(TimePos(position + copied.offset));
		++pasted;
	}
	return pasted;
}

ControlResult pasteRange(const QJsonObject& args)
{
	if (g_clipboard.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("nothing copied (range.copy)")); }
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const tick_t position = args.value(QStringLiteral("position")).toInt(ArrangerRange::instance().start());
	return finishEdit(QStringLiteral("range.paste"), {{QStringLiteral("pasted"), pasteAt(tracks, position)}, {QStringLiteral("position"), position}});
}

ControlResult duplicateRange(const QJsonObject&)
{
	const ControlResult copied = copyRange({});
	if (!copied.ok) { return copied; }
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	const int pasted = pasteAt(tracks, range.end());
	const tick_t length = range.end() - range.start();
	ArrangerRange::instance().set(range.end(), range.end() + length, range.trackIds(), range.allTracks());
	return finishEdit(QStringLiteral("range.duplicate"), {{QStringLiteral("pasted"), pasted}});
}

//! ED-031: clip.consolidate over the range on each covered sample track (it renders and records itself).
ControlResult consolidateRange(const QJsonObject&)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)")); }
	QJsonArray done;
	QJsonArray refused;
	for (Track* track : range.tracks())
	{
		if (track->type() != Track::Type::Sample) { continue; }
		const ControlResult ran = ControlRegistry::instance()->invoke(QStringLiteral("clip.consolidate"),
			{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("start"), range.start()}, {QStringLiteral("end"), range.end()}});
		(ran.ok ? done : refused).append(QJsonObject{{QStringLiteral("track"), trackIdOf(track)},
			{QStringLiteral("detail"), ran.ok ? QString() : ran.errorMessage}});
	}
	return ControlResult::success({{QStringLiteral("consolidated"), done}, {QStringLiteral("refused"), refused}});
}

/*! ED-009: an audition - play from a point, the other tracks silenced, everything put back on
 *  stop. Not a project edit: the mutes are set with journalling off and restored exactly. */
struct Audition
{
	bool active = false;
	bool wasPlaying = false;
	tick_t returnTo = 0;
	QVector<QPair<QPointer<Track>, bool>> muted;  //!< each silenced track and its own mute before
};
Audition g_audition;

ControlResult auditionStop(const QJsonObject&);

//! Set @a track's mute without an undo step (an audition is not a project edit).
void setMuteUnrecorded(Track* track, bool muted)
{
	BoolModel* mute = track->getMutedModel();
	const bool journalling = mute->isJournalling();
	mute->setJournalling(false);
	mute->setValue(muted);
	mute->setJournalling(journalling);
}

//! Silence every track but @a solo, and make @a solo heard even when it was muted; how many were silenced.
int silenceAllBut(Track* solo)
{
	int silenced = 0;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (track == solo && !track->isMuted()) { continue; }
		g_audition.muted.append({track, track->isMuted()});
		setMuteUnrecorded(track, track != solo);
		silenced += track != solo ? 1 : 0;
	}
	return silenced;
}

ControlResult auditionStart(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	ControlResult error;
	Track* solo = args.contains(QStringLiteral("track")) ? resolveTrack(args.value(QStringLiteral("track")).toString(), &error) : nullptr;
	if (args.contains(QStringLiteral("track")) && solo == nullptr) { return error; }
	if (g_audition.active) { auditionStop({}); }  // only once the new one is known to start
	g_audition = {true, song->isPlaying(), song->getPlayPos().getTicks(), {}};
	const int silenced = solo != nullptr ? silenceAllBut(solo) : 0;
	song->setPlayPos(args.value(QStringLiteral("position")).toInt(), Song::PlayMode::Song);
	if (!song->isPlaying()) { song->playSong(); }
	return ControlResult::success({{QStringLiteral("auditioning"), true}, {QStringLiteral("silenced"), silenced}});
}

ControlResult auditionStop(const QJsonObject&)
{
	if (!g_audition.active) { return ControlResult::success({{QStringLiteral("auditioning"), false}}); }
	Song* song = Engine::getSong();
	if (!g_audition.wasPlaying) { song->stop(); }
	for (const auto& [track, wasMuted] : g_audition.muted)
	{
		if (!track.isNull()) { setMuteUnrecorded(track.data(), wasMuted); }
	}
	// The play position comes back whether or not the transport was running (the start moved it either way).
	song->setPlayPos(g_audition.returnTo, Song::PlayMode::Song);
	g_audition = {};
	return ControlResult::success({{QStringLiteral("auditioning"), false}});
}

const QStringList& rippleNames()
{
	static const QStringList names{QStringLiteral("off"), QStringLiteral("track"), QStringLiteral("all")};
	return names;
}

const QStringList& overlapNames()
{
	static const QStringList names{QStringLiteral("free"), QStringLiteral("trim"), QStringLiteral("crossfade")};
	return names;
}

QJsonObject modesJson()
{
	const arrangement::Modes current = arrangement::modes();
	return {{QStringLiteral("ripple"), rippleNames().at(static_cast<int>(current.ripple))},
		{QStringLiteral("overlap"), overlapNames().at(static_cast<int>(current.overlap))},
		{QStringLiteral("snap_to_edges"), current.snapToEdges},
		{QStringLiteral("automation_follows_clips"), current.automationFollowsClips}, {QStringLiteral("auto_fade_ms"), current.autoFadeMs},
		{QStringLiteral("show_transients"), current.showTransients}, {QStringLiteral("snap_to_zero_crossings"), current.snapToZeroCrossings}};
}

ControlResult setModes(const QJsonObject& args)
{
	arrangement::Modes next = arrangement::modes();
	if (args.contains(QStringLiteral("ripple"))) { next.ripple = static_cast<arrangement::Ripple>(rippleNames().indexOf(args.value(QStringLiteral("ripple")).toString())); }
	if (args.contains(QStringLiteral("overlap"))) { next.overlap = static_cast<arrangement::Overlap>(overlapNames().indexOf(args.value(QStringLiteral("overlap")).toString())); }
	if (args.contains(QStringLiteral("snap_to_edges"))) { next.snapToEdges = args.value(QStringLiteral("snap_to_edges")).toBool(); }
	if (args.contains(QStringLiteral("automation_follows_clips"))) { next.automationFollowsClips = args.value(QStringLiteral("automation_follows_clips")).toBool(); }
	if (args.contains(QStringLiteral("auto_fade_ms"))) { next.autoFadeMs = args.value(QStringLiteral("auto_fade_ms")).toInt(); }
	if (args.contains(QStringLiteral("show_transients"))) { next.showTransients = args.value(QStringLiteral("show_transients")).toBool(); }
	next.snapToZeroCrossings = args.value(QStringLiteral("snap_to_zero_crossings")).toBool(next.snapToZeroCrossings);
	arrangement::setModes(next);
	return ControlResult::success(modesJson());
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("range.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerRangeCommands(ControlRegistry& registry)
{
	arrangement::loadModes();  // the audio thread's copy of the automatic fade
	ControlCommand modes;
	modes.id = QStringLiteral("arrangement.modes");
	modes.group = QStringLiteral("arrangement");
	modes.verb = QStringLiteral("modes");
	modes.description = QStringLiteral("The arranger's editing modes, set and answered: 'ripple' (off, track, all - a deletion "
		"closes its gap on the clip's track or on every track), 'overlap' (free, trim, crossfade - what a clip moved onto "
		"others does to them; trim by default, and never to a locked clip: such a move is refused), 'snap_to_edges' (moving a clip also snaps to other clips' edges and to markers) and "
		"'automation_follows_clips' (a clip's move carries the points of the automation lanes following its track) and "
		"'auto_fade_ms' (0..50: every sample clip fades in and out over at least that long, so no edit clicks; 0 off) and "
		"'show_transients' (sample clips mark their audio's hits) and 'snap_to_zero_crossings' (a split or trim of a sample "
		"clip - the knife, clip.split, clip.trim - moves to the nearest zero crossing within 10 ms, frame-exact). The "
		"arranger's gestures and clip.move / clip.delete / range.delete follow them. An editing preference kept in the "
		"config file, not project state.");
	modes.argsSchema = objectSchema({{QStringLiteral("ripple"), enumProperty(rippleNames())},
		{QStringLiteral("overlap"), enumProperty(overlapNames())}, {QStringLiteral("snap_to_edges"), booleanProperty()},
		{QStringLiteral("automation_follows_clips"), booleanProperty()}, {QStringLiteral("auto_fade_ms"), integerProperty(0, arrangement::MaxAutoFadeMs)},
		{QStringLiteral("show_transients"), booleanProperty()}, {QStringLiteral("snap_to_zero_crossings"), booleanProperty()}});
	modes.resultSchema = objectSchema({{QStringLiteral("ripple"), stringProperty()}, {QStringLiteral("overlap"), stringProperty()},
		{QStringLiteral("snap_to_edges"), booleanProperty()}, {QStringLiteral("automation_follows_clips"), booleanProperty()},
		{QStringLiteral("auto_fade_ms"), integerProperty(0, arrangement::MaxAutoFadeMs)}, {QStringLiteral("show_transients"), booleanProperty()},
		{QStringLiteral("snap_to_zero_crossings"), booleanProperty()}});
	modes.handler = setModes;
	registry.registerCommand(modes);

	ControlCommand start;
	start.id = QStringLiteral("transport.audition_start");
	start.group = QStringLiteral("transport");
	start.verb = QStringLiteral("audition_start");
	start.description = QStringLiteral("Audition: play from 'position' (ticks), with every track but 'track' silenced when one "
		"is given - the arranger's audition tool while the button is held. transport.audition_stop puts the mutes, the "
		"transport and the play position back exactly. Not a project edit: nothing is recorded.");
	start.argsSchema = objectSchema({{QStringLiteral("position"), tickProperty()}, {QStringLiteral("track"), stringProperty()}},
		{QStringLiteral("position")});
	start.resultSchema = objectProperty();
	start.handler = auditionStart;
	registry.registerCommand(start);

	ControlCommand stop;
	stop.id = QStringLiteral("transport.audition_stop");
	stop.group = QStringLiteral("transport");
	stop.verb = QStringLiteral("audition_stop");
	stop.description = QStringLiteral("End an audition: the silenced tracks' own mutes, the transport and the play position "
		"come back as they were.");
	stop.argsSchema = objectSchema({});
	stop.resultSchema = objectProperty();
	stop.handler = auditionStop;
	registry.registerCommand(stop);

	const QString oneStep = QStringLiteral(" One undo step (every covered track is checkpointed).");
	add(registry, "set", QStringLiteral("Select a time range from 'start' to 'end' (ticks) over 'tracks' (trk ids), or "
		"over every track ('all', the default with no tracks). Interface state, drawn in the arranger."),
		objectSchema({{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()},
			{QStringLiteral("tracks"), arrayProperty()}, {QStringLiteral("all"), booleanProperty()}},
			{QStringLiteral("start"), QStringLiteral("end")}), false, setRange);
	add(registry, "get", QStringLiteral("The range: active, start, end, its tracks, and how many clips are copied."),
		objectSchema({}), false, [](const QJsonObject&) { return ControlResult::success(rangeJson()); });
	add(registry, "clear", QStringLiteral("Clear the range."), objectSchema({}), false, [](const QJsonObject&) {
		ArrangerRange::instance().clear();
		return ControlResult::success(rangeJson());
	});
	add(registry, "split", QStringLiteral("Split every clip on the range's tracks at its start and its end.") + oneStep,
		objectSchema({}), true, splitRange);
	add(registry, "delete", QStringLiteral("Remove what is inside the range on its tracks (clips crossing an edge are "
		"split there first); with 'ripple', later clips move left to close the gap.") + oneStep,
		objectSchema({{QStringLiteral("ripple"), booleanProperty()}}), true, deleteRange);
	add(registry, "fade", QStringLiteral("Fade the sample clips the range touches: a clip whose start is inside the range "
		"fades in over the part of the range it covers, one whose end is inside fades out - across a cut between two "
		"clips that is a crossfade. With 'fade_in' / 'fade_out' (ticks) every clip in the range gets those lengths "
		"instead (batch fades); 'shape' sets both shapes.") + oneStep,
		objectSchema({{QStringLiteral("fade_in"), integerProperty(0, MaxSongLength)}, {QStringLiteral("fade_out"),
			integerProperty(0, MaxSongLength)}, {QStringLiteral("shape"), stringProperty()}}), true, fadeRange);
	add(registry, "crop", QStringLiteral("Keep only what is inside the range on its tracks.") + oneStep,
		objectSchema({}), true, cropRange);
	add(registry, "insert_time", QStringLiteral("Insert empty time at the range's start on its tracks - 'length' ticks, "
		"default the range's length - moving everything from there later.") + oneStep,
		objectSchema({{QStringLiteral("length"), tickProperty()}}), true, insertTime);
	add(registry, "copy", QStringLiteral("Copy the clips that lie inside the range (range.split first to include the "
		"parts of clips crossing its edges). Changes nothing."), objectSchema({}), false, copyRange);
	add(registry, "paste", QStringLiteral("Paste the copied clips at 'position' (default the range's start) onto the "
		"range's tracks, in the order they were copied from.") + oneStep,
		objectSchema({{QStringLiteral("position"), tickProperty()}}), true, pasteRange);
	add(registry, "consolidate", QStringLiteral("Render each covered sample track's clips inside the range into ONE clip "
		"(clip.consolidate per track - clips crossing an edge are refused: range.split first). "
		"The tracks' consolidations are one undo step, which control.undo reverts for every track together."), objectSchema({}), false, consolidateRange);
	add(registry, "duplicate", QStringLiteral("Copy what is inside the range and paste it right after it; the range moves "
		"onto the copy, so repeating it repeats the pattern.") + oneStep, objectSchema({}), true, duplicateRange);
}

} // namespace lmms
