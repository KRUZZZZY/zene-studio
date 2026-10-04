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

#include <QPointer>

#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Controller.h"
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

/*! What @a model was connected to before a map replaced it (W1): a MIDI binding, or a song controller - an LFO, a peak
 *  follower - that the connection does not own. Undoing the map must put EITHER back; before W1 only the MIDI one came
 *  back, and a replaced LFO connection was lost. The song controller is GUARDED: a controller deleted since is skipped
 *  (its connection could not come back), never dereferenced. */
struct PreviousConnection
{
	MidiBinding midi;
	QPointer<Controller> song;
};

PreviousConnection previousOf(const AutomatableModel* model)
{
	PreviousConnection out{bindingOf(model), nullptr};
	ControllerConnection* connection = model->controllerConnection();
	Controller* controller = connection != nullptr ? connection->getController() : nullptr;
	const bool songOwned = controller != nullptr && out.midi.channel < 0 && controller->type() != Controller::ControllerType::Dummy;
	if (songOwned) { out.song = controller; }
	return out;
}

//! Puts @a previous back on the parameter named @a name (after the map's own binding was taken off).
void restoreByName(const QString& name, const PreviousConnection& previous)
{
	AutomatableModel* model = ControllerSurface::instance().resolveTarget(name);
	if (model == nullptr) { return; }
	if (previous.midi.channel >= 0) { bindTo(model, previous.midi); }
	else if (!previous.song.isNull()) { model->setControllerConnection(new ControllerConnection(previous.song.data())); }
}

//! controller.map's arguments that bind @a name as @a binding: the inverse of a change that replaced or removed it.
QJsonObject mapArgsOf(const QString& name, const MidiBinding& binding)
{
	return {{QStringLiteral("target"), name}, {QStringLiteral("channel"), binding.channel}, {QStringLiteral("controller"), binding.controller},
		{QStringLiteral("soft_takeover"), binding.softTakeover}, {QStringLiteral("feedback"), binding.feedback}};
}

/*! controller.map's transaction. A mapping that replaced a MIDI one is undone by mapping the old one again, and one that
 *  replaced nothing by unmapping; one that replaced a song controller's connection (an LFO) has no command that reconnects
 *  it, so its inverse is the recorded step itself - control.undo - and the before-state names the controller. */
QJsonObject mapTransaction(const QString& name, const PreviousConnection& old)
{
	const QString mechanism = QStringLiteral("action checkpoint: the recorded undo step puts the previous binding back");
	if (old.midi.channel >= 0)
	{
		return transactionPayload({{QStringLiteral("control"), name}}, QStringLiteral("controller.map"), mapArgsOf(name, old.midi), true, mechanism);
	}
	if (!old.song.isNull())
	{
		return transactionPayload({{QStringLiteral("control"), name}, {QStringLiteral("connected_to"), old.song->name()}},
			QStringLiteral("control.undo"), {}, true, mechanism + QStringLiteral(" - the connection to the song controller it "
				"replaced, re-made on the same controller"));
	}
	return transactionPayload({{QStringLiteral("control"), name}}, QStringLiteral("controller.unmap"), {{QStringLiteral("target"), name}},
		true, mechanism);
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
	const PreviousConnection old = previousOf(model);
	addUndoStep([name, old]() {
		unbindByName(name);
		restoreByName(name, old);
	}, [name, binding]() { bindByName(name, binding); });
	bindTo(model, binding);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("control"), name}, {QStringLiteral("channel"), channel}, {QStringLiteral("controller"), controller}};
	result.insert(QStringLiteral("__transaction"), mapTransaction(name, old));
	return ControlResult::success(result);
}

ControlResult unmapControl(const QJsonObject& args)
{
	ControlResult error;
	AutomatableModel* model = parameterOf(args, &error);
	if (model == nullptr) { return error; }
	const MidiBinding old = bindingOf(model);
	if (old.channel < 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("that parameter has no MIDI mapping")); }
	const QString name = model->fullDisplayName();
	addUndoStep([name, old]() { bindByName(name, old); }, [name]() { unbindByName(name); });
	model->setControllerConnection(nullptr);
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("control"), name}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("control"), name}}, QStringLiteral("controller.map"),
		mapArgsOf(name, old), true, QStringLiteral("action checkpoint: the recorded undo step binds the parameter again")));
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
