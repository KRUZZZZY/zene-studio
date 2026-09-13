/*
 * ControlCommandsModulatorRoutes.cpp - the modulator.* command group's ROUTE
 *                                      half (SPEC A11-A16): bind a parameter to
 *                                      a modulator, set a depth, unbind.
 *
 * The other half (the layer, the LFO source and the read-only inspector) is
 * ControlCommandsModulator.cpp. They are separate translation units for the
 * same reason the automation, warp, rack and comp groups are split: this
 * fork's file-length ratchet measures a file as a unit, and a group's
 * boilerplate alone does not fit twice in one file under the limit.
 *
 * A route's address is a rack macro target's address, minus the window: a mixer
 * channel, a chain inside its rack, an effect inside that chain and the
 * parameter's display name - resolved through the effect's own parameter list,
 * so a route means the same parameter to a human, to an agent and after a
 * reload, which no pointer would.
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

//! One edit of the layer, restoring the targets before the runtime is rebuilt
//! (see ControlCommandsModulator.cpp - the rule is stated there once).
void editLayer(const std::function<void(ModulationLayer&, ModulationRuntime&)>& change)
{
	songModulationLayer().edit([&change](ModulationLayer& layer, ModulationRuntime& runtime) {
		change(layer, runtime);
		rebuildModulationRuntimeRestoring(layer, &runtime);
		return true;
	});
}

/*! The undo/redo pairs the three route commands record beside their edit. Each
 *  captures VALUES (the modulator index, the route index, a copy of the route),
 *  never a pointer: an undo step can run long after the command that recorded
 *  it, and it re-resolves the layer through editLayer so the runtime is rebuilt
 *  the same way the edit rebuilt it.
 */
void recordRouteRemoval(int modulator, int route, const ModulationRoute& captured)
{
	addUndoStep(
		[modulator, route, captured]() {
			editLayer([modulator, route, captured](ModulationLayer& layer, ModulationRuntime&) {
				layer.insertRoute(modulator, route, captured);
			});
		},
		[modulator, route]() {
			editLayer([modulator, route](ModulationLayer& layer, ModulationRuntime&) {
				layer.removeRoute(modulator, route);
			});
		});
}

//! The same shape for a created route: the inverse removes it at its index,
//! so a later bind cannot make the undo eat a route nobody asked about.
void recordRouteCreation(int modulator, int route, const ModulationRoute& created)
{
	addUndoStep(
		[modulator, route]() {
			editLayer([modulator, route](ModulationLayer& layer, ModulationRuntime&) {
				layer.removeRoute(modulator, route);
			});
		},
		[modulator, route, created]() {
			editLayer([modulator, route, created](ModulationLayer& layer, ModulationRuntime&) {
				layer.insertRoute(modulator, route, created);
			});
		});
}

//! A depth edit. The route's captured base is untouched by the redo, which is
//! what makes a depth change not a move of the parameter.
void recordDepth(int modulator, int route, float previous, float written)
{
	addUndoStep(
		[modulator, route, previous]() {
			editLayer([modulator, route, previous](ModulationLayer& layer, ModulationRuntime&) {
				layer.setDepth(modulator, route, previous);
			});
		},
		[modulator, route, written]() {
			editLayer([modulator, route, written](ModulationLayer& layer, ModulationRuntime&) {
				layer.setDepth(modulator, route, written);
			});
		});
}

QJsonObject layerResult(const ModulationLayer& layer, int index)
{
	QJsonObject out = modulationLayerJson(layer);
	if (index >= 0 && layer.modulator(index) != nullptr)
	{
		out.insert(QStringLiteral("modulator"), modulatorId(index));
	}
	return out;
}

QJsonObject routeSchema(bool withAddress)
{
	QJsonObject properties{
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("target"), integerProperty()},
		{QStringLiteral("depth"), numberProperty()},
	};
	if (withAddress)
	{
		properties.insert(QStringLiteral("channel"), stringProperty());
		properties.insert(QStringLiteral("chain"), integerProperty());
		properties.insert(QStringLiteral("effect"), integerProperty());
		properties.insert(QStringLiteral("parameter"), stringProperty());
	}
	return properties;
}

