/*
 * ControlCommandsSession.cpp - the session.* (Session View) command group:
 *                              the grid, the slots and the scenes, plus the
 *                              launch engine's read-back (SPEC A11-A16,
 *                              SPEC-zene-studio A1/A2).
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

/* Why this group exists: SPEC A1 gives the Session View a data layer and a
 * scheduler on the audio path, and without a grid nothing reached either. This
 * half is the MODEL - the grid, the slots, the scenes, the quantisation and the
 * launch engine's read-back; the launch REQUESTS are in
 * ControlCommandsSessionLaunch.cpp. SessionModel is not a JournallingObject, so
 * each edit captures the whole <session> block and records ONE action
 * checkpoint that puts it back.
 */

#include <cstdint>

#include <QDomDocument>
#include <QDomElement>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlCommandsSessionShared.h"
#include "ControlEdit.h"  // control::transactionPayload
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;

namespace
{

// ---------------------------------------------------------------------------
// the reversibility mechanism for a <session> block edit
// ---------------------------------------------------------------------------

QString sessionBlockXml(const SessionModel& model)
{
	QDomDocument document;
	QDomElement holder = document.createElement(QStringLiteral("session-snapshot"));
	document.appendChild(holder);
	model.saveState(document, holder);
	return document.toString();
}

void restoreSessionBlock(SessionModel& model, const QString& xml)
{
	QDomDocument document;
	if (!document.setContent(xml, false)) { return; }
	const QDomElement block =
		document.documentElement().firstChildElement(QStringLiteral("session"));
	if (!block.isNull()) { model.restoreState(block); }
}

/*! Records the edit as ONE action checkpoint on the engine's own ProjectJournal
 *  and returns the A16 transaction payload for the command's result. The step is
 *  on the journal on purpose: an agent's control.undo and a user's Ctrl+Z must be
 *  the same history, so the recorded inverse is a real undo step and not a
 *  private stack. `applies: journal` is explicit rather than implied, so the
 *  record cannot be read as naming a command that would restore it. */
QJsonObject recordSessionEdit(const QString& captured, const QJsonObject& before,
	const QString& what)
{
	control::addUndoStep([captured]() {
		Song* song = Engine::getSong();
		if (song != nullptr) { restoreSessionBlock(song->sessionModel(), captured); }
	});
	// The inverse is NOT a command: no registered command replays a <session>
	// block, so control.undo reaches it through the journal (it checks
	// inverse.applies for "command" and finds none). The op text says so in as
	// many words rather than naming a command that does not exist - the same
	// form clip.split uses for the step the journal itself performs.
	return control::transactionPayload(before,
		QStringLiteral("UNIMPLEMENTED: replay the captured <session> block through "
			"session.set_grid / session.set_slot / session.set_scene"),
		QJsonObject(), true,
		QStringLiteral("ProjectJournal (action checkpoint): one control.undo or Ctrl+Z restores the "
			"captured <session> block (%1)").arg(what));
}

//! Captures the block, for a command that is about to edit it.
QString captureSession(const SessionModel& model)
{
	return sessionBlockXml(model);
}

// ---------------------------------------------------------------------------
// argument appliers - small enough that no one of them carries a command
// ---------------------------------------------------------------------------

bool applySlotReference(ClipSlot& slot, const QJsonObject& args, ControlResult* error)
{
	const QString type = args.value(QStringLiteral("type")).toString();
	if (type.isEmpty())
	{
		// No type: 'pattern' / 'source' are self-describing, and the model's
		// setters clear the other reference kind (SPEC A1 mutual exclusivity).
		if (args.contains(QStringLiteral("pattern")))
		{
			slot.setPatternReference(args.value(QStringLiteral("pattern")).toInt());
		}
		else if (args.contains(QStringLiteral("source")))
		{
			slot.setAudioReference(args.value(QStringLiteral("source")).toString());
		}
		return true;
	}
	if (type == QLatin1String("empty")) { slot.clear(); return true; }
	if (type == QLatin1String("midi"))
	{
		if (!args.contains(QStringLiteral("pattern")))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("type 'midi' needs 'pattern': the PatternStore id the slot references"));
			return false;
		}
		slot.setPatternReference(args.value(QStringLiteral("pattern")).toInt());
		return true;
	}
	if (type == QLatin1String("audio") && !args.value(QStringLiteral("source")).toString().isEmpty())
	{
		slot.setAudioReference(args.value(QStringLiteral("source")).toString());
		return true;
	}
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'type' must be empty, midi or audio - and audio needs a non-empty 'source'"));
	return false;
}

