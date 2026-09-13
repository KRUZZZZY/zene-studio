/*
 * ControlCommandsModulator.cpp - the modulator.* command group's HALF (SPEC
 *                                A11-A16): the layer itself, a modulator's own
 *                                source, and the read-only inspector. The
 *                                route half is ControlCommandsModulatorRoutes.cpp.
 *
 * A MODULATOR is a timeline-locked LFO plus a list of ROUTES; each route binds
 * one existing AutomatableModel (addressed exactly like a rack macro target: a
 * mixer channel, a chain in its rack, an effect in that chain, and the
 * parameter's display name) with a DEPTH.
 *
 * The depth is a signed FRACTION of the target's own min..max - the rack-macro
 * lane's unit, for its reason (recording the engine's numbers pins the
 * assignment to the range one build happened to report). What differs is what
 * the fraction is applied to: a macro REPLACES the parameter's value, a
 * modulator ADDS to it, so two parameters with different ranges and different
 * current values move by the same fraction of their own range. That is what
 * makes the amount independent of each parameter's own absolute value.
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

#include <functional>

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlModulationSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString kGroup = QStringLiteral("modulator");

/*! Every edit to the layer goes through here, and every edit restores the
 *  targets first. That is what makes re-capturing a base idempotent: the models
 *  are at their unmodulated values when the new base is read, so editing a
 *  modulator can never bake a modulated value into it (docs/MODULATION.md
 *  section 3).
 *
 *  The undo step each handler records beside it is an ACTION checkpoint
 *  (control::addUndoStep), not a Song journal checkpoint: a Song checkpoint
 *  captures the track container, and the layer is not in it - the same finding
 *  the tempo map records. A step captures VALUES (indices, copies of the
 *  layer's own data), never a pointer, because a step can run long after the
 *  command that recorded it.
 */
void editLayer(const std::function<void(ModulationLayer&, ModulationRuntime&)>& change)
{
	songModulationLayer().edit([&change](ModulationLayer& layer, ModulationRuntime& runtime) {
		change(layer, runtime);
		rebuildModulationRuntimeRestoring(layer, &runtime);
		return true;
	});
}

//! The undo/redo pair of a step that removes \a modulator at \a index: the
//! inverse re-inserts the captured modulator (name, source and routes) at its
//! own index, so the layer and the modulator-<n> ids come back exactly.
void recordRemoval(int index, const Modulator& captured)
{
	addUndoStep(
		[index, captured]() {
			editLayer([index, captured](ModulationLayer& layer, ModulationRuntime&) {
				layer.insertModulator(index, captured);
			});
		},
		[index]() {
			editLayer([index](ModulationLayer& layer, ModulationRuntime&) {
				layer.removeModulator(index);
			});
		});
}

//! The undo/redo pair of a creation: the inverse removes what the command
//! created (which carried no routes yet), so a later edit to another modulator
//! cannot make the undo eat one nobody asked about.
void recordCreation(int index, const Modulator& created)
{
	addUndoStep(
		[index]() {
			editLayer([index](ModulationLayer& layer, ModulationRuntime&) {
				layer.removeModulator(index);
			});
		},
		[index, created]() {
			editLayer([index, created](ModulationLayer& layer, ModulationRuntime&) {
				layer.insertModulator(index, created);
			});
		});
}

//! The undo/redo pair of a source edit: the previous source is captured by
//! value, so the step never reads the layer it is about to change.
void recordSource(int index, const ModulatorSource& previous, const ModulatorSource& written)
{
	addUndoStep(
		[index, previous]() {
			editLayer([index, previous](ModulationLayer& layer, ModulationRuntime&) {
				layer.setSource(index, previous);
			});
		},
		[index, written]() {
			editLayer([index, written](ModulationLayer& layer, ModulationRuntime&) {
				layer.setSource(index, written);
			});
		});
}

