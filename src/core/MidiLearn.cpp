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

#include <QCoreApplication>
#include <QThread>

#include "AutomatableModel.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "MidiController.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "Song.h"

namespace lmms
{

// The MIDI input thread must stay free of locks, and both of these are read from
// it. A non-lock-free atomic would silently introduce one (a libatomic call), so
// refuse to build instead of degrading the input path.
static_assert(std::atomic<bool>::is_always_lock_free,
	"MIDI learn arms/disarms with a lock-free atomic: the MIDI input thread reads it per event");
static_assert(std::atomic<Qt::HANDLE>::is_always_lock_free,
	"the binding-thread seam is read from the GUI thread only, but stays a plain pointer atomic");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
	"the learn state is one lock-free word, read by the MIDI input thread per event");

namespace
{
// The whole learn state is ONE atomic word, so the MIDI input thread can check it and claim the
// pending slot in a single compare-and-swap. Layout:
//   bit 0       a control-change is waiting for the GUI thread (pending)
//   bit 1       learn mode is armed
//   bits 2-9    channel of the waiting control-change (1-based; 8 bits)
//   bits 10-25  controller number of the waiting control-change (16 bits)
//   bits 32-63  generation: every change the GUI thread makes to the learn state takes a new one
// A claim from the MIDI thread is published only if the word is still exactly what the thread read
// when it decided to claim. Any disarm, re-arm, focus change or cancel in between changes the
// generation, so a stale control-change cannot be published after the learn it belonged to ended.
constexpr std::uint64_t kPending = 1ull << 0;
constexpr std::uint64_t kArmed = 1ull << 1;
constexpr unsigned kChannelShift = 2;
constexpr std::uint64_t kChannelMask = 0xFFull << kChannelShift;
constexpr unsigned kControllerShift = 10;
constexpr std::uint64_t kControllerMask = 0xFFFFull << kControllerShift;
constexpr std::uint64_t kPayloadMask = kChannelMask | kControllerMask;
constexpr std::uint64_t kGenerationUnit = 1ull << 32;
constexpr int kMaxChannel = 0xFF;
constexpr int kMaxController = 0xFFFF;
} // namespace



MidiLearn* MidiLearn::instance()
{
	// Intentionally a plain singleton: this object is read from the MIDI input
	// thread and from the audio thread's controller-value path, and must not
	// drag QObject thread affinity, a mutex or a hidden allocation into either.
	static MidiLearn s_instance;
	return &s_instance;
}



bool MidiLearn::onGuiThread()
{
	// "The GUI thread" is the thread the application object lives on - the one
	// that owns the models, the Song and every MidiPort. In a headless test that
	// is the QCoreApplication's thread, which is why the tests exercise the
	// immediate path.
	const QCoreApplication* app = QCoreApplication::instance();
	return app != nullptr && QThread::currentThread() == app->thread();
}



void MidiLearn::setEnabled(bool enabled)
{
	// GUI thread. Every change takes a new generation, so a control-change the MIDI thread
	// read under the previous state cannot be published after this change.
	std::uint64_t state = m_slot.load(std::memory_order_acquire);
	std::uint64_t next = 0;
	do
	{
		next = ((enabled ? (state | kArmed) : (state & ~kArmed)) + kGenerationUnit);
	} while (!m_slot.compare_exchange_weak(state, next, std::memory_order_acq_rel, std::memory_order_acquire));
}



bool MidiLearn::isEnabled() const
{
	return (m_slot.load(std::memory_order_acquire) & kArmed) != 0;
}



void MidiLearn::setFocusTarget(AutomatableModel* model)
{
	// GUI thread only. The pointer is *not* stored as a plain pointer: a model
	// (a control on a track that the user then deletes) can die before the
	// deferred bind runs, and the QPointer turns that into "no target" instead
	// of a dangling read.
	m_focusTarget = model;
	m_focusTargetSet.store(model != nullptr, std::memory_order_release);
	// A new focus starts a new generation, so a claim made for the old focus is not published.
	std::uint64_t state = m_slot.load(std::memory_order_acquire);
	while (!m_slot.compare_exchange_weak(state, state + kGenerationUnit, std::memory_order_acq_rel,
		std::memory_order_acquire)) {}
}



AutomatableModel* MidiLearn::focusTarget() const
{
	// GUI thread only - see setFocusTarget(). A controller is never handed to
	// another thread, so this read cannot race with a controller-to-controller
	// move of the model's value.
	return m_focusTarget;
}



unsigned int MidiLearn::bindingCount() const
{
	return m_bindingCount.load(std::memory_order_acquire);
}



bool MidiLearn::hasPendingBinding() const
{
	return (m_slot.load(std::memory_order_acquire) & kPending) != 0;
}



void MidiLearn::cancelPendingBinding()
{
	// GUI thread. Clears the pending request and starts a new generation in the same step, so a
	// control-change the MIDI thread is claiming right now either lands before this (and is cleared
	// here) or fails its claim (and is dropped). It is never left behind to bind to a later learn.
	std::uint64_t state = m_slot.load(std::memory_order_acquire);
	std::uint64_t next = 0;
	do
	{
		next = ((state & ~kPending) + kGenerationUnit);
	} while (!m_slot.compare_exchange_weak(state, next, std::memory_order_acq_rel, std::memory_order_acquire));
}