void applySlotLaunchSettings(ClipSlot& slot, const QJsonObject& args)
{
	if (args.contains(QStringLiteral("name"))) { slot.setName(args.value(QStringLiteral("name")).toString()); }
	if (args.contains(QStringLiteral("legato"))) { slot.setLegato(args.value(QStringLiteral("legato")).toBool()); }
	if (args.contains(QStringLiteral("mode"))) { slot.setLaunchMode(modeFromName(args.value(QStringLiteral("mode")).toString())); }
	if (args.contains(QStringLiteral("quantisation")))
	{
		slot.setLaunchQuantisation(quantisationFromName(args.value(QStringLiteral("quantisation")).toString()));
	}
}

void applySlotShape(ClipSlot& slot, const QJsonObject& args)
{
	if (args.contains(QStringLiteral("loop_start"))) { slot.setLoopStart(args.value(QStringLiteral("loop_start")).toInt()); }
	if (args.contains(QStringLiteral("loop_length"))) { slot.setLoopLength(args.value(QStringLiteral("loop_length")).toInt()); }
	if (args.contains(QStringLiteral("gain_db"))) { slot.setGainDb(static_cast<float>(args.value(QStringLiteral("gain_db")).toDouble())); }
	if (args.contains(QStringLiteral("ram"))) { slot.setRamMode(args.value(QStringLiteral("ram")).toBool()); }
	if (args.contains(QStringLiteral("transpose"))) { slot.setTranspose(args.value(QStringLiteral("transpose")).toInt()); }
	if (args.contains(QStringLiteral("detune"))) { slot.setDetune(args.value(QStringLiteral("detune")).toInt()); }
}

} // namespace

// ---------------------------------------------------------------------------
// the commands
// ---------------------------------------------------------------------------

void registerSessionGetState(ControlRegistry& registry)
{
	ControlCommand state;
	state.id = QStringLiteral("session.get_state");
	state.group = QStringLiteral("session");
	state.verb = QStringLiteral("get_state");
	state.description = QStringLiteral("The Session View: grid dimensions, the global launch "
		"quantisation, every non-empty clip slot with its launch settings, every scene override, "
		"and the launch engine's read-back (completed launches, the grid line the newest starts "
		"fired on, how many clips started on that one line).");
	state.argsSchema = objectSchema();
	state.resultSchema = objectSchema({
		{QStringLiteral("grid"), objectProperty()},
		{QStringLiteral("quantisation"), stringProperty()},
		{QStringLiteral("slots"), arrayProperty()},
		{QStringLiteral("scenes"), arrayProperty()},
		{QStringLiteral("launch"), objectProperty()},
	});
	state.mutating = false;
	state.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const QJsonArray cells = clipSlots(*model);
		QJsonObject grid = gridState(*model, cells);
		Song* song = Engine::getSong();
		grid.insert(QStringLiteral("song_tracks"),
			static_cast<int>(song->tracks().size()));
		QJsonObject result;
		result.insert(QStringLiteral("grid"), grid);
		result.insert(QStringLiteral("quantisation"),
			quantisationName(model->globalLaunchQuantisation()));
		result.insert(QStringLiteral("slots"), cells);
		result.insert(QStringLiteral("scenes"), sceneStates(*model));
		result.insert(QStringLiteral("launch"), launchState(*song, song->sessionScheduler()));
		return ControlResult::success(result);
	};
	registry.registerCommand(state);
}