QJsonObject layerSchema()
{
	return objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("shape"), enumProperty({QStringLiteral("sine"), QStringLiteral("triangle"),
			QStringLiteral("square"), QStringLiteral("saw")})},
		{QStringLiteral("rate"), numberProperty()},
		{QStringLiteral("phase"), numberProperty()},
		{QStringLiteral("unipolar"), booleanProperty()},
		{QStringLiteral("active"), booleanProperty()},
	});
}

QJsonObject layerResultSchema()
{
	return objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("modulator_count"), integerProperty()},
		{QStringLiteral("removed"), objectProperty()},
		{QStringLiteral("source"), objectProperty()},
		{QStringLiteral("engine_active"), booleanProperty()},
		{QStringLiteral("driving"), integerProperty()},
	});
}

//! The single-modulator form of the layer state, for the commands' results.
QJsonObject layerResult(const ModulationLayer& layer, int index)
{
	QJsonObject out = modulationLayerJson(layer);
	if (index >= 0 && layer.modulator(index) != nullptr)
	{
		out.insert(QStringLiteral("modulator"), modulatorId(index));
		out.insert(QStringLiteral("source"), modulationSourceJson(layer.modulator(index)->source));
	}
	return out;
}

void registerGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.get_state");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("The modulation layer: every modulator with its source "
		"(shape, rate in Hz, phase, polarity, active) and every route it drives, with the route's "
		"address, its depth and whether it still resolves. 'driving' is the number of routes the "
		"audio thread will actually write - a route whose device is gone is reported, not hidden.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("modulators"), arrayProperty()},
		{QStringLiteral("modulator_count"), integerProperty()},
		{QStringLiteral("max_modulators"), integerProperty()},
		{QStringLiteral("max_targets_per_modulator"), integerProperty()},
		{QStringLiteral("driving"), integerProperty()},
		{QStringLiteral("engine_active"), booleanProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		return ControlResult::success(modulationLayerJson(songModulationLayer().layer()));
	};
	registry.registerCommand(cmd);
}

