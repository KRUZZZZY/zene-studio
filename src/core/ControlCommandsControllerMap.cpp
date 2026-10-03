/*
 * ControlCommandsControllerMap.cpp - controller.map / unmap: bind a parameter to a MIDI controller, or free it
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

/*! WF-124 (0.5.0 L13): making and removing a MIDI mapping without MIDI learn - controller.map binds
 *  a parameter (addressed the automation way, track + parameter, or by the full display name the
 *  mapping list shows) to control-change N on channel C, the way learn and a controller template
 *  bind (bindMidiController); controller.unmap frees it. Each is one undo step (an action step that
 *  re-resolves the parameter by name, so it survives the model being rebuilt). */

#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControllerConnection.h"
#include "ControllerSurface.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiLearn.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! The parameter a call names: track + parameter (as automation.* address it), else 'target' by name.
AutomatableModel* parameterOf(const QJsonObject& args, ControlResult* error)
{
	if (args.contains(QStringLiteral("track")))
	{
		ControlTarget target;
		AutomationParameter parameter;
		return resolveAutomationParameter(args, &target, &parameter, error) ? parameter.model : nullptr;
	}
	const QString name = args.value(QStringLiteral("target")).toString();
	AutomatableModel* model = ControllerSurface::instance().resolveTarget(name);
	if (model == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no parameter named '%1' (give track + parameter, "
			"or the name controller.surface_state shows)").arg(name));
	}
	return model;
}

void bindByName(const QString& name, int channel, int controller)
{
	if (AutomatableModel* model = ControllerSurface::instance().resolveTarget(name)) { bindMidiController(model, channel, controller); }
}

void unbindByName(const QString& name)
{
	if (AutomatableModel* model = ControllerSurface::instance().resolveTarget(name)) { model->setControllerConnection(nullptr); }
}

//! The MIDI binding @a model has now (channel, controller), or -1s.
std::pair<int, int> bindingOf(const AutomatableModel* model)
{
	ControllerConnection* connection = model->controllerConnection();
	auto* midi = connection != nullptr ? dynamic_cast<MidiController*>(connection->getController()) : nullptr;
	if (midi == nullptr) { return {-1, -1}; }
	return {midi->midiPort().inputChannel(), midi->midiPort().inputController()};
}

ControlResult mapControl(const QJsonObject& args)
{
	ControlResult error;
	AutomatableModel* model = parameterOf(args, &error);
	if (model == nullptr) { return error; }
	const int channel = args.value(QStringLiteral("channel")).toInt(1);
	const int controller = args.value(QStringLiteral("controller")).toInt();
	const QString name = model->fullDisplayName();
	const auto [oldChannel, oldController] = bindingOf(model);
	addUndoStep([name, oldChannel, oldController]() {
		unbindByName(name);
		if (oldChannel >= 0) { bindByName(name, oldChannel, oldController); }
	}, [name, channel, controller]() { bindByName(name, channel, controller); });
	MidiController* bound = bindMidiController(model, channel, controller);
	bound->setSoftTakeoverEnabled(args.value(QStringLiteral("soft_takeover")).toBool());
	bound->setFeedbackEnabled(args.value(QStringLiteral("feedback")).toBool());
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("control"), name}, {QStringLiteral("channel"), channel}, {QStringLiteral("controller"), controller}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("control"), name}}, QStringLiteral("controller.unmap"),
		{{QStringLiteral("target"), name}}, true, QStringLiteral("action checkpoint: the recorded undo step puts the previous binding back")));
	return ControlResult::success(result);
}

ControlResult unmapControl(const QJsonObject& args)
{
	ControlResult error;
	AutomatableModel* model = parameterOf(args, &error);
	if (model == nullptr) { return error; }
	const auto [channel, controller] = bindingOf(model);
	if (channel < 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("that parameter has no MIDI mapping")); }
	const QString name = model->fullDisplayName();
	addUndoStep([name, channel, controller]() { bindByName(name, channel, controller); }, [name]() { unbindByName(name); });
	model->setControllerConnection(nullptr);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("control"), name}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("control"), name}}, QStringLiteral("controller.map"),
		{{QStringLiteral("target"), name}, {QStringLiteral("channel"), channel}, {QStringLiteral("controller"), controller}}, true,
		QStringLiteral("action checkpoint: the recorded undo step binds the parameter again")));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("controller.") + QLatin1String(verb), description, args, true, handler);
}

} // namespace

void registerControllerMapCommands(ControlRegistry& registry)
{
	const QJsonObject which{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("parameter"), stringProperty()},
		{QStringLiteral("target"), stringProperty()}};
	QJsonObject mapArgs = which;
	mapArgs.insert(QStringLiteral("channel"), integerProperty(1, 16));
	mapArgs.insert(QStringLiteral("controller"), integerProperty(0, 127));
	mapArgs.insert(QStringLiteral("soft_takeover"), booleanProperty());
	mapArgs.insert(QStringLiteral("feedback"), booleanProperty());
	add(registry, "map", QStringLiteral("Map a parameter to a MIDI controller without learn: the parameter as automation.* "
		"addresses it (track + parameter) or by the name controller.surface_state shows ('target'); control-change "
		"'controller' (0..127) on 'channel' (1..16, default 1), listening on every MIDI input; 'soft_takeover' and "
		"'feedback' as controller.soft_takeover / feedback set them. Replaces a mapping the parameter had. One undo step."),
		objectSchema(mapArgs, {QStringLiteral("controller")}), mapControl);
	add(registry, "unmap", QStringLiteral("Remove a parameter's MIDI mapping (addressed as controller.map takes it). One undo step."),
		objectSchema(which), unmapControl);
}

} // namespace lmms
