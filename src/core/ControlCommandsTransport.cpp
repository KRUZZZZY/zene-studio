/*
 * ControlCommandsTransport.cpp - the transport.* and track.* command groups.
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

#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

QString trackTypeName(Track::Type type)
{
	switch (type)
	{
		case Track::Type::Instrument: return QStringLiteral("instrument");
		case Track::Type::Pattern: return QStringLiteral("pattern");
		case Track::Type::Sample: return QStringLiteral("sample");
		case Track::Type::Event: return QStringLiteral("event");
		case Track::Type::Video: return QStringLiteral("video");
		case Track::Type::Automation: return QStringLiteral("automation");
		case Track::Type::HiddenAutomation: return QStringLiteral("hidden_automation");
		case Track::Type::Count: break;
	}
	return QStringLiteral("unknown");
}

QJsonObject trackState(Track* track, int index)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), control::trackIdOf(track));
	entry.insert(QStringLiteral("index"), index);
	entry.insert(QStringLiteral("name"), track->name());
	entry.insert(QStringLiteral("type"), trackTypeName(track->type()));
	entry.insert(QStringLiteral("muted"), track->isMuted());
	entry.insert(QStringLiteral("soloed"), track->isSolo());
	return entry;
}

} // namespace

void registerTransportCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("transport.play");
		cmd.group = QStringLiteral("transport");
		cmd.verb = QStringLiteral("play");
		cmd.description = QStringLiteral("Start playback of the current song.");
		// Playback is audible output, so this one command refuses (typed,
		// 'requires') when the configured audio device failed to open and the
		// engine is running on the dummy device - the refusal names the backend.
		// Everything else stays usable headless: the model, render and save
		// (task #626).
		cmd.requiresDecl = QStringList{QStringLiteral("device")};
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("playing"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			Song* song = Engine::getSong();
			song->playSong();
			QJsonObject result;
			result.insert(QStringLiteral("playing"), song->isPlaying());
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("transport.stop");
		cmd.group = QStringLiteral("transport");
		cmd.verb = QStringLiteral("stop");
		cmd.description = QStringLiteral("Stop playback.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("playing"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			Song* song = Engine::getSong();
			song->stop();
			QJsonObject result;
			result.insert(QStringLiteral("playing"), false);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("transport.seek");
		cmd.group = QStringLiteral("transport");
		cmd.verb = QStringLiteral("seek");
		cmd.description = QStringLiteral("Move the play head to an absolute position in ticks.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("ticks"), integerProperty(0, 0x7fffffff)}}, {QStringLiteral("ticks")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("position_ticks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) {
			Song* song = Engine::getSong();
			const qint64 previous = static_cast<qint64>(song->getPlayPos().getTicks());
			const tick_t ticks = static_cast<tick_t>(args.value(QStringLiteral("ticks")).toDouble());
			// SPEC A16: the play head is engine state and not a JournallingObject,
			// so there is no object checkpoint to restore - but the inverse is a
			// bounded scalar, so it becomes ONE action step on the engine's own
			// undo stack (the same stack Ctrl+Z unwinds).
			control::addUndoStep(
				[song, previous]() { song->setPlayPos(static_cast<tick_t>(previous)); },
				[song, ticks]() { song->setPlayPos(ticks); });
			song->setPlayPos(ticks);

			QJsonObject result;
			result.insert(QStringLiteral("position_ticks"), static_cast<qint64>(song->getPlayPos().getTicks()));
			QJsonObject before;
			before.insert(QStringLiteral("position_ticks"), previous);
			QJsonObject inverse;
			inverse.insert(QStringLiteral("op"), QStringLiteral("transport.seek"));
			inverse.insert(QStringLiteral("args"), QJsonObject{{QStringLiteral("ticks"), previous}});
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"), before);
			transaction.insert(QStringLiteral("inverse"), inverse);
			transaction.insert(QStringLiteral("reversible"), true);
			transaction.insert(QStringLiteral("mechanism"),
				QStringLiteral("action checkpoint: the play head is not a JournallingObject, so the "
					"recorded undo step calls Song::setPlayPos with the previous tick"));
			result.insert(QStringLiteral("__transaction"), transaction);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("transport.set_tempo");
		cmd.group = QStringLiteral("transport");
		cmd.verb = QStringLiteral("set_tempo");
		cmd.description = QStringLiteral("Set the song tempo in BPM.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("bpm"), integerProperty(MinTempo, MaxTempo)}}, {QStringLiteral("bpm")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) {
			Song* song = Engine::getSong();
			const int previous = song->getTempo();
			const int bpm = static_cast<int>(args.value(QStringLiteral("bpm")).toDouble());
			// A Song-level journal checkpoint gives the engine's own undo stack a
			// real inverse for this change (SPEC A16, ProjectJournal).
			song->addJournalCheckPoint();
			song->setTempo(bpm);

			QJsonObject result;
			result.insert(QStringLiteral("tempo"), song->getTempo());
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"), QJsonObject{{QStringLiteral("tempo"), previous}});
			transaction.insert(QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("transport.set_tempo")},
					{QStringLiteral("args"), QJsonObject{{QStringLiteral("bpm"), previous}}}});
			transaction.insert(QStringLiteral("reversible"), true);
			transaction.insert(QStringLiteral("mechanism"), QStringLiteral("ProjectJournal (Song checkpoint)"));
			result.insert(QStringLiteral("__transaction"), transaction);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("transport.get_state");
		cmd.group = QStringLiteral("transport");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Playback position and transport flags.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("playing"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("paused"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
			{QStringLiteral("position_ticks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
			{QStringLiteral("tempo"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			Song* song = Engine::getSong();
			QJsonObject result;
			result.insert(QStringLiteral("playing"), song->isPlaying());
			result.insert(QStringLiteral("paused"), song->isPaused());
			result.insert(QStringLiteral("position_ticks"), static_cast<qint64>(song->getPlayPos().getTicks()));
			result.insert(QStringLiteral("tempo"), song->getTempo());
			result.insert(QStringLiteral("master_volume"), song->masterVolume());
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("track.list");
		cmd.group = QStringLiteral("track");
		cmd.verb = QStringLiteral("list");
		cmd.description = QStringLiteral("Every track in the song container, with its stable trk-<n> id. "
			"The id is assigned at creation and persists in the project file. Addressing is scoped to "
			"the SONG container: a track inside a nested container (the <trackcontainer> a pattern "
			"track carries) is not reachable by id, exactly as it is not addressable by index.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("tracks"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			QJsonArray tracks;
			const TrackContainer::TrackList& list = Engine::getSong()->tracks();
			for (int i = 0; i < static_cast<int>(list.size()); ++i)
			{
				tracks.append(trackState(list[i], i));
			}
			QJsonObject result;
			result.insert(QStringLiteral("tracks"), tracks);
			result.insert(QStringLiteral("count"), tracks.size());
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("track.get_state");
		cmd.group = QStringLiteral("track");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("One track addressed by its trk-<n> id: the number the track "
			"was given at creation, which it keeps until it is deleted. A malformed id is "
			"invalid_args; a well-formed id naming no live track is not_found. Addressing is scoped "
			"to the SONG container, so a track inside a nested container is not reachable by id.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("track"), stringProperty()}}, {QStringLiteral("track")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("id"), stringProperty()},
			{QStringLiteral("name"), stringProperty()},
			{QStringLiteral("type"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) {
			const QString id = args.value(QStringLiteral("track")).toString();
			// One resolver for every trk-<n> consumer: a malformed id is typed
			// invalid_args and a well-formed id that names no live track is
			// typed not_found, never a positional guess (SPEC-stable-ids.md).
			ControlResult error;
			Track* track = control::resolveTrack(id, &error);
			if (track == nullptr) { return error; }
			const TrackContainer::TrackList& list = Engine::getSong()->tracks();
			for (int i = 0; i < static_cast<int>(list.size()); ++i)
			{
				if (list[i] == track) { return ControlResult::success(trackState(track, i)); }
			}
			return ControlResult::success(trackState(track, -1));
		};
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
