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
// Threads: setEnabled()/setFocusTarget() are called from the GUI thread (the
// MIDI Learn menu action and the GUI focus filter). handleMidiEvent() is called
// from the MIDI *input* thread - MidiAlsaSeq::run() for the sequencer client and
// MidiClientRaw::processParsedEvent() for the raw clients - and is never called
// from the audio render thread. The state is lock-free atomics and, with learn
// mode off, handleMidiEvent() does nothing but one relaxed load: no mutex, no
// allocation. A binding is only ever built when the user armed learn mode and a
// MIDI control-change actually arrived.
class MidiLearn
{
public:
	static MidiLearn* instance();

	void setEnabled(bool enabled);
	bool isEnabled() const;

	void setFocusTarget(AutomatableModel* model);
	AutomatableModel* focusTarget() const;

	//! The single entry point every MIDI input path feeds.
	//! Returns true when this event completed a learn and a binding was created.
	bool handleMidiEvent(const MidiEvent& event);

	//! Number of bindings created since process start (tests / diagnostics).
	unsigned int bindingCount() const;

private:
	MidiLearn() = default;

	MidiLearn(const MidiLearn&) = delete;
	MidiLearn& operator=(const MidiLearn&) = delete;

	std::atomic<bool> m_enabled{false};
	std::atomic<AutomatableModel*> m_focusTarget{nullptr};
	std::atomic<unsigned int> m_bindingCount{0};
};

} // namespace lmms

#endif // LMMS_MIDI_LEARN_H
