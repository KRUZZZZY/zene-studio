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

#include <memory>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "JournallingObject.h"
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

//! The per-revision cap on a captured track's XML (SPEC A16: a bounded
//! snapshot). A track larger than this refuses the inverse rather than keeping
//! a truncated one.
constexpr int MaxTrackSnapshotChars = 65536;

//! The Track::Type a track.add type name selects - the same mapping the
//! creation below uses, so the undo step's redo cannot disagree with it.
Track::Type trackTypeForName(const QString& typeName)
{
	if (typeName == QLatin1String("pattern")) { return Track::Type::Pattern; }
	if (typeName == QLatin1String("sample")) { return Track::Type::Sample; }
	if (typeName == QLatin1String("automation")) { return Track::Type::Automation; }
	return Track::Type::Instrument;
}

//! The product's own track creation for \p type.
Track* createTrackOfType(Track::Type type, Song* song)
{
	if (type == Track::Type::Pattern) { song->addPatternTrack(); return song->tracks().back(); }
	return Track::create(type, song);
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

//! The snapshot a transaction carries for a structural track change (SPEC A16).
QJsonObject trackSnapshot(const QJsonObject& before, const QString& inverseOp,
	const QString& mechanism, bool reversible)
{
	return control::transactionPayload(before, inverseOp, QJsonObject(), reversible, mechanism);
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
		const Track::Type type = trackTypeForName(typeName);
		Song* song = Engine::getSong();
		const int before = static_cast<int>(song->tracks().size());
		QJsonArray beforeIds;
		for (int i = 0; i < before; ++i) { beforeIds.append(control::trackIdOf(song->tracks()[i])); }
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("track_count"), before);
		beforeState.insert(QStringLiteral("tracks"), beforeIds);

		// SPEC A16 deliverable 5: a created track has no before-state to
		// restore, so the inverse is the OPERATION. ONE action step is recorded
		// BEFORE the creation, so one control.undo - and one Ctrl+Z, which
		// unwinds the same ProjectJournal - removes the track again. The holder
		// keeps the pointer alive across the two lambdas without either of them
		// outliving a stack frame.
		const QString name = args.value(QStringLiteral("name")).toString();
		auto holder = std::make_shared<Track*>(nullptr);
		control::addUndoStep(
			[holder]() {
				if (*holder != nullptr) { control::removeTrack(*holder); *holder = nullptr; }
			},
			[song, type, name, holder]() {
				*holder = createTrackOfType(type, song);
				if (*holder != nullptr && !name.isEmpty()) { (*holder)->setName(name); }
			});

		// The product's own entry points where they are callable from outside
		// (Song::addPatternTrack is public; addSampleTrack/addAutomationTrack are
		// private slots, so those two take the same public Track::create() path
		// they themselves call - see Song.cpp).
		Track* track = createTrackOfType(type, song);
		*holder = track;
		if (!name.isEmpty()) { track->setName(name); }
		QJsonObject result = trackAddResult(track, song);
		result.insert(QStringLiteral("__transaction"),
			trackSnapshot(beforeState, QStringLiteral("remove the created track"),
				QStringLiteral("action checkpoint: the recorded undo step removes the created "
					"track through the product's own TrackContainerView::deleteTrackView path, "
					"and a fresh track carries only defaults, so removing it restores the "
					"container. LIMIT: the project's id counter is monotonic and is not rewound, "
					"so a re-add gets a fresh trk-<n>"),
				true));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! Removes \p track the way the product does. The implementation moved to
//! control::removeTrack (ControlEditSupport.cpp) so the undo step of track.add
//! and the creating call of automation.add_point use the same one.
void removeTrack(Track* track) { control::removeTrack(track); }

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
					QStringLiteral("recreate the track from its captured XML"), QJsonObject(), true,
					QStringLiteral("dry_run preview: nothing was changed")));
			return ControlResult::success(preview);
		}
		const QString id = args.value(QStringLiteral("track")).toString();
		Song* song = Engine::getSong();
		const int before = static_cast<int>(song->tracks().size());

		// SPEC A16 deliverable 5: a deleted track cannot be restored in place -
		// its journal id resolves to nullptr the moment it is freed - so the
		// inverse is the OPERATION: capture the track's own XML first, then
		// recreate it through Track::create(element, song), the call
		// TrackContainer::loadSettings makes. Bounded: a track whose XML is
		// over MaxTrackSnapshotChars refuses the inverse rather than keeping a
		// truncated (corrupt) one.
		QString capturedXml;
		auto holder = std::make_shared<Track*>(track);
		const bool captured = control::captureTrackXml(track, &capturedXml, MaxTrackSnapshotChars);
		if (captured)
		{
			control::addUndoStep(
				[song, capturedXml, holder]() {
					*holder = control::restoreTrackFromXml(capturedXml, song);
				},
				[holder]() {
					if (*holder != nullptr) { control::removeTrack(*holder); *holder = nullptr; }
				});
		}
		removeTrack(track);
		*holder = nullptr;

		QJsonObject result;
		result.insert(QStringLiteral("removed"), id);
		result.insert(QStringLiteral("track_count"), before - 1);
		result.insert(QStringLiteral("dry_run"), false);
		result.insert(QStringLiteral("__transaction"),
			captured
				? trackSnapshot(snapshot, QStringLiteral("recreate the track from its captured XML"),
					QStringLiteral("action checkpoint: the track's own XML (Track::saveState) is "
						"captured before the delete and the recorded undo step recreates it with "
						"Track::create(element, song), the same call the project loader makes"),
					true)
				: trackSnapshot(snapshot, QStringLiteral("UNIMPLEMENTED for this track"),
					QStringLiteral("snapshot only: this track's serialized state is larger than the "
						"%1-character cap, and a truncated track is a corrupt track, so no inverse "
						"was recorded - the before-state names what was removed")
						.arg(MaxTrackSnapshotChars),
					false));
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
	//
	// SPEC A16 deliverable 3: the action writes MORE THAN ONE OBJECT - the solo
	// flag plus every other track's mute - so a single-object checkpoint would
	// make one agent command cost N Ctrl+Z presses. Every song track goes into
	// ONE composite checkpoint instead, so one undo (control.undo or the GUI's
	// own stack, which is the same stack) restores the whole action.
	QVector<JournallingObject*> step;
	for (Track* songTrack : Engine::getSong()->tracks()) { step.append(songTrack); }
	control::addUndoStep(step);
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
			true,
			QStringLiteral("composite checkpoint: every track of the song container is recorded "
				"as ONE undo step, so the solo flag and the mute state of every other track "
				"(which Track::toggleSolo writes from the solo model's dataChanged) are restored "
				"together by one control.undo or one Ctrl+Z. LIMIT: Track::mutedBeforeSolo is "
				"transient and is not part of the project file, so it is not restored - it is "
				"re-derived on the next solo action")));
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
} // namespace

void registerArrangementCommands(ControlRegistry& registry)
{
	registerTrackAdd(registry);
	registerTrackRemove(registry);
	registerTrackRename(registry);
	registerTrackSetMute(registry);
	registerTrackSetSolo(registry);
	// track.set_arm and arrangement.get_state live in
	// ControlCommandsArrangementState.cpp (the 500-line ratchet).
	registerArrangementStateCommands(registry);
}

} // namespace lmms
