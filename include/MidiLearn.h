/*
 * MidiLearn.h - a single global MIDI-learn mode: focus a control, move a
 *               hardware control-change message, get a binding saved with
 *               the project.
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

#ifndef LMMS_MIDI_LEARN_H
#define LMMS_MIDI_LEARN_H

#include <QPointer>
#include <QtGlobal>

#include <atomic>

namespace lmms
{

class AutomatableModel;
class MidiEvent;

//! Global, one-shot MIDI-learn state.
//
// Flow: arm learn mode, point it at the control the user focused, then move a
// hardware control. The next incoming MIDI control-change message is bound to
// that control as an ordinary MidiController/ControllerConnection pair - the
// very same objects the "Connect to controller" dialog builds - so the binding
// serialises with the project through the existing ControllerConnection
// conventions and reloads identically. Learn mode disarms itself once it binds.
//
// Threads. This object is touched from two threads and the split is deliberate:
//
//   GUI thread - setEnabled(), setFocusTarget(), focusTarget(),
//                applyPendingBinding() and the bind itself. The target model,
//                the MidiController and the ControllerConnection are all owned
//                by this thread, and AutomatableModel::setControllerConnection()
//                - which writes the pointer the GUI renders from - runs here.
//
//   MIDI input thread - handleMidiEvent() (MidiAlsaSeq::run() for the sequencer
//                client, MidiClientRaw::processParsedEvent() for the raw
//                clients). It does one atomic load per event while disarmed and,
//                when armed and a control is focused, records the control-change
//                in a lock-free slot. It allocates nothing, takes no lock, never
//                waits, and never touches a model, a Song or a MidiPort.
//
// The hand-off carries plain integers (channel, controller number), never a
// pointer to a model, so there is no object whose lifetime must be kept alive
// across the hand-off. applyPendingBinding() runs on the GUI thread, resolves
// the focused control there (a QPointer, so a model that died in the meantime
// reads as "no target"), and is driven by MidiLearnGui's bind timer.
class MidiLearn
{
public:
	static MidiLearn* instance();

	//! Arm/disarm learn mode. GUI thread.
	void setEnabled(bool enabled);
	bool isEnabled() const;

	//! Register the control the user just focused. GUI thread only: the pointer
	//! is kept as a QPointer, so it is nulled by the model's own destruction
	//! instead of going stale while a binding is in flight.
	void setFocusTarget(AutomatableModel* model);
	AutomatableModel* focusTarget() const;

	//! The single entry point every MIDI input path feeds. Called from the MIDI
	//! input thread and from the GUI thread.
	//!
	//! On the GUI thread it builds the binding immediately and returns true when
	//! one was created. On any other thread it records the request in the
	//! lock-free pending slot (no allocation) and returns false - the binding is
	//! created later, on the GUI thread, by applyPendingBinding().
	bool handleMidiEvent(const MidiEvent& event);

	//! Apply a binding the MIDI input thread requested. GUI thread only.
	//! Returns true when this call created a binding.
	bool applyPendingBinding();

	//! True while a control-change from the MIDI input thread is waiting for the
	//! GUI thread to build its binding (tests / diagnostics).
	bool hasPendingBinding() const;

	//! Number of bindings created since process start (tests / diagnostics).
	unsigned int bindingCount() const;

	//! Identity of the thread that created the most recent binding - the seam a
	//! test asserts on to prove the write is not on the MIDI input thread. Null
	//! until the first binding. (tests / diagnostics)
	Qt::HANDLE lastBindingThreadId() const;

private:
	MidiLearn() = default;

	MidiLearn(const MidiLearn&) = delete;
	MidiLearn& operator=(const MidiLearn&) = delete;

	//! GUI thread only: build the MidiController/ControllerConnection for the
	//! focused control and install it. This is the deferred write.
	bool bindFocusedControl(int channel, int controllerNum);

	//! MIDI input thread: record the request. No allocation, no lock.
	void requestBinding(int channel, int controllerNum);

	//! True when the calling thread is the application (GUI) thread.
	static bool onGuiThread();

	std::atomic<bool> m_enabled{false};

	//! The MIDI input thread cannot look at m_focusTarget (a QPointer owned by
	//! the GUI thread); it reads this flag instead.
	std::atomic<bool> m_focusTargetSet{false};
	QPointer<AutomatableModel> m_focusTarget;

	//! Lock-free single slot: payload first, then m_pendingBinding with release
	//! ordering. One outstanding request at a time, by construction - the first
	//! control-change of an armed learn claims the slot and spends the learn.
	std::atomic<bool> m_pendingBinding{false};
	std::atomic<int> m_pendingChannel{0};
	std::atomic<int> m_pendingController{0};

	std::atomic<unsigned int> m_bindingCount{0};
	std::atomic<Qt::HANDLE> m_lastBindingThreadId{nullptr};
};

} // namespace lmms

#endif // LMMS_MIDI_LEARN_H
