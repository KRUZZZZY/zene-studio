/*
 * ControlCommandsModulatorVoices.cpp - 0.5.0 MX-120: per-voice and per-note
 *                                      modulation (modulator.voice_set,
 *                                      modulator.instrument_target_set).
 *
 * A per-voice modulator restarts its LFO or envelope for every note of the track it follows and runs a
 * cycle per note at once; each note's value reaches the instrument as a per-note modulation (CLAP note
 * modulation, VST3 note expression). A target without a per-note path follows the newest note instead -
 * the monophonic fallback - and modulator.get_state says which, per target ('voice_mode').
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

#include <algorithm>
#include <cmath>

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlInstrumentCommands.h"
#include "ControlModulationSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "ModulationLayer.h"

namespace lmms
{

using namespace control;

namespace
{

const QString kGroup = QStringLiteral("modulator");

QJsonObject resultSchema()
{
	return objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("modulator_count"), integerProperty()},
		{QStringLiteral("target_index"), integerProperty()},
		{QStringLiteral("target_count"), integerProperty()},
		{QStringLiteral("per_note"), booleanProperty()},
		{QStringLiteral("driving"), integerProperty()},
		{QStringLiteral("engine_active"), booleanProperty()},
		{QStringLiteral("modulators"), arrayProperty()},
		{QStringLiteral("max_modulators"), integerProperty()},
		{QStringLiteral("max_targets_per_modulator"), integerProperty()},
	});
}

QJsonObject layerResultFor(int index)
{
	QJsonObject out = modulationLayerJson(songModulationLayer().layer());
	out.insert(QStringLiteral("modulator"), modulatorId(index));
	return out;
}

//! An undo/redo half that writes @a source back as modulator @a index's source.
std::function<void()> sourceWriter(int index, const ModulatorSource& source)
{
	return [index, source]() {
		editModulationLayer([index, source](ModulationLayer& layer, ModulationRuntime&) { layer.setSource(index, source); });
	};
}

//! The source voice_set asks for; false with a typed refusal (a per-voice modulator needs a track to follow).
bool voiceSource(const QJsonObject& args, ModulatorSource* source, ControlResult* error)
{
	source->perVoice = args.value(QStringLiteral("per_voice")).toBool(false);
	if (args.contains(QStringLiteral("trigger")))
	{
		if (args.value(QStringLiteral("trigger")).isNull())
		{
			source->triggerTrack = -1;
		}
		else
		{
			InstrumentTrack* track = resolveInstrumentTarget(args.value(QStringLiteral("trigger")).toString(), error);
			if (track == nullptr) { return false; }
			source->triggerTrack = track->id();
		}
	}
	if (source->perVoice && source->triggerTrack < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a per-voice modulator runs a cycle per note of a track: give 'trigger' (trk-<n>)"));
		return false;
	}
	return true;
}

ControlResult setVoice(const QJsonObject& args)
{
	ControlResult error;
	const ModulationLayer& layer = songModulationLayer().layer();
	const int index = resolveModulatorIndex(layer, args.value(QStringLiteral("modulator")).toString(), &error);
	if (index < 0) { return error; }
	const ModulatorSource previous = layer.modulator(index)->source;
	ModulatorSource wanted = previous;
	if (!voiceSource(args, &wanted, &error)) { return error; }
	if (wanted.perVoice == previous.perVoice && wanted.triggerTrack == previous.triggerTrack)
	{
		return ControlResult::success(layerResultFor(index));
	}
	const QJsonObject before = modulationLayerJson(layer);
	editModulationLayer([index, wanted](ModulationLayer& target, ModulationRuntime&) { target.setSource(index, wanted); });
	addUndoStep(sourceWriter(index, previous), sourceWriter(index, wanted));
	QJsonObject inverse{{QStringLiteral("modulator"), modulatorId(index)}, {QStringLiteral("per_voice"), previous.perVoice}};
	if (previous.triggerTrack >= 0) { inverse.insert(QStringLiteral("trigger"), trackId(previous.triggerTrack)); }
	else { inverse.insert(QStringLiteral("trigger"), QJsonValue(QJsonValue::Null)); }
	QJsonObject result = layerResultFor(index);
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("modulator.voice_set"), inverse,
		true, QStringLiteral("action checkpoint: the recorded undo step writes the previous source (its per-voice "
			"flag and trigger track) back and re-resolves the modulator's targets")));
	return ControlResult::success(result);
}

void registerVoiceSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.voice_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("voice_set");
	cmd.description = QStringLiteral("Make a modulator per-voice (or not): 'per_voice' true restarts its LFO or "
		"envelope for EVERY note of the 'trigger' track (trk-<n>; needed unless it already follows one) and runs "
		"one cycle per sounding note, each note's value sent to the instrument for that note only (CLAP note "
		"modulation, VST3 note expression). A target the instrument cannot modulate per note follows the newest "
		"note instead - monophonic - and modulator.get_state reports 'voice_mode' per target. Saved with the "
		"project. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("per_voice"), booleanProperty()},
		{QStringLiteral("trigger"), nullable(stringProperty())},
	}, {QStringLiteral("modulator"), QStringLiteral("per_voice")});
	cmd.resultSchema = resultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return setVoice(args); };
	registry.registerCommand(cmd);
}

//! An instrument route the layer would refuse: empty, already bound, or naming no parameter.
ControlResult refusedInstrumentRoute(const ModulationLayer& layer, const ModulationRoute& route)
{
	if (route.parameter.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'parameter' must not be empty: it is the instrument parameter's display name"));
	}
	if (layer.findRoute(route) != nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is already driven by a modulator: one parameter has one depth").arg(route.parameter));
	}
	QString why;
	if (modulationTargetModel(route, &why) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' does not resolve to a parameter: %2").arg(route.parameter, why));
	}
	return ControlResult::success(QJsonObject{});
}

ModulationRoute instrumentRoute(InstrumentTrack* track, const QJsonObject& args)
{
	ModulationRoute route;
	route.channel = -1;  // not a rack address
	route.track = track->id();
	route.parameter = args.value(QStringLiteral("parameter")).toString();
	route.depth = std::clamp(static_cast<float>(args.value(QStringLiteral("depth")).toDouble()), -1.0f, 1.0f);
	route.perSample = args.value(QStringLiteral("per_sample")).toBool(false);
	return route;
}

void recordInstrumentRoute(int modulator, int index, const ModulationRoute& route)
{
	addUndoStep([modulator, index]() {
		editModulationLayer([modulator, index](ModulationLayer& layer, ModulationRuntime&) { layer.removeRoute(modulator, index); });
	}, [modulator, index, route]() {
		editModulationLayer([modulator, index, route](ModulationLayer& layer, ModulationRuntime&) { layer.insertRoute(modulator, index, route); });
	});
}

ControlResult bindInstrumentRoute(InstrumentTrack* track, const QJsonObject& args)
{
	ControlResult error;
	const ModulationLayer& layer = songModulationLayer().layer();
	const int modulator = resolveModulatorIndex(layer, args.value(QStringLiteral("modulator")).toString(), &error);
	if (modulator < 0) { return error; }
	if (layer.modulator(modulator)->routeCount() >= ModulationLayer::MaxRoutesPerModulator)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("modulator %1 already drives %2 parameters (the cap)")
			.arg(modulatorId(modulator)).arg(ModulationLayer::MaxRoutesPerModulator));
	}
	const double depth = args.value(QStringLiteral("depth")).toDouble();
	if (!std::isfinite(depth) || depth < -1.0 || depth > 1.0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'depth' %1 is outside -1..1 (a fraction of the target's range)").arg(depth));
	}
	const int requestedIndex = args.value(QStringLiteral("target_index")).toInt(layer.modulator(modulator)->routeCount());
	const ModulationRoute route = instrumentRoute(track, args);
	const ControlResult refused = refusedInstrumentRoute(layer, route);
	if (!refused.ok) { return refused; }
	const QJsonObject before = modulationLayerJson(layer);
	int index = -1;
	editModulationLayer([&](ModulationLayer& target, ModulationRuntime&) {
		index = insertModulationRouteAtIndex(target, modulator, requestedIndex, route);
	});
	if (index < 0) { return modulationRouteInsertFailure(args, QStringLiteral("target_index")); }
	recordInstrumentRoute(modulator, index, route);
	Instrument* instrument = track->instrument();
	QJsonObject result = layerResultFor(modulator);
	result.insert(QStringLiteral("target_index"), index);
	result.insert(QStringLiteral("target_count"), songModulationLayer().layer().modulator(modulator)->routeCount());
	result.insert(QStringLiteral("per_note"), instrument != nullptr && instrument->perNoteTarget(modulationTargetModel(route, nullptr)) >= 0);
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("modulator.target_remove"),
		QJsonObject{{QStringLiteral("modulator"), modulatorId(modulator)}, {QStringLiteral("target"), index}}, true,
		QStringLiteral("action checkpoint: the recorded undo step removes the route this command appended "
			"(ModulationLayer::removeRoute at the same index) and hands the parameter back")));
	return ControlResult::success(result);
}

void registerInstrumentTargetSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.instrument_target_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("instrument_target_set");
	cmd.description = QStringLiteral("Bind a parameter of an instrument track's INSTRUMENT ('target' trk-<n>, "
		"'parameter' its display name, as plugin.param_get lists it) to a modulator, with 'depth' a fraction of "
		"the parameter's range (-1..1). This is the target a per-voice modulator sends per-note values to; "
		"'per_note' says whether the instrument takes them for this parameter (a CLAP parameter marked "
		"modulatable per key or note, a VST3 note-expression type). Refused when the parameter does not resolve "
		"or is already driven. Optional 'target_index' inserts at a route position; omitting it appends. "
		"Reversible through the ProjectJournal (modulator.target_remove is the inverse).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("target_index"), integerProperty()},
		{QStringLiteral("parameter"), stringProperty()},
		{QStringLiteral("depth"), numberProperty()},
		{QStringLiteral("per_sample"), booleanProperty()},
	}, {QStringLiteral("modulator"), QStringLiteral("target"), QStringLiteral("parameter")});
	cmd.resultSchema = resultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		return withInstrumentTarget(args, [&args](InstrumentTrack* track) { return bindInstrumentRoute(track, args); });
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerModulatorVoiceCommands(ControlRegistry& registry)
{
	registerVoiceSet(registry);
	registerInstrumentTargetSet(registry);
}

} // namespace lmms
