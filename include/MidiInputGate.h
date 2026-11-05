/*
 * MidiInputGate.h - lets a MIDI client wait out the input callbacks it does not own
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio - an LMMS-derived DAW
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

#ifndef LMMS_MIDI_INPUT_GATE_H
#define LMMS_MIDI_INPUT_GATE_H

#include <atomic>

#include <QThread>

namespace lmms
{

/*! Q050-488: JACK, WinMM and CoreMIDI call a client's input code on a thread the client
 *  cannot join, so MidiClient::stopInput() cannot be a join there. A callback enters this
 *  gate around its dispatch; close() refuses later entries and waits for those inside.
 *
 *  enter()/leave() may run on a realtime thread: they are wait-free and neither allocate
 *  nor lock, and a refused callback just returns. Only close() waits, on the closing
 *  thread (Engine::destroy(), the main thread). It must not be called from a dispatch.
 *
 *  Both sides use sequentially consistent operations: either the callback sees the gate
 *  closed, or close() sees the callback's count and waits for it. */
class MidiInputGate
{
public:
	//! True if the caller may dispatch; it must then call leave().
	bool enter() noexcept
	{
		m_inside.fetch_add(1);
		if (m_closed.load())
		{
			m_inside.fetch_sub(1);
			return false;
		}
		return true;
	}

	void leave() noexcept
	{
		m_inside.fetch_sub(1);
	}

	//! When this returns no dispatch is inside and none can enter. Idempotent.
	void close()
	{
		m_closed.store(true);
		while (m_inside.load() != 0)
		{
			QThread::msleep(1);
		}
	}

	//! Holds the gate for one dispatch; test the object before dispatching.
	class Scope
	{
	public:
		explicit Scope(MidiInputGate& gate) noexcept : m_gate(gate.enter() ? &gate : nullptr) {}
		~Scope()
		{
			if (m_gate != nullptr) { m_gate->leave(); }
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
		explicit operator bool() const noexcept { return m_gate != nullptr; }

	private:
		MidiInputGate* m_gate;
	};

private:
	std::atomic<bool> m_closed{false};
	std::atomic<int> m_inside{0};
};

} // namespace lmms

#endif // LMMS_MIDI_INPUT_GATE_H
