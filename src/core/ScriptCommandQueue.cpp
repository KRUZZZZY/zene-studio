/*
 * ScriptCommandQueue.cpp - the Lua script -> apply-side command queue
 *
 * Copyright (c) 2026 LMMS contributors
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

/*! \file
 * Split out of ScriptEngine.cpp, which is a grandfathered 910-line source under
 * the per-file length ratchet (tests/file-length-gate.sh): the queue is a
 * self-contained SPSC type and does not belong in the same translation unit as
 * the engine, the sandbox and the Lua run loop. No behaviour changes here.
 */

#include "ScriptEngine.h"

namespace lmms
{

ScriptCommandQueue::ScriptCommandQueue(std::size_t capacity) :
	m_buffer(capacity),
	m_reader(m_buffer)
{
}


bool ScriptCommandQueue::push(const ScriptCommand& command)
{
	if (m_buffer.write(&command, 1) == 1)
	{
		return true;
	}
	// Realtime violation (spec section 4): overflow never blocks the producer,
	// the command is dropped and counted.
	m_dropped.fetch_add(1, std::memory_order_relaxed);
	return false;
}


std::size_t ScriptCommandQueue::drain(const std::function<void(const ScriptCommand&)>& fn,
					std::size_t max)
{
	std::size_t applied = 0;
	while (applied < max)
	{
		auto sequence = m_reader.read_max(1);
		if (sequence.size() == 0)
		{
			break;
		}
		fn(sequence[0]);
		++applied;
	}
	return applied;
}


std::size_t ScriptCommandQueue::pending() const
{
	return m_reader.read_space();
}


std::uint64_t ScriptCommandQueue::dropped() const
{
	return m_dropped.load(std::memory_order_relaxed);
}

} // namespace lmms
