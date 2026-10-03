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

//! A MIDI mapping as one undo step has to put it back: the control-change and its two options.
struct MidiBinding
{
	int channel = -1;
	int controller = -1;
	bool softTakeover = false;
	bool feedback = false;
};

MidiController* bindTo(AutomatableModel* model, const MidiBinding& binding)
{
	MidiController* bound = bindMidiController(model, binding.channel, binding.controller);
	bound->setSoftTakeoverEnabled(binding.softTakeover);
	bound->setFeedbackEnabled(binding.feedback);
	return bound;
}

void bindByName(const QString& name, const MidiBinding& binding)
{
	if (AutomatableModel* model = ControllerSurface::instance().resolveTarget(name)) { bindTo(model, binding); }
}

void unbindByName(const QString& name)
{
	if (AutomatableModel* model = ControllerSurface::instance().resolveTarget(name)) { model->setControllerConnection(nullptr); }
}

//! The MIDI binding @a model has now, or a channel of -1.
MidiBinding bindingOf(const AutomatableModel* model)
{
	ControllerConnection* connection = model->controllerConnection();
	auto* midi = connection != nullptr ? dynamic_cast<MidiController*>(connection->getController()) : nullptr;
	if (midi == nullptr) { return {}; }
	return {midi->midiPort().inputChannel(), midi->midiPort().inputController(), midi->softTakeoverEnabled(), midi->feedbackEnabled()};
}

ControlResult mapControl(const QJsonObject& args)
{
	ControlResult error;
	AutomatableModel* model = parameterOf(args, &error);
	if (model == nullptr) { return error; }
	const MidiBinding binding{args.value(QStringLiteral("channel")).toInt(1), args.value(QStringLiteral("controller")).toInt(),
		args.value(QStringLiteral("soft_takeover")).toBool(), args.value(QStringLiteral("feedback")).toBool()};
	const int channel = binding.channel;
	const int controller = binding.controller;
	const QString name = model->fullDisplayName();
	const MidiBinding old = bindingOf(model);
	addUndoStep([name, old]() {
		unbindByName(name);
		if (old.channel >= 0) { bindByName(name, old); }
	}, [name, binding]() { bindByName(name, binding); });
	bindTo(model, binding);
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
	const MidiBinding old = bindingOf(model);
	const int channel = old.channel;
	const int controller = old.controller;
	if (channel < 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("that parameter has no MIDI mapping")); }
	const QString name = model->fullDisplayName();
	addUndoStep([name, old]() { bindByName(name, old); }, [name]() { unbindByName(name); });
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
