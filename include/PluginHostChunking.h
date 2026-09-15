/*
 * PluginHostChunking.h - the chunking contract the plugin hosts keep, and the
 *                        counters the control surface reads it from
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

#ifndef LMMS_PLUGIN_HOST_CHUNKING_H
#define LMMS_PLUGIN_HOST_CHUNKING_H

#include "lmms_export.h"

#include <atomic>
#include <cstdint>

namespace lmms
{

class ControlRegistry;

namespace control
{

/*! What one host path's audio path has been asked for, and how it answered.
 *
 * WHY THIS IS IN THE CORE. The two host paths (plugins/Vst3Effect/Vst3Host.cpp
 * and plugins/ClapEffect/ClapHost.cpp) are in plugin modules; a control command
 * is in the core. The counters therefore live here, in the core, and the host
 * paths write into them - which is also what makes them observable from
 * `--control-socket` (the `plugin.host_chunking` command) rather than only from
 * a test that compiles the host sources.
 *
 * `requests` counts process() calls that reached a loaded plug-in; `chunks`
 * counts the plug-in's own process() calls they were turned into. The two being
 * equal means every request fitted in the prepared block; `chunks` larger means
 * the host split the request instead of truncating it or over-running.
 */
struct PluginHostChunkingStats
{
	//! process() calls that reached the plug-in
	std::uint64_t requests = 0;
	//! frames those calls asked for, in total
	std::uint64_t frames = 0;
	//! plug-in process() calls they were turned into (>= requests)
	std::uint64_t chunks = 0;
	//! requests that needed more than one chunk, i.e. asked for more frames
	//! than the block size the plug-in was prepared with
	std::uint64_t multiChunkRequests = 0;
	//! frames delivered beyond that block size, i.e. frames a truncating host
	//! would have dropped and an unclamped host would have over-run the
	//! scratch buffers with
	std::uint64_t framesBeyondPreparedBlock = 0;
	//! the largest single request seen
	std::int64_t maxRequestedFrames = 0;
	//! the block size the most recent request was prepared with
	std::int64_t preparedBlock = 0;
};

/*! One host path's counters.
 *
 * Audio-thread side: recordRequest()/recordChunk() - relaxed atomics, no
 * allocation, no lock, no unbounded growth. Control-thread side: read().
 */
class LMMS_EXPORT PluginHostChunkingCounters
{
public:
	//! One process() request, on the audio thread.
	void recordRequest(std::int64_t frames, std::int64_t preparedBlock);
	//! One plug-in process() call, on the audio thread.
	void recordChunk();

	//! A snapshot. Any thread.
	PluginHostChunkingStats read() const;

private:
	std::atomic<std::uint64_t> m_requests{0};
	std::atomic<std::uint64_t> m_frames{0};
	std::atomic<std::uint64_t> m_chunks{0};
	std::atomic<std::uint64_t> m_multiChunkRequests{0};
	std::atomic<std::uint64_t> m_framesBeyondPreparedBlock{0};
	std::atomic<std::int64_t> m_maxRequestedFrames{0};
	std::atomic<std::int64_t> m_preparedBlock{0};
};

//! The VST3 host path's counters (plugins/Vst3Effect/Vst3Host.cpp).
LMMS_EXPORT PluginHostChunkingCounters& vst3HostChunkingCounters();
//! The CLAP host path's counters (plugins/ClapEffect/ClapHost.cpp).
LMMS_EXPORT PluginHostChunkingCounters& clapHostChunkingCounters();

} // namespace control

} // namespace lmms

#endif // LMMS_PLUGIN_HOST_CHUNKING_H