Qt::HANDLE MidiLearn::lastBindingThreadId() const
{
	return m_lastBindingThreadId.load(std::memory_order_acquire);
}



void MidiLearn::requestBinding(int channel, int controllerNum, std::uint64_t seen)
{
	// MIDI input thread. Nothing here allocates or locks: one compare-and-swap on the learn word.
	// The swap publishes the channel and controller number together with the pending flag, and
	// it disarms the learn, since the first control-change of an armed learn spends it. It only
	// succeeds if the word still equals @a seen, the state handleMidiEvent() checked; if the GUI
	// thread changed the learn meanwhile, this control-change is dropped, never published late.
	if ((seen & kPending) != 0 || (seen & kArmed) == 0)
	{
		return;  // a request is already waiting (the first one wins), or the learn is no longer armed
	}
	if (channel < 0 || channel > kMaxChannel || controllerNum < 0 || controllerNum > kMaxController)
	{
		return;  // not a value a MIDI message can carry; nothing to bind
	}
	const std::uint64_t payload = (static_cast<std::uint64_t>(channel) << kChannelShift)
		| (static_cast<std::uint64_t>(controllerNum) << kControllerShift);
	const std::uint64_t next = ((seen & ~(kArmed | kPayloadMask)) | kPending | payload);
	m_slot.compare_exchange_strong(seen, next, std::memory_order_acq_rel, std::memory_order_acquire);
}

bool MidiLearn::handleMidiEvent(const MidiEvent& event)
{
	// Off by default, and the off path is a single atomic load: an inbound CC
	// stream costs one relaxed read per event when the user is not learning.
	const std::uint64_t seen = m_slot.load(std::memory_order_acquire);
	if ((seen & kArmed) == 0)
	{
		return false;
	}

	// Only control-change messages carry a mapping; notes and pitch bend pass.
	if (event.type() != MidiControlChange)
	{
		return false;
	}

	// Armed but nothing focused: a CC must not invent a target. The flag is read
	// instead of the target itself because the target is a GUI-thread object.
	if (!m_focusTargetSet.load(std::memory_order_acquire))
	{
		return false;
	}

	// MidiPort input channels are 1-based, with 0 meaning "any channel"; bind to
	// the channel the controller actually transmitted on.
	const int channel = event.channel() + 1;
	const int controllerNum = event.controllerNumber();

	if (onGuiThread())
	{
		// Already where the model lives - build the binding now. Nothing is
		// deferred, so a caller on the GUI thread keeps the immediate semantics
		// it has always had.
		return bindFocusedControl(channel, controllerNum);
	}

	// MIDI input thread. Hand the *data* to the GUI thread, not an object: the
	// MidiController and the ControllerConnection - and the
	// AutomatableModel::setControllerConnection() write the GUI renders from -
	// are built later, by applyPendingBinding(), on the thread that owns them.
	requestBinding(channel, controllerNum, seen);
	return false;
}



bool MidiLearn::applyPendingBinding()
{
	// GUI thread only. This is the deferred half of handleMidiEvent(): it is
	// where a control-change seen on the MIDI input thread becomes a binding.
	// Taking the pending flag and reading the payload come from the same word, so they always agree.
	std::uint64_t state = m_slot.load(std::memory_order_acquire);
	do
	{
		if ((state & kPending) == 0)
		{
			return false;
		}
	} while (!m_slot.compare_exchange_weak(state, state & ~kPending, std::memory_order_acq_rel,
		std::memory_order_acquire));
	const int channel = static_cast<int>((state & kChannelMask) >> kChannelShift);
	const int controllerNum = static_cast<int>((state & kControllerMask) >> kControllerShift);
	return bindFocusedControl(channel, controllerNum);
}

MidiController* bindMidiController(AutomatableModel* target, int channel, int controllerNum)
{
	// The controller is deliberately parentless. It is created here, on the GUI
	// thread, so parenting it would no longer splice a QObject into the Song's
	// child graph from the wrong thread - but it stays parentless because
	// ownership is already settled: ControllerConnection owns Midi controllers
	// (setController() sets m_ownsController for them) and the target model
	// deletes the connection. Re-parenting it would change where it appears in
	// the project's object graph for no gain.
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
	return controller;
}

bool MidiLearn::bindFocusedControl(int channel, int controllerNum)
{
	// GUI thread only. Everything below touches GUI-thread state: the focused
	// model, the Song, the MidiPorts, and the model's connection pointer.
	//
	// Lifetime: the target is resolved *here*, on the owning thread, and held as
	// a QPointer. There is no window in which another thread has been told to
	// touch it: the MIDI input thread never received the pointer in the first
	// place. If the project, track or plugin went away between the CC and this
	// call, the QPointer is already null and the request is dropped - the
	// request is data, so dropping it is safe.
	AutomatableModel* target = m_focusTarget;
	if (target == nullptr)
	{
		return false;
	}

	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		return false;
	}

	bindMidiController(target, channel, controllerNum);

	// The seam a test asserts on: the thread that ran the write above.
	m_lastBindingThreadId.store(QThread::currentThreadId(), std::memory_order_release);

	// The binding lives in the project file, so the project is now dirty.
	song->setModified();

	// One-shot: a learn binds exactly one control, then disarms.
	setFocusTarget(nullptr);
	setEnabled(false);
	m_bindingCount.fetch_add(1, std::memory_order_relaxed);

	return true;
}




} // namespace lmms
