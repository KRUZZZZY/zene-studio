/*
 * ControlCommandsArrangement.cpp - the track.* (song container) commands and
 *                                  arrangement.get_state (SPEC A11-A16).
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainer.h"
#include "TrackView.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

QJsonObject trackEditState(Track* track, int index)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), control::trackIdOf(track));
	entry.insert(QStringLiteral("index"), index);
	entry.insert(QStringLiteral("name"), track->name());
	entry.insert(QStringLiteral("type"), control::trackTypeNameOf(track->type()));
	entry.insert(QStringLiteral("muted"), track->isMuted());
	entry.insert(QStringLiteral("soloed"), track->isSolo());
	entry.insert(QStringLiteral("clip_count"), track->numOfClips());
	return entry;
}

//! Index of \p track in its container, or -1 when it is not in it.
int trackIndexInContainer(Track* track)
{
	const TrackContainer::TrackList& list = track->trackContainer()->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		if (list[i] == track) { return i; }
	}
	return -1;
}

//! The snapshot a transaction carries when the journal has no inverse (SPEC A16).
QJsonObject trackSnapshot(const QJsonObject& before, const QString& inverseOp)
{
	return control::transactionPayload(before, inverseOp, QJsonObject(), false,
		QStringLiteral("snapshot only: the product's own track add/remove path keeps no journal "
			"checkpoint (TrackContainerView::createTrackView / deleteTrackView have theirs "
			"commented out in this tree), so the ProjectJournal cannot create or destroy a track; "
			"the before-state is recorded so the change can be re-applied by hand"));
}

//! The new track is the last one the container holds (TrackContainer::addTrack
//! pushes back, and the Track constructor adds itself).
QJsonObject trackAddResult(Track* track, Song* song)
{
	const int index = static_cast<int>(song->tracks().size()) - 1;
	QJsonObject result = trackEditState(track, index);
	// The id the track was GIVEN AT CREATION, which is what track.add promises
	// the caller (SPEC-stable-ids.md 5.1 item 5): not a position, so it stays
	// valid when other tracks are added or removed.
	result.insert(QStringLiteral("track"), control::trackIdOf(track));
	result.insert(QStringLiteral("track_count"), static_cast<int>(song->tracks().size()));
	return result;
}

void registerTrackAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.add");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("add");
	cmd.description = QStringLiteral("Append a track of the given type (default instrument) and return "
		"its stable trk-<n> id. Not reversible: see the transaction's mechanism.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("type"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
			{QStringLiteral("enum"), QJsonArray{QStringLiteral("instrument"), QStringLiteral("pattern"),
				QStringLiteral("sample"), QStringLiteral("automation")}}}},
		{QStringLiteral("name"), control::stringProperty()},
	});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("index"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("type"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString typeName = args.value(QStringLiteral("type")).toString(QStringLiteral("instrument"));
		Song* song = Engine::getSong();
		const int before = static_cast<int>(song->tracks().size());
		QJsonArray beforeIds;
		for (int i = 0; i < before; ++i) { beforeIds.append(control::trackIdOf(song->tracks()[i])); }
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("track_count"), before);
		beforeState.insert(QStringLiteral("tracks"), beforeIds);

		// The product's own entry points where they are callable from outside
		// (Song::addPatternTrack is public; addSampleTrack/addAutomationTrack are
		// private slots, so those two take the same public Track::create() path
		// they themselves call - see Song.cpp).
		if (typeName == QLatin1String("pattern")) { song->addPatternTrack(); }
		else if (typeName == QLatin1String("sample")) { Track::create(Track::Type::Sample, song); }
		else if (typeName == QLatin1String("automation")) { Track::create(Track::Type::Automation, song); }
		else { Track::create(Track::Type::Instrument, song); }

		Track* track = song->tracks().back();
		if (!args.value(QStringLiteral("name")).toString().isEmpty())
		{
			track->setName(args.value(QStringLiteral("name")).toString());
		}
		QJsonObject result = trackAddResult(track, song);
		result.insert(QStringLiteral("__transaction"),
			trackSnapshot(beforeState, QStringLiteral("UNIMPLEMENTED: remove the created track")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! Remove \p track the way the product does. TrackContainerView::deleteTrackView
//! removes the view, deletes it, and only then deletes the track; deleting the
//! track directly leaves its view pointing at freed memory, which was measured as
//! a SIGSEGV right after the command answered. A guiless instance has no view.
void removeTrack(Track* track)
{
	gui::SongEditor* editor = gui::getGUI() == nullptr || gui::getGUI()->songEditor() == nullptr
		? nullptr : gui::getGUI()->songEditor()->m_editor;
	if (editor == nullptr) { delete track; return; }
	for (gui::TrackView* view : editor->trackViews())
	{
		if (view->getTrack() == track) { editor->deleteTrackView(view); return; }
	}
	delete track;
}

void registerTrackRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.remove");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Delete a track together with its clips. dry_run previews it.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("dry_run"), control::booleanProperty()},
	}, {QStringLiteral("track")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("removed"), control::stringProperty()},
		{QStringLiteral("track_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("dry_run"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		const int index = trackIndexInContainer(track);
		if (index < 0)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the track is not in the song container"));
		}
		QJsonObject snapshot = trackEditState(track, index);
		if (args.value(QStringLiteral("dry_run")).toBool(false))
		{
			QJsonObject preview = snapshot;
			preview.insert(QStringLiteral("dry_run"), true);
			preview.insert(QStringLiteral("__transaction"),
				control::transactionPayload(snapshot,
					QStringLiteral("UNIMPLEMENTED: restore a deleted track"), QJsonObject(), false,
					QStringLiteral("dry_run preview: nothing was changed")));
			return ControlResult::success(preview);
		}
		const QString id = args.value(QStringLiteral("track")).toString();
		const Song* song = Engine::getSong();
		const int before = static_cast<int>(song->tracks().size());
		removeTrack(track);

		QJsonObject result;
		result.insert(QStringLiteral("removed"), id);
		result.insert(QStringLiteral("track_count"), before - 1);
		result.insert(QStringLiteral("dry_run"), false);
		result.insert(QStringLiteral("__transaction"),
			trackSnapshot(snapshot, QStringLiteral("UNIMPLEMENTED: restore a deleted track")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerTrackRename(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.rename");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("rename");
	cmd.description = QStringLiteral("Rename a track. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
	}, {QStringLiteral("track"), QStringLiteral("name")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		const QString name = args.value(QStringLiteral("name")).toString();
		if (name.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'name' must not be empty"));
		}
		const QString previous = track->name();
		// A Track checkpoint is a real inverse: Track::saveState serializes name,
		// mute/solo and the clips, and restoreState re-loads the whole track
		// (ProjectJournal::undo applies it). Same mechanism the GUI's own track
		// operations use (TrackOperationsWidget).
		track->addJournalCheckPoint();
		track->setName(name);

		QJsonObject result;
		result.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
		result.insert(QStringLiteral("name"), track->name());
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(
				QJsonObject{{QStringLiteral("track"), args.value(QStringLiteral("track")).toString()},
					{QStringLiteral("name"), previous}},
				QStringLiteral("track.rename"),
				QJsonObject{{QStringLiteral("track"), args.value(QStringLiteral("track")).toString()},
					{QStringLiteral("name"), previous}},
				true, QStringLiteral("ProjectJournal (Track checkpoint)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! Every track's mute/solo state: the before-snapshot of the solo action, which
//! changes more than one object (SPEC A16's documented snapshot fallback).
QJsonArray muteSoloSnapshot()
{
	QJsonArray tracks;
	const TrackContainer::TrackList& list = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), control::trackIdOf(list[i]));
		entry.insert(QStringLiteral("muted"), list[i]->isMuted());
		entry.insert(QStringLiteral("soloed"), list[i]->isSolo());
		tracks.append(entry);
	}
	return tracks;
}

ControlResult setTrackMute(const QJsonObject& args)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const bool value = args.value(QStringLiteral("muted")).toBool();
	const bool previous = track->isMuted();
	// The muted model of a Track is a BoolModel, i.e. a JournallingObject, so the
	// checkpoint is a real inverse for exactly the value this command changes -
	// the same mechanism mixer.set_volume uses.
	track->getMutedModel()->addJournalCheckPoint();
	track->setMuted(value);

	QJsonObject result;
	result.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	result.insert(QStringLiteral("muted"), track->isMuted());
	QJsonObject before;
	before.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	before.insert(QStringLiteral("muted"), previous);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	inverseArgs.insert(QStringLiteral("muted"), previous);
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("track.set_mute"), inverseArgs, true,
			QStringLiteral("ProjectJournal (Track mute BoolModel checkpoint)")));
	return ControlResult::success(result);
}

ControlResult setTrackSolo(const QJsonObject& args)
{
	ControlResult error;
	Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const bool value = args.value(QStringLiteral("solo")).toBool();
	QJsonObject before;
	before.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	before.insert(QStringLiteral("tracks"), muteSoloSnapshot());

	// The product's solo is a two-part action: the solo BoolModel carries the flag
	// and Track::toggleSolo() applies the cross-track mute ("this track unmuted,
	// every other track muted", or the pre-solo mute states back). TrackView
	// connects the model's dataChanged signal to toggleSolo(), so a display-ful
	// instance gets that for free; a guiless one is given it here, which is what
	// keeps the action identical with and without a display (SPEC A13). Calling it
	// ourselves in the GUI case too would apply it twice and corrupt
	// mutedBeforeSolo, hence the branch.
	track->addJournalCheckPoint();
	track->setSolo(value);
	if (gui::getGUI() == nullptr) { track->toggleSolo(); }

	QJsonObject result;
	result.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
	result.insert(QStringLiteral("soloed"), track->isSolo());
	result.insert(QStringLiteral("tracks"), muteSoloSnapshot());
	result.insert(QStringLiteral("__transaction"),
		control::transactionPayload(before, QStringLiteral("track.set_solo"),
			QJsonObject{{QStringLiteral("track"), args.value(QStringLiteral("track")).toString()},
				{QStringLiteral("solo"), !value}},
			false,
			QStringLiteral("partial: the ProjectJournal (Track checkpoint) reverses this track's solo "
				"flag, but the same action writes the mute state of every track (the solo model's "
				"dataChanged is connected to Track::toggleSolo by TrackView) and one checkpoint covers "
				"one object, so a single control.undo does not reverse the whole action; the before-state "
				"recorded here is a snapshot of every track")));
	return ControlResult::success(result);
}

void registerTrackSetMute(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_mute");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_mute");
	cmd.description = QStringLiteral("Mute or unmute a track. Reversible through the ProjectJournal "
		"(the track's mute BoolModel checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("muted")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("muted"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setTrackMute(args); };
	registry.registerCommand(cmd);
}

void registerTrackSetSolo(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_solo");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_solo");
	cmd.description = QStringLiteral("Solo or unsolo a track. This is the product's whole solo action: the "
		"soloed track is unmuted and the others muted (TrackView drives that from the solo model), and "
		"the result carries the mute/solo state of every track.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("solo"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("solo")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("soloed"), control::booleanProperty()},
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setTrackSolo(args); };
	registry.registerCommand(cmd);
}

void registerTrackSetArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.set_arm");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("set_arm");
	cmd.description = QStringLiteral("Arm or disarm a track for recording. Refused: this tree has no "
		"record-arm on a song track.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("armed"), control::booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("armed")});
	cmd.resultSchema = control::objectSchema({});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		// Honest refusal, not a fake success. Record-arm in this tree lives on the
		// prototype MultiTrackRecorder (AudioEngine::recorder(), two capture
		// streams keyed by input channel, task #556) and not on lmms::Track: there
		// is no per-track armed flag to write, and inventing one would add a field
		// to the track serialization format.
		ControlResult error;
		Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no record-arm exists on a %1 track in this build: arm state lives on the "
				"prototype MultiTrackRecorder (AudioEngine::recorder(), input-channel keyed), not on the "
				"song model").arg(control::trackTypeNameOf(track->type())));
	};
	registry.registerCommand(cmd);
}

void registerArrangementGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("arrangement.get_state");
	cmd.group = QStringLiteral("arrangement");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Every track with its clips, addressed by the stable trk-<n> and "
		"clip-<n> ids. The trk-<n> number is assigned at creation and persists in the project file. "
		"Addressing is scoped to the SONG container: a track inside a nested container (the "
		"<trackcontainer> a pattern track carries) is not reachable by id, exactly as it is not "
		"addressable by index.");
	cmd.argsSchema = control::objectSchema({});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("clips"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
		{QStringLiteral("track_count"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("clip_count"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.handler = [](const QJsonObject&) {
		const QVector<control::ClipRef> refs = control::enumerateClips();
		QJsonArray clips;
		for (const control::ClipRef& ref : refs) { clips.append(control::clipState(ref)); }

		QJsonArray tracks;
		const TrackContainer::TrackList& list = Engine::getSong()->tracks();
		for (int i = 0; i < static_cast<int>(list.size()); ++i)
		{
			QJsonObject entry = trackEditState(list[i], i);
			QJsonArray clipIds;
			for (const control::ClipRef& ref : refs)
			{
				if (ref.trackIndex == i) { clipIds.append(control::clipId(ref.ordinal)); }
			}
			entry.insert(QStringLiteral("clips"), clipIds);
			entry.insert(QStringLiteral("selected"), control::selectedClipId().isEmpty() ? false
				: clipIds.contains(control::selectedClipId()));
			tracks.append(entry);
		}

		QJsonObject result;
		result.insert(QStringLiteral("tracks"), tracks);
		result.insert(QStringLiteral("clips"), clips);
		result.insert(QStringLiteral("track_count"), tracks.size());
		result.insert(QStringLiteral("clip_count"), clips.size());
		result.insert(QStringLiteral("selected_clip"), control::selectedClipId());
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerArrangementCommands(ControlRegistry& registry)
{
	registerTrackAdd(registry);
	registerTrackRemove(registry);
	registerTrackRename(registry);
	registerTrackSetMute(registry);
	registerTrackSetSolo(registry);
	registerTrackSetArm(registry);
	registerArrangementGetState(registry);
}

} // namespace lmms