void registerCreate(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.create");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("create");
	cmd.description = QStringLiteral("Create a modulator: a timeline-locked LFO. Returns its "
		"modulator-<n> id. It drives nothing until modulator.target_set binds a parameter to it. "
		"Reversible through the ProjectJournal (an action checkpoint removes the modulator this "
		"command created).");
	cmd.argsSchema = layerSchema();
	cmd.resultSchema = layerResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ModulationLayer& layer = songModulationLayer().layer();
		if (layer.modulatorCount() >= ModulationLayer::MaxModulators)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the modulation layer is full (%1 modulators)")
					.arg(ModulationLayer::MaxModulators));
		}
		const QString name = args.value(QStringLiteral("name")).toString().trimmed();
		if (name.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'name' must not be empty: a modulator with no name cannot be "
					"identified in a project file"));
		}
		bool valid = true;
		bool shapeGiven = false;
		ModulatorSource source = modulationSourceFromArgs(args, ModulatorSource{}, &shapeGiven,
			&valid);
		if (!valid)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'shape' is not one of sine, triangle, square, saw"));
		}
		QString why;
		if (!layer.validSource(source, &why))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, why);
		}

		int index = -1;
		const QJsonObject before = modulationLayerJson(layer);
		editLayer([&](ModulationLayer& target, ModulationRuntime&) {
			index = target.addModulator(name, source);
		});
		if (index < 0)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("the modulation layer is full"));
		}
		recordCreation(index, *layer.modulator(index));

		QJsonObject result = layerResult(songModulationLayer().layer(), index);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("modulator.remove"),
				QJsonObject{{QStringLiteral("modulator"), modulatorId(index)}}, true,
				QStringLiteral("action checkpoint: the recorded undo step removes the modulator "
					"this command created, through the same ModulationLayer::removeModulator "
					"modulator.remove uses")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.remove");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("remove");
	cmd.description = QStringLiteral("Drop a modulator and every route it drives. Its targets are "
		"handed back to the values they had before it modulated them. Reversible through the "
		"ProjectJournal (the recorded undo step re-inserts the captured modulator at its own "
		"index, target list included).");
	cmd.argsSchema = objectSchema({{QStringLiteral("modulator"), stringProperty()}},
		{QStringLiteral("modulator")});
	cmd.resultSchema = layerResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ModulationLayer& layer = songModulationLayer().layer();
		ControlResult error;
		const int index = resolveModulatorIndex(layer,
			args.value(QStringLiteral("modulator")).toString(), &error);
		if (index < 0) { return error; }

		const Modulator captured = *layer.modulator(index);
		const QJsonObject before = modulationLayerJson(layer);
		editLayer([&](ModulationLayer& target, ModulationRuntime&) {
			target.removeModulator(index);
		});
		recordRemoval(index, captured);

		QJsonObject result = layerResult(songModulationLayer().layer(), -1);
		result.insert(QStringLiteral("removed"), modulatorJson(captured, index));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(modulatorJson(captured, index),
				QStringLiteral("modulator.create"),
				QJsonObject{{QStringLiteral("modulator"), modulatorId(index)}}, true,
				QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured "
					"modulator - name, source and every route - at its own index, so the layer "
					"and the modulator-<n> ids come back exactly; a hand replay is "
					"modulator.create followed by one modulator.target_set per entry of "
					"removed.targets")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerRateSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.rate_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("rate_set");
	cmd.description = QStringLiteral("Set a modulator's LFO. 'rate' is in Hz and 'phase' is 0..1 "
		"where in its cycle the modulator starts; 'shape' is one of sine/triangle/square/saw and "
		"'unipolar' makes the output 0..1 instead of -1..1. An argument the call omits keeps the "
		"value it had. Switching 'active' off hands every target back to its own value. "
		"Reversible through the ProjectJournal (the recorded undo step puts the previous source "
		"back).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("shape"), enumProperty({QStringLiteral("sine"), QStringLiteral("triangle"),
			QStringLiteral("square"), QStringLiteral("saw")})},
		{QStringLiteral("rate"), numberProperty()},
		{QStringLiteral("phase"), numberProperty()},
		{QStringLiteral("unipolar"), booleanProperty()},
		{QStringLiteral("active"), booleanProperty()},
	}, {QStringLiteral("modulator")});
	cmd.resultSchema = layerResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ModulationLayer& layer = songModulationLayer().layer();
		ControlResult error;
		const int index = resolveModulatorIndex(layer,
			args.value(QStringLiteral("modulator")).toString(), &error);
		if (index < 0) { return error; }

		const ModulatorSource previous = layer.modulator(index)->source;
		bool valid = true;
		bool ignored = false;
		const ModulatorSource wanted = modulationSourceFromArgs(args, previous, &ignored, &valid);
		if (!valid)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'shape' is not one of sine, triangle, square, saw"));
		}
		QString why;
		if (!layer.validSource(wanted, &why))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, why);
		}
		const QJsonObject before = layerResult(layer, index);
		editLayer([&](ModulationLayer& target, ModulationRuntime&) {
			target.setSource(index, wanted);
		});
		recordSource(index, previous, wanted);

		QJsonObject result = layerResult(songModulationLayer().layer(), index);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("modulator.rate_set"),
				QJsonObject{{QStringLiteral("modulator"), modulatorId(index)},
					{QStringLiteral("shape"), modulationShapeName(previous.shape)},
					{QStringLiteral("rate"), static_cast<double>(previous.rateHz)},
					{QStringLiteral("phase"), static_cast<double>(previous.phase)},
					{QStringLiteral("unipolar"), previous.unipolar},
					{QStringLiteral("active"), previous.active}}, true,
				QStringLiteral("action checkpoint: the recorded undo step writes the previous "
					"source back and re-resolves the modulator's targets from the values they "
					"had before it drove them")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerModulatorCommands(ControlRegistry& registry)
{
	registerGetState(registry);
	registerCreate(registry);
	registerRemove(registry);
	registerRateSet(registry);
}

} // namespace lmms