QJsonObject routeResultSchema()
{
	return objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("modulator_count"), integerProperty()},
		{QStringLiteral("target_index"), integerProperty()},
		{QStringLiteral("target_count"), integerProperty()},
		{QStringLiteral("removed"), objectProperty()},
		{QStringLiteral("driving"), integerProperty()},
		{QStringLiteral("engine_active"), booleanProperty()},
		{QStringLiteral("modulators"), arrayProperty()},
		{QStringLiteral("max_modulators"), integerProperty()},
		{QStringLiteral("max_targets_per_modulator"), integerProperty()},
	});
}

/*! The address of a target_set call, refused before anything is written when
 *  it is malformed, does not resolve, or is already bound.
 */
ControlResult refusedRoute(const ModulationLayer& layer, const ModulationRoute& route)
{
	if (route.channel < 0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'channel' must be a ch-<n> id"));
	}
	if (route.parameter.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'parameter' must not be empty: it is the parameter's display name"));
	}
	const ModulationRoute* existing = layer.findRoute(route);
	if (existing != nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is already driven by a modulator: one parameter has one depth "
				"(modulator.depth_set changes it, modulator.target_remove unbinds it)")
				.arg(route.parameter));
	}
	QString why;
	// Refused at BIND time, like rack.macro_target_add: a stored route that
	// names nothing would only ever fail at apply time, silently.
	if (modulationTargetModel(route, &why) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' does not resolve to a parameter: %2").arg(route.parameter, why));
	}
	return ControlResult::success(QJsonObject{});
}

void registerTargetSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.target_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("target_set");
	cmd.description = QStringLiteral("Bind a parameter to a modulator. 'channel', 'chain' and "
		"'effect' address a device in a mixer channel's rack (fx-<n> order) and 'parameter' is "
		"its display name, exactly as rack.macro_target_add names one; 'depth' is the modulation "
		"amount as a FRACTION of that parameter's own range (-1..1), added to the value the "
		"parameter already has. Refused when the parameter does not resolve or is already driven, "
		"so a modulator never carries a route that can only fail. Reversible through the "
		"ProjectJournal.");
	cmd.argsSchema = objectSchema(routeSchema(true),
		{QStringLiteral("modulator"), QStringLiteral("channel"), QStringLiteral("effect"),
			QStringLiteral("parameter")});
	cmd.resultSchema = routeResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ModulationLayer& layer = songModulationLayer().layer();
		ControlResult error;
		const int index = resolveModulatorIndex(layer,
			args.value(QStringLiteral("modulator")).toString(), &error);
		if (index < 0) { return error; }
		if (layer.modulator(index)->routeCount() >= ModulationLayer::MaxRoutesPerModulator)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("modulator %1 already drives %2 parameters (the cap)")
					.arg(modulatorId(index)).arg(ModulationLayer::MaxRoutesPerModulator));
		}
		const ModulationRoute route = modulationRouteFromArgs(args);
		const ControlResult refused = refusedRoute(layer, route);
		if (!refused.ok) { return refused; }

		const QJsonObject before = modulationLayerJson(layer);
		int routeIndex = -1;
		editLayer([&](ModulationLayer& target, ModulationRuntime&) {
			routeIndex = target.addRoute(index, route);
		});
		if (routeIndex < 0)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("modulator %1 is full").arg(modulatorId(index)));
		}
		recordRouteCreation(index, routeIndex, route);

		QJsonObject result = layerResult(songModulationLayer().layer(), index);
		result.insert(QStringLiteral("target_index"), routeIndex);
		result.insert(QStringLiteral("target_count"),
			songModulationLayer().layer().modulator(index)->routeCount());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("modulator.target_remove"),
				QJsonObject{{QStringLiteral("modulator"), modulatorId(index)},
					{QStringLiteral("target"), routeIndex}}, true,
				QStringLiteral("action checkpoint: the recorded undo step removes the route this "
					"command appended (ModulationLayer::removeRoute at the same index) and hands "
					"the parameter back to the value it had before it was bound")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerDepthSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.depth_set");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("depth_set");
	cmd.description = QStringLiteral("Set one route's depth: the modulation amount as a FRACTION "
		"of the target's own range (-1..1). 0 leaves the route bound but driving nothing. The base "
		"value the parameter is modulated around is kept, so changing a depth does not re-read "
		"the parameter. Reversible through the ProjectJournal (the recorded undo step writes the "
		"previous depth back).");
	cmd.argsSchema = objectSchema(routeSchema(false),
		{QStringLiteral("modulator"), QStringLiteral("target"), QStringLiteral("depth")});
	cmd.resultSchema = routeResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ModulationLayer& layer = songModulationLayer().layer();
		ControlResult error;
		const int index = resolveModulatorIndex(layer,
			args.value(QStringLiteral("modulator")).toString(), &error);
		if (index < 0) { return error; }
		const Modulator* modulator = layer.modulator(index);
		const int route = static_cast<int>(args.value(QStringLiteral("target")).toDouble());
		if (route < 0 || route >= modulator->routeCount())
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("modulator %1 has no target %2 (it has %3)")
					.arg(modulatorId(index)).arg(route).arg(modulator->routeCount()));
		}
		const float previous = modulator->routes[static_cast<std::size_t>(route)].depth;
		const float depth = static_cast<float>(args.value(QStringLiteral("depth")).toDouble());
		if (!(depth >= -1.0f) || !(depth <= 1.0f))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'depth' %1 is outside -1..1 (a fraction of the target's range)")
					.arg(static_cast<double>(depth)));
		}
		editLayer([&](ModulationLayer& target, ModulationRuntime&) {
			target.setDepth(index, route, depth);
		});
		recordDepth(index, route, previous, depth);

		QJsonObject result = layerResult(songModulationLayer().layer(), index);
		result.insert(QStringLiteral("target_index"), route);
		result.insert(QStringLiteral("target_count"), modulator->routeCount());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(QJsonObject{{QStringLiteral("depth"), static_cast<double>(previous)}},
				QStringLiteral("modulator.depth_set"),
				QJsonObject{{QStringLiteral("modulator"), modulatorId(index)},
					{QStringLiteral("target"), route},
					{QStringLiteral("depth"), static_cast<double>(previous)}}, true,
				QStringLiteral("action checkpoint: the recorded undo step writes the previous "
					"depth back through ModulationLayer::setDepth; the route's captured base is "
					"untouched, because a depth change never re-reads the parameter")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerTargetRemove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("modulator.target_remove");
	cmd.group = kGroup;
	cmd.verb = QStringLiteral("target_remove");
	cmd.description = QStringLiteral("Unbind one parameter from a modulator, by its target index "
		"as modulator.get_state reports it. The parameter is handed back to the value it had "
		"before it was modulated. Reversible through the ProjectJournal (the recorded undo step "
		"re-inserts the captured route at its own index).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("modulator"), stringProperty()},
		{QStringLiteral("target"), integerProperty()},
	}, {QStringLiteral("modulator"), QStringLiteral("target")});
	cmd.resultSchema = routeResultSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ModulationLayer& layer = songModulationLayer().layer();
		ControlResult error;
		const int index = resolveModulatorIndex(layer,
			args.value(QStringLiteral("modulator")).toString(), &error);
		if (index < 0) { return error; }
		const Modulator* modulator = layer.modulator(index);
		const int route = static_cast<int>(args.value(QStringLiteral("target")).toDouble());
		if (route < 0 || route >= modulator->routeCount())
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("modulator %1 has no target %2 (it has %3)")
					.arg(modulatorId(index)).arg(route).arg(modulator->routeCount()));
		}
		const ModulationRoute captured = modulator->routes[static_cast<std::size_t>(route)];
		const QJsonObject before = modulationLayerJson(layer);
		editLayer([&](ModulationLayer& target, ModulationRuntime&) {
			target.removeRoute(index, route);
		});
		recordRouteRemoval(index, route, captured);

		QJsonObject result = layerResult(songModulationLayer().layer(), index);
		result.insert(QStringLiteral("removed"), modulationRouteJson(captured, route));
		result.insert(QStringLiteral("target_count"), modulator->routeCount());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("modulator.target_set"),
				QJsonObject{{QStringLiteral("modulator"), modulatorId(index)}}, true,
				QStringLiteral("action checkpoint: the recorded undo step re-inserts the captured "
					"route at its own index, so the bind order - which is the order the routes are "
					"applied in - comes back exactly; a hand replay is modulator.target_set with "
					"removed.channel, removed.chain, removed.effect, removed.parameter and "
					"removed.depth")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerModulatorRouteCommands(ControlRegistry& registry)
{
	registerTargetSet(registry);
	registerDepthSet(registry);
	registerTargetRemove(registry);
}

} // namespace lmms
