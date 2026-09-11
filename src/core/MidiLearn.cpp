/*
 * MidiLearn.cpp - implementation of the global MIDI-learn mode.
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

#include "MidiLearn.h"

#include "AutomatableModel.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "Song.h"

namespace lmms
{

MidiLearn* MidiLearn::instance()
{
	// Intentionally a plain singleton: this object is read from the MIDI input
	// thread and from the audio thread's controller-value path, and must not
	// drag QObject thread affinity, a mutex or a hidden allocation into either.
	static MidiLearn s_instance;
	return &s_instance;
}




void MidiLearn::setEnabled(bool enabled)
{
	m_enabled.store(enabled, std::memory_order_release);
}




bool MidiLearn::isEnabled() const
{
	return m_enabled.load(std::memory_order_acquire);
}




void MidiLearn::setFocusTarget(AutomatableModel* model)
{
	m_focusTarget.store(model, std::memory_order_release);
}




AutomatableModel* MidiLearn::focusTarget() const
{
	return m_focusTarget.load(std::memory_order_acquire);
}




unsigned int MidiLearn::bindingCount() const
{
	return m_bindingCount.load(std::memory_order_acquire);
}




bool MidiLearn::handleMidiEvent(const MidiEvent& event)
{
	// Off by default, and the off path is a single atomic load: an inbound CC
	// stream costs one relaxed read per event when the user is not learning.
	if (!isEnabled())
	{
		return false;
	}

	// Only control-change messages carry a mapping; notes and pitch bend pass.
	if (event.type() != MidiControlChange)
	{
		return false;
	}

	AutomatableModel* target = focusTarget();
	if (target == nullptr)
	{
		return false;
	}

	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		return false;
	}

	// MidiPort input channels are 1-based, with 0 meaning "any channel"; bind to
	// the channel the controller actually transmitted on.
	const int channel = event.channel() + 1;
	const int controllerNum = event.controllerNumber();

	// The controller is deliberately parentless. This runs on a MIDI input
	// thread, and a parent would splice a new QObject into the Song's child graph
	// from the wrong thread; lifetime is already covered - ControllerConnection
	// owns Midi controllers (setController() sets m_ownsController for them) and
	// the target model deletes the connection.
	auto* controller = new MidiController(nullptr);
	controller->midiPort().setInputChannel(channel);
	controller->midiPort().setInputController(controllerNum);

	// Listen to every port the client offers, exactly like the "Auto Detect"
	// path in ControllerConnectionDialog does once it has a hit.
	MidiPort::Map ports = controller->midiPort().readablePorts();
	for (auto it = ports.begin(); it != ports.end(); ++it)
	{
		it.value() = true;
	}
	controller->subscribeReadablePorts(ports);
	controller->updateName();
	controller->midiPort().setName(target->fullDisplayName());

	auto* connection = new ControllerConnection(controller);
	target->setControllerConnection(connection);

	// The binding lives in the project file, so the project is now dirty.
	song->setModified();

	// One-shot: a learn binds exactly one control, then disarms.
	setFocusTarget(nullptr);
	setEnabled(false);
	m_bindingCount.fetch_add(1, std::memory_order_relaxed);

	return true;
}




} // namespace lmms
