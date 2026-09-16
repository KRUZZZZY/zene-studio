/*
 * ClapHost.cpp - in-process CLAP host for LMMS
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

//! The note path of the CLAP host (feature row 79): the discovery half
//! (clap.note-ports, read while the plug-in is deactivated), the bounded queue
//! the MIDI route pushes into, and the drain that turns one block's worth of
//! queued notes into the plug-in's input event list. Split out of ClapHost.cpp
//! - the note path is the part that grew past the file-length ratchet - so
//! ClapHost.cpp stays the host core; ClapHostInternals.h is the state the TUs
//! share and ClapHostParams.cpp is the other plug-in-facing surface.

#include "ClapHost.h"
#include "ClapHostInternals.h"

#include "PluginHostNotes.h"

#include <algorithm>
#include <cstdint>

#include <clap/clap.h>

namespace lmms::clap
{

auto HostedPlugin::noteInputPorts() const -> const std::vector<NotePortDescriptor>&
{
	return m_impl->notePorts;
}

auto HostedPlugin::acceptsNotes() const -> bool
{
	// No extension at all, or an extension that declares no input port: both
	// mean "this plug-in takes no notes", and both are refused the same way.
	return !m_impl->notePorts.empty();
}

auto HostedPlugin::preferredNotePort() const -> std::uint32_t
{
	return m_impl->notePortIndex;
}

auto HostedPlugin::noteCounters() const -> NoteCounters
{
	NoteCounters counters;
	counters.pushed = m_impl->notesPushed.load(std::memory_order_relaxed);
	counters.dropped = m_impl->notesDropped.load(std::memory_order_relaxed);
	counters.delivered = m_impl->notesDelivered.load(std::memory_order_relaxed);
	counters.ports = static_cast<std::uint32_t>(m_impl->notePorts.size());
	counters.played = m_impl->notesPlayed.load(std::memory_order_relaxed);
	return counters;
}

void HostedPlugin::setNoteOn(std::uint8_t channel, std::int16_t key, double velocity,
	std::int32_t frameOffset)
{
	// Audio thread. The clamp is CLAP's own 0..1 velocity scale, not MIDI's
	// 0..127: clap_event_note_t::velocity is a double in [0, 1].
	NoteEventIn event;
	event.type = CLAP_EVENT_NOTE_ON;
	event.channel = static_cast<std::uint8_t>(std::min<int>(channel, 15));
	event.key = static_cast<std::int16_t>(std::clamp<int>(key, 0, 127));
	event.velocity = static_cast<std::int16_t>(std::clamp(velocity, 0.0, 1.0) * 127.0);
	event.frameOffset = frameOffset;
	pushNote(event);
}

void HostedPlugin::setNoteOff(std::uint8_t channel, std::int16_t key, std::int32_t frameOffset)
{
	NoteEventIn event;
	event.type = CLAP_EVENT_NOTE_OFF;
	event.channel = static_cast<std::uint8_t>(std::min<int>(channel, 15));
	event.key = static_cast<std::int16_t>(std::clamp<int>(key, 0, 127));
	event.velocity = 0;
	event.frameOffset = frameOffset;
	pushNote(event);
}

void HostedPlugin::setNoteChoke(std::uint8_t channel, std::int16_t key, std::int32_t frameOffset)
{
	NoteEventIn event;
	event.type = CLAP_EVENT_NOTE_CHOKE;
	event.channel = static_cast<std::uint8_t>(std::min<int>(channel, 15));
	event.key = static_cast<std::int16_t>(std::clamp<int>(key, 0, 127));
	event.velocity = 0;
	event.frameOffset = frameOffset;
	pushNote(event);
}

void HostedPlugin::pushNote(const NoteEventIn& event)
{
	auto& impl = *m_impl;
	if (!acceptsNotes())
	{
		// Not a queue overflow: the plug-in has no note input port, so the
		// event has nowhere to go. Counted as a drop so `plugin.host_notes`
		// shows it rather than hiding it.
		impl.notesDropped.fetch_add(1, std::memory_order_relaxed);
		control::clapHostNoteCounters().recordDrop();
		return;
	}
	impl.notesPushed.fetch_add(1, std::memory_order_relaxed);
	control::clapHostNoteCounters().recordPush();
	if (!impl.noteQueue.push(event))
	{
		impl.notesDropped.fetch_add(1, std::memory_order_relaxed);
		control::clapHostNoteCounters().recordDrop();
	}
}

void HostedPlugin::Impl::scanNotePorts()
{
	// Main thread, from load(), with the plug-in deactivated - the state
	// clap.note-ports' contract requires for a port scan. A plug-in WITHOUT
	// the extension is not an error: a host must not require note ports of a
	// plug-in that takes no notes (every effect), so nothing here can fail
	// the load. INPUT ports only: this host delivers notes and has no use for
	// a note output port in this release.
	if (!notePortsExt) { return; }

	// The per-instance counters describe THIS instance, so a reload starts
	// them over.
	notesPushed.store(0, std::memory_order_relaxed);
	notesDropped.store(0, std::memory_order_relaxed);
	notesDelivered.store(0, std::memory_order_relaxed);
	notesPlayed.store(0, std::memory_order_relaxed);

	const auto portCount = notePortsExt->count(plugin, true);
	notePorts.reserve(portCount);
	for (std::uint32_t i = 0; i < portCount; ++i)
	{
		clap_note_port_info_t info{};
		// The index is the PLUG-IN's, and it is what clap_event_note_t's
		// port_index carries, so this is the number the host must send back
		// - never a re-numbered one.
		if (!notePortsExt->get(plugin, i, true, &info)) { continue; }
		NotePortDescriptor port;
		port.id = info.id;
		port.name = QString::fromUtf8(info.name);
		port.supportedDialects = info.supported_dialects;
		port.preferredDialect = info.preferred_dialect;
		notePorts.push_back(port);
	}
	// Deliver to a port that speaks the CLAP dialect when one declares it;
	// otherwise the first port (a MIDI-dialect port still receives
	// clap_event_note events - the dialect says what the plug-in can
	// additionally accept, not what the host must send).
	for (std::uint32_t i = 0; i < notePorts.size(); ++i)
	{
		if ((notePorts[i].preferredDialect & NoteDialectClap) != 0 ||
			(notePorts[i].supportedDialects & NoteDialectClap) != 0)
		{
			notePortIndex = i;
			break;
		}
	}
	if (!notePorts.empty()) { notePorts[notePortIndex].preferred = true; }

	// The process-wide half of the same facts, for `plugin.host_notes`: the
	// note ports and the audio layout of the plug-in just loaded.
	control::clapHostNoteCounters().recordLoad(static_cast<std::uint32_t>(notePorts.size()),
		notePortIndex,
		notePorts.empty() ? 0u : notePorts[notePortIndex].supportedDialects,
		static_cast<std::uint32_t>(std::max(0, layout.inputs)),
		static_cast<std::uint32_t>(std::max(0, layout.outputs)));
}

void HostedPlugin::Impl::drainNoteQueue()
{
	// Real-time safe: bounded pops from a fixed ring into an array sized in
	// prepare(). A no-op for a plug-in with no note input port and a no-op
	// for a block with no notes - no allocation on either path.
	std::uint32_t count = 0;
	NoteEventIn queued;
	while (count < static_cast<std::uint32_t>(kMaxNoteEventsPerBlock) &&
		noteQueue.pop(&queued))
	{
		auto& event = noteEvents[count];
		event = {};
		event.header.size = sizeof(clap_event_note_t);
		// The frame offset is where LMMS' own MIDI route put the event, in
		// frames from the start of this request; the plug-in applies it at
		// that frame of the chunk it is handed.
		event.header.time = static_cast<std::uint32_t>(std::max(queued.frameOffset, 0));
		event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
		event.header.type = queued.type;
		event.header.flags = 0;
		event.note_id = -1; // this host carries no per-note id
		// The plug-in's OWN port index: clap_event_note_t::port_index is an
		// index into the plug-in's note input ports, not a port id.
		event.port_index = static_cast<std::int16_t>(notePortIndex);
		event.channel = static_cast<std::int16_t>(queued.channel);
		event.key = queued.key;
		event.velocity = queued.type == CLAP_EVENT_NOTE_ON
			? static_cast<double>(queued.velocity) / 127.0
			: 0.0;
		++count;
	}
	eventState.noteCount = count;
	notesDelivered.fetch_add(count, std::memory_order_relaxed);
	std::uint32_t played = 0;
	if (count > 0)
	{
		// One "played" count per note-ON delivered, so a caller can tell
		// notes that started voices from note-offs and chokes.
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (noteEvents[i].header.type == CLAP_EVENT_NOTE_ON) { ++played; }
		}
		notesPlayed.fetch_add(played, std::memory_order_relaxed);
	}
	control::clapHostNoteCounters().recordDelivered(count, played);
}

} // namespace lmms::clap
