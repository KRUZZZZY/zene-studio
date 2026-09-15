/*
 * ControlCommandsController.cpp - the controller.* group: the engine half of
 *                                 MIDI controller surfaces (feature row 19 -
 *                                 soft-takeover, LED/feedback output and
 *                                 saveable mapping templates).
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "AutomatableModel.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControllerConnection.h"
#include "ControllerSurface.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiPort.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

// ---------------------------------------------------------------------------
// The seam every command in this file goes through: the project's bound MIDI
// controls, addressed by the fullDisplayName() of the model each one drives.
// The name is not invented here - it is the one MidiLearn.cpp already stores on
// the port when it builds a binding (controller->midiPort().setName(
// target->fullDisplayName())), so a control learned through the menu, a control
// restored from a project file and a control this group creates all answer to
// the same address.
// ---------------------------------------------------------------------------

//! The model a connection drives, or nullptr when the connection is not in the
//! song's object tree (a model deleted after its controller was created).
//! Walks the same tree ControllerSurface::resolveTarget walks, but matches on
//! the connection's IDENTITY rather than on a name, so two controls that share
//! a display name cannot be swapped.
AutomatableModel* modelOfConnection(const ControllerConnection* wanted)
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return nullptr; }

	QList<QObject*> queue;
	queue.append(song);
	while (!queue.isEmpty())
	{
		QObject* obj = queue.takeFirst();
		auto* model = dynamic_cast<AutomatableModel*>(obj);
		if (model != nullptr && model->controllerConnection() == wanted)
		{
			return model;
		}
		queue.append(obj->findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly));
	}
	return nullptr;
}

//! Every bound MidiController in the project, with the model it drives.
struct BoundControl
{
	ControllerConnection* connection = nullptr;
	MidiController* controller = nullptr;
	AutomatableModel* model = nullptr;
	QString name;  //!< the model's fullDisplayName(), or empty when orphaned
};

QVector<BoundControl> boundControls()
{
	QVector<BoundControl> result;
	for (ControllerConnection* connection : ControllerConnection::connections())
	{
		auto* controller = dynamic_cast<MidiController*>(connection->getController());
		if (controller == nullptr) { continue; }

		BoundControl bound;
		bound.connection = connection;
		bound.controller = controller;
		bound.model = modelOfConnection(connection);
		if (bound.model != nullptr) { bound.name = bound.model->fullDisplayName(); }
		result.append(bound);
	}
	return result;
}

//! Whether any model in the song answers to \a name. Used only to tell "no
//! such control" from "that control exists but no MIDI controller drives it".
bool modelNamedExists(const QString& name)
{
	if (name.isEmpty()) { return false; }
	Song* song = Engine::getSong();
	if (song == nullptr) { return false; }

	QList<QObject*> queue;
	queue.append(song);
	while (!queue.isEmpty())
	{
		QObject* obj = queue.takeFirst();
		auto* model = dynamic_cast<AutomatableModel*>(obj);
		if (model != nullptr && model->fullDisplayName() == name) { return true; }
		queue.append(obj->findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly));
	}
	return false;
}

//! The control an argument names. An empty \a spec means "the project's only
//! bound MIDI control", which is what a single-controller session wants; with
//! more than one bound control it is refused rather than guessed, because
//! picking one silently would write to a control the caller did not choose.
BoundControl findBoundControl(const QString& spec, ControlResult* error)
{
	const QVector<BoundControl> all = boundControls();

	if (spec.isEmpty())
	{
		if (all.isEmpty())
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("this project has no MIDI-bound control: bind one with MIDI "
					"learn (midi.learn_toggle), or name one through controller.template_apply"));
			return BoundControl();
		}
		if (all.size() > 1)
		{
			QStringList names;
			for (const BoundControl& bound : all)
			{
				names.append(bound.name.isEmpty() ? QStringLiteral("<orphaned>") : bound.name);
			}
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("this project has %1 MIDI-bound controls and no 'control' was "
					"given: name one of %2").arg(all.size()).arg(names.join(QStringLiteral(", "))));
			return BoundControl();
		}
		return all.first();
	}

	for (const BoundControl& bound : all)
	{
		if (bound.name == spec) { return bound; }
	}

	// Distinguish "no such control" from "the control exists but is not bound
	// to MIDI": only the first is a name the caller got wrong.
	const bool exists = modelNamedExists(spec);
	*error = ControlResult::failure(ControlErrorKind::NotFound,
		exists
			? QStringLiteral("'%1' is not driven by a MIDI controller: only a model whose "
				"ControllerConnection holds a MidiController can be soft-taken-over or fed "
				"back to").arg(spec)
			: QStringLiteral("no model in this project is named '%1'").arg(spec));
	return BoundControl();
}

//! One bound control's state, as every command in this group reports it.
QJsonObject controlState(const BoundControl& bound)
{
	QJsonObject state;
	state.insert(QStringLiteral("control"), bound.name);
	state.insert(QStringLiteral("channel"), bound.controller->midiPort().inputChannel());
	state.insert(QStringLiteral("controller"), bound.controller->midiPort().inputController());
	state.insert(QStringLiteral("soft_takeover"), bound.controller->softTakeoverEnabled());
	state.insert(QStringLiteral("captured"), bound.controller->softTakeoverCaptured());
	state.insert(QStringLiteral("takeover_target"),
		static_cast<double>(bound.controller->softTakeoverTarget()));
	state.insert(QStringLiteral("feedback"), bound.controller->feedbackEnabled());
	// The model's own value: what a feedback write is a picture of. Reported
	// even for a control whose model has gone, as 0. The accessor is a template
	// (AutomatableModel::value<T>()), so the element type is named.
	state.insert(QStringLiteral("value"),
		bound.model != nullptr ? static_cast<double>(bound.model->value<float>()) : 0.0);
	// The port's own counters, read at the client boundary - see MidiPort.h.
	state.insert(QStringLiteral("output_events_offered"),
		static_cast<double>(bound.controller->midiPort().outputEventsOffered()));
	state.insert(QStringLiteral("output_events_written"),
		static_cast<double>(bound.controller->midiPort().outputEventsWritten()));
	return state;
}


QJsonObject softTakeoverSchema()
{
	return objectSchema({
		{QStringLiteral("control"), stringProperty()},
		{QStringLiteral("channel"), integerProperty(0, 16)},
		{QStringLiteral("controller"), integerProperty(-1, 127)},
		{QStringLiteral("soft_takeover"), booleanProperty()},
		{QStringLiteral("captured"), booleanProperty()},
		{QStringLiteral("takeover_target"), numberProperty()},
	});
}

QJsonObject feedbackSchema()
{
	return objectSchema({
		{QStringLiteral("control"), stringProperty()},
		{QStringLiteral("channel"), integerProperty(0, 16)},
		{QStringLiteral("controller"), integerProperty(-1, 127)},
		{QStringLiteral("feedback"), booleanProperty()},
		{QStringLiteral("written"), booleanProperty()},
		{QStringLiteral("output_events_offered"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("output_events_written"), integerProperty(0, MaxSongLength)},
	});
}

void registerControllerSurfaceState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.surface_state");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("surface_state");
	cmd.description = QStringLiteral("Read-only: every control in this project that a MIDI "
		"controller drives, addressed by the model's fullDisplayName(). Each entry carries the "
		"MIDI channel and controller number, the soft-takeover flag and whether the hardware "
		"has already taken the control over ('captured'), the value the takeover waits for "
		"('takeover_target'), the LED/feedback flag and the port's two output counters. "
		"'output_events_written' is the count of control-changes this control's port actually "
		"handed to the MIDI client's output - the measurement the LED half is proved by, taken "
		"at the client boundary in MidiPort (include/MidiPort.h) and not self-reported by the "
		"command. This is how an agent reads a surface without a controller in hand.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("count"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("controls"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
	});
	// A13: no display, device or human - the connections and the ports exist in an
	// offscreen instance, so no `requires` is declared and the headless sweep
	// exercises it (the midi.retro_capture_status precedent).
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		const QVector<BoundControl> all = boundControls();
		QJsonArray controls;
		for (const BoundControl& bound : all) { controls.append(controlState(bound)); }

		QJsonObject result;
		result.insert(QStringLiteral("count"), controls.size());
		result.insert(QStringLiteral("controls"), controls);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerControllerSoftTakeover(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.soft_takeover");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("soft_takeover");
	cmd.description = QStringLiteral("Turn soft-takeover on or off for one MIDI-bound control, "
		"and/or set the value it waits for. While soft-takeover is on, a hardware control that "
		"is bound to a control whose stored value differs from the knob's position is IGNORED: "
		"the model does not jump to wherever the knob happened to be. The hardware takes the "
		"control over the moment a control-change crosses the stored value (or lands within one "
		"MIDI step of it), and only then do that control-change and the ones after it move the "
		"model. Without 'target' the value the model holds now is used, which is the reading a "
		"user expects: the surface was in some position when the project was saved. 'control' "
		"names the model's fullDisplayName() (controller.surface_state lists the names); an "
		"empty or absent 'control' means the project's only bound MIDI control. 'captured' "
		"reports whether the hardware has already taken over - it is reset by the first "
		"movement after enabling, which is what makes a fresh binding wait for the knob.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("control"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("target"), numberProperty()},
	});
	cmd.resultSchema = softTakeoverSchema();
	// SPEC A16: the flag is serialized in the project (MidiController::saveSettings
	// writes the 'softtakeover' attribute into the model's <connection> element), so
	// the command IS mutating - but the ControllerConnection is not a
	// JournallingObject, so no checkpoint holds the previous flag and the row is a
	// snapshot with reversible=false (ControlReversibilityTableController.cpp).
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		const BoundControl bound = findBoundControl(
			args.value(QStringLiteral("control")).toString(), &error);
		if (bound.controller == nullptr) { return error; }

		const bool wasEnabled = bound.controller->softTakeoverEnabled();
		const float wasTarget = bound.controller->softTakeoverTarget();

		// No 'enabled' means "turn it on": the deterministic reading of a command
		// whose subject is the feature itself.
		const bool wanted = args.contains(QStringLiteral("enabled"))
			? args.value(QStringLiteral("enabled")).toBool() : true;
		bound.controller->setSoftTakeoverEnabled(wanted);

		if (args.contains(QStringLiteral("target")))
		{
			bound.controller->setSoftTakeoverTarget(
				static_cast<float>(args.value(QStringLiteral("target")).toDouble()));
		}
		else if (wanted && bound.model != nullptr)
		{
			// The value the model holds now, mapped onto the controller's 0..1
			// domain. The model's own range is what the fader position means, so
			// normalising by min/max is the takeover point a user is aiming at.
			// The accessors are templates (AutomatableModel::value<T>()).
			const float span = bound.model->maxValue<float>() - bound.model->minValue<float>();
			const float normalized = span > 0.0f
				? (bound.model->value<float>() - bound.model->minValue<float>()) / span : 0.0f;
			bound.controller->setSoftTakeoverTarget(normalized);
		}

		if (Engine::getSong() != nullptr) { Engine::getSong()->setModified(); }

		QJsonObject result = controlState(bound);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("control"), bound.name},
					{QStringLiteral("soft_takeover"), wasEnabled},
					{QStringLiteral("takeover_target"), static_cast<double>(wasTarget)}},
				QStringLiteral("controller.soft_takeover"),
				QJsonObject{{QStringLiteral("control"), bound.name},
					{QStringLiteral("enabled"), wasEnabled},
					{QStringLiteral("target"), static_cast<double>(wasTarget)}},
				false,
				QStringLiteral("the flag is serialized in the project through the model's own "
					"<connection> element; the ControllerConnection is not a JournallingObject, "
					"so no ProjectJournal checkpoint holds the previous value and the inverse "
					"is the recorded command, not control.undo")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerControllerFeedback(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("controller.feedback");
	cmd.group = QStringLiteral("controller");
	cmd.verb = QStringLiteral("feedback");
	cmd.description = QStringLiteral("Turn LED/feedback output on or off for one MIDI-bound "
		"control. With feedback on, every control-change that moves the model is written back "
		"to the controller as a control-change on the channel the hardware transmits on, so a "
		"motorised fader or a lit ring follows the value the project holds - including after a "
		"project load, a MIDI-learn binding or an automation step. Enabling it also moves the "
		"control's MIDI port to Duplex mode, because a port that is not output-enabled cannot "
		"carry the write; that port mode is serialized in the project, which is why this "
		"command records a transaction. 'written' reports whether THIS call's own feedback "
		"write was handed to the port's output path, and 'output_events_written' is the port's "
		"own count of control-changes that reached the MIDI client - 'written': false with a "
		"port that is not output-enabled is the honest answer, not an error. Without a real "
		"controller attached nothing consumes the bytes; the count is a write to the client, "
		"not a proof that a lamp lit.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("control"), stringProperty()},
		{QStringLiteral("enabled"), booleanProperty()},
	});
	cmd.resultSchema = feedbackSchema();
	// SPEC A16: the flag AND the port mode are serialized in the project, so this
	// mutates; neither is inside a journal checkpoint, so the row is a snapshot with
	// reversible=false (ControlReversibilityTableController.cpp).
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		const BoundControl bound = findBoundControl(
			args.value(QStringLiteral("control")).toString(), &error);
		if (bound.controller == nullptr) { return error; }

		const bool wasEnabled = bound.controller->feedbackEnabled();
		const MidiPort::Mode wasMode = bound.controller->midiPort().mode();
		// The measurement bracket: the port's own count of events that reached
		// the MIDI client, read before and after. 'written' is the delta, so it
		// is the port's number and not this handler's claim.
		const quint64 writesBefore = bound.controller->midiPort().outputEventsWritten();

		const bool wanted = args.contains(QStringLiteral("enabled"))
			? args.value(QStringLiteral("enabled")).toBool() : true;
		bound.controller->setFeedbackEnabled(wanted);

		if (Engine::getSong() != nullptr) { Engine::getSong()->setModified(); }

		const quint64 writesAfter = bound.controller->midiPort().outputEventsWritten();

		QJsonObject result = controlState(bound);
		result.insert(QStringLiteral("written"), writesAfter > writesBefore);
		result.insert(QStringLiteral("mode"),
			bound.controller->midiPort().mode() == MidiPort::Mode::Duplex
				? QStringLiteral("duplex") : QStringLiteral("not-duplex"));
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("control"), bound.name},
					{QStringLiteral("feedback"), wasEnabled},
					{QStringLiteral("port_mode"), static_cast<int>(wasMode)}},
				QStringLiteral("controller.feedback"),
				QJsonObject{{QStringLiteral("control"), bound.name},
					{QStringLiteral("enabled"), wasEnabled}},
				false,
				QStringLiteral("the flag and the port mode are serialized in the project "
					"through the model's own <connection> element; neither is inside a "
					"ProjectJournal checkpoint, so the inverse is the recorded command")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}


} // namespace


void registerControllerSurfaceCommands(ControlRegistry& registry)
{
	registerControllerSurfaceState(registry);
	registerControllerSoftTakeover(registry);
	registerControllerFeedback(registry);
	// The four file verbs of the group live in their own translation unit, split
	// off at the file-length ratchet: they touch the template store and no
	// controller or model (src/core/ControlCommandsControllerTemplates.cpp).
	registerControllerTemplateCommands(registry);
}


} // namespace lmms
