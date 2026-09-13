/*
 * ControlModulationSupport.cpp - the modulator.* group's shared helpers: the
 *                                target resolver, the ids and the JSON shapes
 *                                (SPEC A11-A16; docs/MODULATION.md).
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

#include "ControlModulationSupport.h"

#include <algorithm>

#include <QJsonArray>

#include "AutomatableModel.h"
#include "ControlRackSupport.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "RackMacros.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // channelId()/idToIndex() live in ControlVocabulary.h

ModulationLayerPublisher& songModulationLayer()
{
	return Engine::getSong()->modulationLayer();
}

// The resolver is the RACK lane's own: a modulator route names a parameter
// exactly the way a rack macro target does, and a second implementation would
// be a second opinion about what a parameter name means.
AutomatableModel* modulationTargetModel(const ModulationRoute& route, QString* why)
{
	ControlResult error;
	Rack* rack = resolveRack(channelId(route.channel), &error);
	if (rack == nullptr)
	{
		if (why != nullptr) { *why = error.errorMessage; }
		return nullptr;
	}
	RackMacroTarget target;
	target.chain = route.chain;
	target.effect = route.effect;
	target.parameter = route.parameter;
	QString local;
	return rackMacroTargetModel(*rack, target, why != nullptr ? why : &local);
}

QString modulatorId(int index)
{
	return QStringLiteral("modulator-") + QString::number(index);
}

int resolveModulatorIndex(const ModulationLayer& layer, const QString& id, ControlResult* error)
{
	const int index = idToIndex(id, QStringLiteral("modulator-"));
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a modulator id of the form modulator-<n>").arg(id));
		return -1;
	}
	if (index >= layer.modulatorCount())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no modulator %1 (the layer has %2)")
				.arg(modulatorId(index)).arg(layer.modulatorCount()));
		return -1;
	}
	return index;
}

QJsonObject modulationSourceJson(const ModulatorSource& source)
{
	QJsonObject out;
	out.insert(QStringLiteral("shape"), modulationShapeName(source.shape));
	out.insert(QStringLiteral("rate"), static_cast<double>(source.rateHz));
	out.insert(QStringLiteral("phase"), static_cast<double>(source.phase));
	out.insert(QStringLiteral("unipolar"), source.unipolar);
	out.insert(QStringLiteral("active"), source.active);
	// The LFO's own bounds, published so a caller can define a rate without a
	// round trip through a refusal.
	out.insert(QStringLiteral("min_rate"), static_cast<double>(ModulationLayer::MinRateHz));
	out.insert(QStringLiteral("max_rate"), static_cast<double>(ModulationLayer::MaxRateHz));
	return out;
}

QJsonObject modulationRouteJson(const ModulationRoute& route, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("channel"), channelId(route.channel));
	out.insert(QStringLiteral("chain"), route.chain);
	out.insert(QStringLiteral("effect"), route.effect);
	out.insert(QStringLiteral("parameter"), route.parameter);
	out.insert(QStringLiteral("depth"), static_cast<double>(route.depth));
	QString why;
	out.insert(QStringLiteral("resolved"), modulationTargetModel(route, &why) != nullptr);
	if (!why.isEmpty()) { out.insert(QStringLiteral("why"), why); }
	return out;
}

QJsonObject modulatorJson(const Modulator& modulator, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), modulatorId(index));
	out.insert(QStringLiteral("name"), modulator.name);
	out.insert(QStringLiteral("source"), modulationSourceJson(modulator.source));
	QJsonArray routes;
	for (int i = 0; i < modulator.routeCount(); ++i)
	{
		routes.append(modulationRouteJson(modulator.routes[static_cast<std::size_t>(i)], i));
	}
	out.insert(QStringLiteral("targets"), routes);
	out.insert(QStringLiteral("target_count"), modulator.routeCount());
	return out;
}

QJsonObject modulationLayerJson(const ModulationLayer& layer)
{
	QJsonArray modulators;
	for (int i = 0; i < layer.modulatorCount(); ++i)
	{
		const Modulator* modulator = layer.modulator(i);
		if (modulator != nullptr) { modulators.append(modulatorJson(*modulator, i)); }
	}
	QJsonObject out;
	out.insert(QStringLiteral("modulators"), modulators);
	out.insert(QStringLiteral("modulator_count"), layer.modulatorCount());
	out.insert(QStringLiteral("max_modulators"), ModulationLayer::MaxModulators);
	out.insert(QStringLiteral("max_targets_per_modulator"), ModulationLayer::MaxRoutesPerModulator);
	// Honest, and the number a caller needs to know whether the layer can
	// drive anything at all: the resolved routes the audio thread will write.
	const ModulationRuntime& runtime = songModulationLayer().runtime();
	out.insert(QStringLiteral("driving"), runtime.entryCount);
	out.insert(QStringLiteral("engine_active"), runtime.active());
	return out;
}

ModulatorSource modulationSourceFromArgs(const QJsonObject& args, const ModulatorSource& current,
	bool* shapeGiven, bool* valid)
{
	ModulatorSource source = current;
	*valid = true;
	*shapeGiven = args.contains(QStringLiteral("shape"));
	if (*shapeGiven && !modulationShapeFromName(args.value(QStringLiteral("shape")).toString(),
			&source.shape))
	{
		*valid = false;
		return source;
	}
	if (args.contains(QStringLiteral("rate")))
	{
		source.rateHz = static_cast<float>(args.value(QStringLiteral("rate")).toDouble());
	}
	if (args.contains(QStringLiteral("phase")))
	{
		source.phase = static_cast<float>(args.value(QStringLiteral("phase")).toDouble());
	}
	if (args.contains(QStringLiteral("unipolar")))
	{
		source.unipolar = args.value(QStringLiteral("unipolar")).toBool();
	}
	if (args.contains(QStringLiteral("active")))
	{
		source.active = args.value(QStringLiteral("active")).toBool();
	}
	return source;
}

ModulationRoute modulationRouteFromArgs(const QJsonObject& args)
{
	ModulationRoute route;
	route.channel = idToIndex(args.value(QStringLiteral("channel")).toString(), QStringLiteral("ch-"));
	route.chain = static_cast<int>(args.value(QStringLiteral("chain")).toDouble());
	route.effect = static_cast<int>(args.value(QStringLiteral("effect")).toDouble());
	route.parameter = args.value(QStringLiteral("parameter")).toString();
	route.depth = std::clamp(static_cast<float>(args.value(QStringLiteral("depth")).toDouble()),
		-1.0f, 1.0f);
	return route;
}

} // namespace lmms
