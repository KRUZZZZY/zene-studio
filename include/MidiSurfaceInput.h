/*
 * MidiSurfaceInput.h - a MIDI controller's input, decoded on the MIDI thread and applied on the UI thread
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
#ifndef LMMS_MIDI_SURFACE_INPUT_H
#define LMMS_MIDI_SURFACE_INPUT_H

#include <atomic>
#include <memory>

#include <QString>
#include <QTimer>

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"
#include "MpscQueue.h"

namespace lmms
{

/*! 0.5.0 L10 / L02: what the remote-control pages and the launcher's MIDI bindings share - a duplex
 *  MIDI port subscribed to a controller, events decoded on the MIDI client's thread into @a Item
 *  (nothing allocated or locked there; a realtime callback on some backends) and handed through a
 *  lock-free queue that a UI-thread timer drains, where the registry and the models live. The
 *  derived surface says what an event means (decode) and what it does (apply), and calls close() in
 *  its destructor. */
template <typename Item>
class MidiSurfaceInput : public MidiEventProcessor
{
public:
	MidiSurfaceInput() { QObject::connect(&m_timer, &QTimer::timeout, [this] { drain(); }); }
	~MidiSurfaceInput() override = default;

	//! Open the port named @a name and subscribe it to @a input (and @a output for feedback).
	void open(const QString& name, const QString& input, const QString& output)
	{
		m_input = input;
		m_output = output;
		// No engine (before startup, after teardown): no MIDI client to open on, so the surface stays closed.
		if (Engine::audioEngine() == nullptr) { return; }
		m_port = std::make_unique<MidiPort>(name, Engine::audioEngine()->midiClient(), this, nullptr, MidiPort::Mode::Duplex);
		if (!input.isEmpty()) { m_port->subscribeReadablePort(input); }
		if (!output.isEmpty()) { m_port->subscribeWritablePort(output); }
		m_timer.start(DrainMs);
	}

	void processInEvent(const MidiEvent& event, const TimePos& = TimePos(), f_cnt_t = 0) override
	{
		Item item;
		if (!decode(event, &item)) { return; }
		m_eventsIn.fetch_add(1, std::memory_order_relaxed);
		(void)m_inbox.push(item);
	}
	void processOutEvent(const MidiEvent&, const TimePos& = TimePos(), f_cnt_t = 0) override {}

	//! Release the port. A derived surface calls it first in its destructor: the MIDI thread must not
	//! reach decode() once the derived part is gone.
	void close()
	{
		m_timer.stop();
		m_port.reset();
	}

	bool isOpen() const { return m_port != nullptr; }
	//! Whether the input names a port the MIDI client has (review L10-L03-L02-1 #7: an empty or unknown
	//! name opens the surface but nothing will ever arrive - said so, not hidden).
	bool subscribed() const
	{
		// An open surface can outlive the engine (Engine::destroy() does not close it): then there is no client.
		MidiClient* client = Engine::audioEngine() != nullptr ? Engine::audioEngine()->midiClient() : nullptr;
		return isOpen() && !m_input.isEmpty() && client != nullptr && client->readablePorts().contains(m_input);
	}
	const QString& input() const { return m_input; }
	const QString& output() const { return m_output; }
	int eventsIn() const { return m_eventsIn.load(std::memory_order_relaxed); }

protected:
	//! MIDI thread: what @a event means, or false to ignore it.
	virtual bool decode(const MidiEvent& event, Item* out) = 0;
	//! UI thread: do it.
	virtual void apply(const Item& item) = 0;
	//! Send @a event to the controller (feedback), when an output is subscribed.
	void send(const MidiEvent& event)
	{
		if (m_port != nullptr && !m_output.isEmpty()) { m_port->processOutEvent(event); }
	}

private:
	static constexpr int DrainMs = 15;

	void drain()
	{
		Item item;
		while (m_inbox.pop(&item)) { apply(item); }
	}

	std::unique_ptr<MidiPort> m_port;
	QString m_input;
	QString m_output;
	QTimer m_timer;
	MpscQueue<Item, 256> m_inbox;
	std::atomic<int> m_eventsIn{0};
};

} // namespace lmms

#endif // LMMS_MIDI_SURFACE_INPUT_H