void registerSessionSetGrid(ControlRegistry& registry)
{
	ControlCommand grid;
	grid.id = QStringLiteral("session.set_grid");
	grid.group = QStringLiteral("session");
	grid.verb = QStringLiteral("set_grid");
	grid.description = QStringLiteral("Resize the Session View grid (column = a song track, "
		"row = a scene). Existing cells keep their content and position; new cells are empty. "
		"Reversible through the ProjectJournal (action checkpoint on the <session> block).");
	grid.argsSchema = objectSchema({
		{QStringLiteral("tracks"), integerProperty(0, 256)},
		{QStringLiteral("scenes"), integerProperty(0, 512)},
	}, {QStringLiteral("tracks"), QStringLiteral("scenes")});
	grid.resultSchema = objectSchema({
		{QStringLiteral("grid"), objectProperty()},
	});
	grid.mutating = true;
	grid.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const QString captured = captureSession(*model);
		const QJsonObject before = gridState(*model, clipSlots(*model));
		model->setTrackCount(args.value(QStringLiteral("tracks")).toInt());
		model->setSceneCount(args.value(QStringLiteral("scenes")).toInt());
		QJsonObject result;
		result.insert(QStringLiteral("grid"), gridState(*model, clipSlots(*model)));
		result.insert(QStringLiteral("__transaction"),
			recordSessionEdit(captured, before, QStringLiteral("the previous grid dimensions")));
		return ControlResult::success(result);
	};
	registry.registerCommand(grid);
}


void registerSessionSetQuantisation(ControlRegistry& registry)
{
	ControlCommand quantisation;
	quantisation.id = QStringLiteral("session.set_quantisation");
	quantisation.group = QStringLiteral("session");
	quantisation.verb = QStringLiteral("set_quantisation");
	quantisation.description = QStringLiteral("Set the session's global launch quantisation - the "
		"grid line every slot whose own quantisation is 'global' waits for. Reversible through the "
		"ProjectJournal (action checkpoint on the <session> block).");
	quantisation.argsSchema = objectSchema({
		{QStringLiteral("quantisation"), enumProperty(quantisationNames())},
	}, {QStringLiteral("quantisation")});
	quantisation.resultSchema = objectSchema({
		{QStringLiteral("quantisation"), stringProperty()},
	});
	quantisation.mutating = true;
	quantisation.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const QString captured = captureSession(*model);
		const QJsonObject before = gridState(*model, clipSlots(*model));
		model->setGlobalLaunchQuantisation(
			quantisationFromName(args.value(QStringLiteral("quantisation")).toString()));
		QJsonObject result;
		result.insert(QStringLiteral("quantisation"),
			quantisationName(model->globalLaunchQuantisation()));
		result.insert(QStringLiteral("__transaction"), recordSessionEdit(captured, before,
			QStringLiteral("the previous global launch quantisation")));
		return ControlResult::success(result);
	};
	registry.registerCommand(quantisation);
}


