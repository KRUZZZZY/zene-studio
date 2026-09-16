/*
 * PluginHostNotes.cpp - the CLAP host's note-path counters (feature row 79)
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
 *
 * WHY THIS EXISTS. Feature row 79 gives the CLAP host a note input path
 * (clap.note-ports -> clap_event_note in the plug-in's input event list) and
 * the CLAP instrument module an audio-output configuration read from the
 * plug-in's own clap.audio-ports. Both halves are only real if they can be
 * OBSERVED through the control socket, so the host writes what it discovered
 * and what it delivered into these counters and `plugin.host_notes`
 * (src/core/ControlCommandsHostNotes.cpp) reports them on a running instance.
 *
 * This translation unit has no engine dependency on purpose: the host tests
 * (ClapHostTest) compile plugins/ClapEffect/ClapHost.cpp and this file, and
 * nothing else of the core - the same shape as src/core/PluginHostChunking.cpp.
 */

#include "PluginHostNotes.h"

namespace lmms
{

namespace control
{

namespace
{

//! Process-wide, and never destroyed before the last host is: a plugin module
//! may outlive the engine. A function-local static, so the host path, the
//! command and the tests all see the same object.
PluginHostNoteCounters s_clapNoteCounters;

} // namespace

void PluginHostNoteCounters::recordLoad(std::uint32_t ports, std::uint32_t preferredPort,
	std::uint32_t dialects, std::uint32_t audioInputs, std::uint32_t audioOutputs)
{
	m_ports.store(ports, std::memory_order_relaxed);
	m_preferredPort.store(preferredPort, std::memory_order_relaxed);
	m_dialects.store(dialects, std::memory_order_relaxed);
	m_audioInputs.store(audioInputs, std::memory_order_relaxed);
	m_audioOutputs.store(audioOutputs, std::memory_order_relaxed);
	m_loads.fetch_add(1, std::memory_order_relaxed);
}

void PluginHostNoteCounters::recordPush()
{
	m_pushed.fetch_add(1, std::memory_order_relaxed);
}

void PluginHostNoteCounters::recordDrop()
{
	m_dropped.fetch_add(1, std::memory_order_relaxed);
}

void PluginHostNoteCounters::recordDelivered(std::uint32_t count, std::uint32_t played)
{
	m_delivered.fetch_add(count, std::memory_order_relaxed);
	m_played.fetch_add(played, std::memory_order_relaxed);
}

PluginHostNoteStats PluginHostNoteCounters::read() const
{
	PluginHostNoteStats stats;
	stats.ports = m_ports.load(std::memory_order_relaxed);
	stats.preferredPort = m_preferredPort.load(std::memory_order_relaxed);
	stats.dialects = m_dialects.load(std::memory_order_relaxed);
	stats.audioInputs = m_audioInputs.load(std::memory_order_relaxed);
	stats.audioOutputs = m_audioOutputs.load(std::memory_order_relaxed);
	stats.loads = m_loads.load(std::memory_order_relaxed);
	stats.pushed = m_pushed.load(std::memory_order_relaxed);
	stats.dropped = m_dropped.load(std::memory_order_relaxed);
	stats.delivered = m_delivered.load(std::memory_order_relaxed);
	stats.played = m_played.load(std::memory_order_relaxed);
	return stats;
}

PluginHostNoteCounters& clapHostNoteCounters()
{
	return s_clapNoteCounters;
}

} // namespace control

} // namespace lmms
