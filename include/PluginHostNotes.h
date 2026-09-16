/*
 * PluginHostNotes.h - the note path a plugin host keeps, and the counters the
 *                     control surface reads it from (feature row 79)
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

#ifndef LMMS_PLUGIN_HOST_NOTES_H
#define LMMS_PLUGIN_HOST_NOTES_H

#include "lmms_export.h"

#include <atomic>
#include <cstdint>

namespace lmms
{

class ControlRegistry;

namespace control
{

/*! What the CLAP host path's note route has been told, and what it did with
 * it.
 *
 * WHY THIS IS IN THE CORE. The host (plugins/ClapEffect/ClapHost.cpp, compiled
 * into both the effect and the instrument module) is a plugin module; a control
 * command is in the core. The counters therefore live here, in the core, and
 * the host writes into them - which is what makes the note path observable from
 * `--control-socket` (the `plugin.host_notes` command) rather than only from a
 * test that compiles the host sources. Same reason, same shape, as
 * include/PluginHostChunking.h (CODE-4).
 *
 * The port fields describe the CLAP plug-in that was loaded LAST: a plug-in's
 * note ports and audio ports are per-instance facts, and this is the
 * process-wide view of them, exactly as the chunking counters' `preparedBlock`
 * is. The four event counters are process-lifetime.
 */
struct PluginHostNoteStats
{
	//! note INPUT ports (clap.note-ports) the last loaded plug-in declared
	std::uint32_t ports = 0;
	//! the plug-in's own index of the port notes are delivered to
	std::uint32_t preferredPort = 0;
	//! the dialects that port declared (clap_note_dialect bitfield)
	std::uint32_t dialects = 0;
	//! the last loaded plug-in's own audio port channel counts. A generator
	//! declares no input at all, which is what `inputs == 0` reports.
	std::uint32_t audioInputs = 0;
	std::uint32_t audioOutputs = 0;
	//! how many CLAP plug-ins have been loaded in this process
	std::uint64_t loads = 0;
	//! note events handed to a host's queue
	std::uint64_t pushed = 0;
	//! note events a full queue - or a plug-in with no note input port -
	//! refused
	std::uint64_t dropped = 0;
	//! note events put into a plug-in's input event list
	std::uint64_t delivered = 0;
	//! note-ON events among those: the ones that start a voice
	std::uint64_t played = 0;
};

/*! The CLAP host path's note counters.
 *
 * Main-thread side: recordLoad(), once per load(). Audio-thread side:
 * recordPush()/recordDrop()/recordDelivered()/recordPlayed() - relaxed
 * atomics, no allocation, no lock, no unbounded growth. Control-thread side:
 * read().
 */
class LMMS_EXPORT PluginHostNoteCounters
{
public:
	//! The ports and audio layout of the plug-in just loaded, main thread.
	void recordLoad(std::uint32_t ports, std::uint32_t preferredPort,
		std::uint32_t dialects, std::uint32_t audioInputs, std::uint32_t audioOutputs);
	//! One event handed to the queue, audio thread.
	void recordPush();
	//! One event the queue (or the missing note port) refused, audio thread.
	void recordDrop();
	//! Events put into the plug-in's input list, audio thread.
	void recordDelivered(std::uint32_t count, std::uint32_t played);

	//! A snapshot. Any thread.
	PluginHostNoteStats read() const;

private:
	std::atomic<std::uint32_t> m_ports{0};
	std::atomic<std::uint32_t> m_preferredPort{0};
	std::atomic<std::uint32_t> m_dialects{0};
	std::atomic<std::uint32_t> m_audioInputs{0};
	std::atomic<std::uint32_t> m_audioOutputs{0};
	std::atomic<std::uint64_t> m_loads{0};
	std::atomic<std::uint64_t> m_pushed{0};
	std::atomic<std::uint64_t> m_dropped{0};
	std::atomic<std::uint64_t> m_delivered{0};
	std::atomic<std::uint64_t> m_played{0};
};

//! The CLAP host path's note counters (plugins/ClapEffect/ClapHost.cpp).
LMMS_EXPORT PluginHostNoteCounters& clapHostNoteCounters();

} // namespace control

} // namespace lmms

#endif // LMMS_PLUGIN_HOST_NOTES_H