void registerSessionSetScene(ControlRegistry& registry)
{
	ControlCommand scene;
	scene.id = QStringLiteral("session.set_scene");
	scene.group = QStringLiteral("session");
	scene.verb = QStringLiteral("set_scene");
	scene.description = QStringLiteral("Set one scene's name and/or its tempo and time-signature "
		"overrides; a field that is supplied is written and its override switched on, a field that "
		"is not is left alone. Reversible through the ProjectJournal (action checkpoint).");
	scene.argsSchema = objectSchema({
		{QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("tempo"), numberProperty()},
		{QStringLiteral("timesig_numerator"), integerProperty(1, 64)},
		{QStringLiteral("timesig_denominator"), integerProperty(1, 64)},
	}, {QStringLiteral("scene")});
	scene.resultSchema = objectSchema({
		{QStringLiteral("scene"), objectProperty()},
	});
	scene.mutating = true;
	scene.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const int index = args.value(QStringLiteral("scene")).toInt();
		if (index < 0 || index >= model->sceneCount())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("scene %1 is outside the grid's %2 scenes; session.set_grid resizes it")
					.arg(index).arg(model->sceneCount()));
		}
		const QString captured = captureSession(*model);
		const QJsonObject before = gridState(*model, clipSlots(*model));
		Scene& sceneModel = model->scene(index);
		if (args.contains(QStringLiteral("name")))
		{
			sceneModel.setName(args.value(QStringLiteral("name")).toString());
		}
		if (args.contains(QStringLiteral("tempo")))
		{
			sceneModel.setTempo(args.value(QStringLiteral("tempo")).toDouble());
			sceneModel.setTempoEnabled(true);
		}
		if (args.contains(QStringLiteral("timesig_numerator")))
		{
			sceneModel.setTimeSigNumerator(args.value(QStringLiteral("timesig_numerator")).toInt());
			sceneModel.setTimeSigEnabled(true);
		}
		if (args.contains(QStringLiteral("timesig_denominator")))
		{
			sceneModel.setTimeSigDenominator(args.value(QStringLiteral("timesig_denominator")).toInt());
			sceneModel.setTimeSigEnabled(true);
		}
		QJsonObject result;
		result.insert(QStringLiteral("scene"), sceneState(index, sceneModel));
		result.insert(QStringLiteral("__transaction"),
			recordSessionEdit(captured, before, QStringLiteral("the previous scene settings")));
		return ControlResult::success(result);
	};
	registry.registerCommand(scene);
}


void registerSessionSetSlot(ControlRegistry& registry)
{
	ControlCommand slot;
	slot.id = QStringLiteral("session.set_slot");
	slot.group = QStringLiteral("session");
	slot.verb = QStringLiteral("set_slot");
	slot.description = QStringLiteral("Define one grid cell: a MIDI clip ('type':'midi' + "
		"'pattern'), an audio clip ('type':'audio' + 'source'), or empty ('type':'empty'); plus its "
		"launch mode, launch quantisation and playback settings. Only the fields supplied are "
		"written. Reversible through the ProjectJournal (action checkpoint on the <session> block).");
	slot.argsSchema = objectSchema({
		{QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)},
		{QStringLiteral("type"), enumProperty(slotTypeNames())},
		{QStringLiteral("pattern"), integerProperty(0, 0x7fffffff)},
		{QStringLiteral("source"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("mode"), enumProperty(modeNames())},
		{QStringLiteral("quantisation"), enumProperty(perClipQuantisationNames())},
		{QStringLiteral("legato"), booleanProperty()},
		{QStringLiteral("loop_start"), integerProperty(0, 0x7fffffff)},
		{QStringLiteral("loop_length"), integerProperty(0, 0x7fffffff)},
		{QStringLiteral("gain_db"), numberProperty()},
		{QStringLiteral("transpose"), integerProperty(-128, 128)},
		{QStringLiteral("detune"), integerProperty(-1200, 1200)},
		{QStringLiteral("ram"), booleanProperty()},
	}, {QStringLiteral("track"), QStringLiteral("scene")});
	slot.resultSchema = objectSchema({
		{QStringLiteral("slot"), objectProperty()},
	});
	slot.mutating = true;
	slot.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const int track = args.value(QStringLiteral("track")).toInt();
		const int scene = args.value(QStringLiteral("scene")).toInt();
		if (!gridContains(*model, track, scene, &error)) { return error; }
		const QString captured = captureSession(*model);
		const QJsonObject before = gridState(*model, clipSlots(*model));
		ClipSlot& target = model->slot(track, scene);
		if (!applySlotReference(target, args, &error)) { return error; }
		applySlotLaunchSettings(target, args);
		applySlotShape(target, args);
		QJsonObject result;
		result.insert(QStringLiteral("slot"), clipSlotState(track, scene, target));
		result.insert(QStringLiteral("__transaction"), recordSessionEdit(captured, before,
			QStringLiteral("the previous content of slot (%1, %2)").arg(track).arg(scene)));
		return ControlResult::success(result);
	};
	registry.registerCommand(slot);
}


