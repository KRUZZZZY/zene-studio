/*
 * PluginHostChunking.cpp - the plugin hosts' chunking counters (CODE-4)
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
 * WHY THIS EXISTS. CODE-4 made both plugin host paths process exactly the
 * frames they are asked for, in chunks of at most the prepared block, with an
 * explicit over-run/tail rule (the rule itself is documented on
 * HostedPlugin::process() in plugins/Vst3Effect/Vst3Host.h and
 * plugins/ClapEffect/ClapHost.h). These counters are the observable half: the
 * hosts increment them on the audio path, and `plugin.host_chunking`
 * (src/core/ControlCommandsHostChunking.cpp) reports them over the control
 * socket, so the property "a request larger than the prepared block is chunked,
 * not truncated and not over-run" can be checked on a running instance and not
 * only in a unit test.
 *
 * This translation unit has no engine dependency on purpose: the host tests
 * (Vst3HostTest, ClapHostTest, Vst3ChunkProbeTest, Vst3InstrumentTest) compile
 * the host sources and this file, and nothing else of the core.
 */

#include "PluginHostChunking.h"

namespace lmms
{

namespace control
{

namespace
{

//! Process-wide, and never destroyed before the last host is: a plugin module
//! may outlive the engine. Function-local statics, so the two host paths, the
//! command and the tests all see the same objects.
PluginHostChunkingCounters s_vst3Counters;
PluginHostChunkingCounters s_clapCounters;

} // namespace

void PluginHostChunkingCounters::recordRequest(std::int64_t frames, std::int64_t preparedBlock)
{
	m_requests.fetch_add(1, std::memory_order_relaxed);
	m_frames.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
	m_preparedBlock.store(preparedBlock, std::memory_order_relaxed);
	std::int64_t previous = m_maxRequestedFrames.load(std::memory_order_relaxed);
	while (frames > previous &&
		!m_maxRequestedFrames.compare_exchange_weak(previous, frames, std::memory_order_relaxed))
	{
	}
	if (preparedBlock > 0 && frames > preparedBlock)
	{
		m_multiChunkRequests.fetch_add(1, std::memory_order_relaxed);
		m_framesBeyondPreparedBlock.fetch_add(static_cast<std::uint64_t>(frames - preparedBlock),
			std::memory_order_relaxed);
	}
}

void PluginHostChunkingCounters::recordChunk()
{
	m_chunks.fetch_add(1, std::memory_order_relaxed);
}

PluginHostChunkingStats PluginHostChunkingCounters::read() const
{
	PluginHostChunkingStats stats;
	stats.requests = m_requests.load(std::memory_order_relaxed);
	stats.frames = m_frames.load(std::memory_order_relaxed);
	stats.chunks = m_chunks.load(std::memory_order_relaxed);
	stats.multiChunkRequests = m_multiChunkRequests.load(std::memory_order_relaxed);
	stats.framesBeyondPreparedBlock = m_framesBeyondPreparedBlock.load(std::memory_order_relaxed);
	stats.maxRequestedFrames = m_maxRequestedFrames.load(std::memory_order_relaxed);
	stats.preparedBlock = m_preparedBlock.load(std::memory_order_relaxed);
	return stats;
}

PluginHostChunkingCounters& vst3HostChunkingCounters()
{
	return s_vst3Counters;
}

PluginHostChunkingCounters& clapHostChunkingCounters()
{
	return s_clapCounters;
}

} // namespace control

} // namespace lmms