void registerSessionClearSlot(ControlRegistry& registry)
{
	ControlCommand clearSlot;
	clearSlot.id = QStringLiteral("session.clear_slot");
	clearSlot.group = QStringLiteral("session");
	clearSlot.verb = QStringLiteral("clear_slot");
	clearSlot.description = QStringLiteral("Empty one grid cell, whatever it held. Reversible "
		"through the ProjectJournal (action checkpoint on the <session> block).");
	clearSlot.argsSchema = objectSchema({
		{QStringLiteral("track"), integerProperty(0, 255)},
		{QStringLiteral("scene"), integerProperty(0, 511)},
	}, {QStringLiteral("track"), QStringLiteral("scene")});
	clearSlot.resultSchema = objectSchema({
		{QStringLiteral("slot"), objectProperty()},
	});
	clearSlot.mutating = true;
	clearSlot.handler = [](const QJsonObject& args) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const int track = args.value(QStringLiteral("track")).toInt();
		const int scene = args.value(QStringLiteral("scene")).toInt();
		if (!gridContains(*model, track, scene, &error)) { return error; }
		const QString captured = captureSession(*model);
		const QJsonObject before = gridState(*model, clipSlots(*model));
		ClipSlot& target = model->slot(track, scene);
		target.clear();
		QJsonObject result;
		result.insert(QStringLiteral("slot"), clipSlotState(track, scene, target));
		result.insert(QStringLiteral("__transaction"), recordSessionEdit(captured, before,
			QStringLiteral("the previous content of slot (%1, %2)").arg(track).arg(scene)));
		return ControlResult::success(result);
	};
	registry.registerCommand(clearSlot);
}


void registerSessionClear(ControlRegistry& registry)
{
	ControlCommand clear;
	clear.id = QStringLiteral("session.clear");
	clear.group = QStringLiteral("session");
	clear.verb = QStringLiteral("clear");
	clear.description = QStringLiteral("Empty the whole Session View: no grid, no slot, no scene "
		"override, default quantisation. A project that never used the session then re-saves "
		"byte-identically (the <session> block is dropped). Reversible through the ProjectJournal.");
	clear.argsSchema = objectSchema();
	clear.resultSchema = objectSchema({
		{QStringLiteral("grid"), objectProperty()},
	});
	clear.mutating = true;
	clear.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		const QString captured = captureSession(*model);
		const QJsonObject before = gridState(*model, clipSlots(*model));
		model->clear();
		QJsonObject result;
		result.insert(QStringLiteral("grid"), gridState(*model, clipSlots(*model)));
		result.insert(QStringLiteral("__transaction"), recordSessionEdit(captured, before,
			QStringLiteral("the whole grid")));
		return ControlResult::success(result);
	};
	registry.registerCommand(clear);
}


/*! The model half of the group, in one place so the two halves read the same
 *  way (see ControlCommandsSessionLaunch.cpp for the other). One function per
 *  command, like every other group in this tree: a single registrar carrying
 *  seven commands' conditions measures CCN 17 and cannot be reviewed. */
void registerSessionCommands(ControlRegistry& registry)
{
	registerSessionGetState(registry);
	registerSessionSetGrid(registry);
	registerSessionSetQuantisation(registry);
	registerSessionSetScene(registry);
	registerSessionSetSlot(registry);
	registerSessionClearSlot(registry);
	registerSessionClear(registry);
}

} // namespace lmms
