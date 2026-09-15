/*
 * ScriptMemoryBudget.h - the Lua allocator behind the script memory budget
 *                        (CODE-6).
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
 * WHY THIS IS ITS OWN FILE. ScriptEngine::ScriptWorker opens one Lua state per
 * run with lua_newstate() and this allocator (ScriptEngine.cpp), and the budget
 * it enforces is the one beside the instruction budget: the count hook bounds
 * how LONG a script may run, this bounds how MUCH it may hold. The machinery is
 * here rather than in ScriptEngine.cpp because ScriptEngine.cpp is a file the
 * 500-line ratchet already grandfathers at 880 lines - the budget's own code
 * must not be the reason that number moves (tests/file-length-gate.sh: a
 * grandfathered file may not grow, and a new file may not open over the limit).
 * The call site stays in ScriptEngine.cpp, where the state is opened.
 */

#ifndef LMMS_SCRIPT_MEMORY_BUDGET_H
#define LMMS_SCRIPT_MEMORY_BUDGET_H

#include <QString>

#include <cstdlib>
#include <cstddef>

namespace lmms
{

/*! \brief The allocator's bookkeeping for one Lua state (CODE-6).
 *
 * One instance lives on the worker thread for the duration of one run and is
 * handed to lua_newstate() as the allocator's user data, so the count of live
 * bytes and the cap they are measured against belong to the state that owns
 * them: no global, no lock, and nothing a script can read, reset or widen.
 * `budget == 0` disables the cap (reachable from C++ only: the control
 * surface's script.set_memory_budget refuses anything outside the range
 * ScriptEngine declares).
 */
struct ScriptMemoryState
{
	quint64 live = 0;    //!< bytes the Lua state currently holds
	quint64 peak = 0;    //!< high-water mark of live
	quint64 budget = 0;  //!< the cap, in bytes; 0 disables it (C++ only)
	quint64 refused = 0; //!< allocations refused because they would cross it

	/*! \brief Lua's allocator: counts live bytes and REFUSES what would cross
	 *  the budget.
	 *
	 * luaL_newstate() would install a bare realloc() with no accounting at all;
	 * this is the allocator lua_newstate() is given instead. Lua's contract for
	 * an allocator is realloc()'s - return NULL and the caller sees a memory
	 * error - which is exactly the behaviour a budget needs: a run that asks for
	 * more than the budget becomes a LUA_ERRMEM the run reports, instead of an
	 * allocation that keeps growing until the OOM killer takes the process.
	 *
	 * \a osize is only a real size when \a pointer is non-NULL: with a NULL
	 * pointer Lua passes the TYPE of the object being allocated (LUA_TSTRING and
	 * friends), which must not be subtracted from the live count.
	 */
	static void* allocate(void* userData, void* pointer, std::size_t osize, std::size_t nsize)
	{
		ScriptMemoryState* memory = static_cast<ScriptMemoryState*>(userData);
		if (memory == nullptr)
		{
			return std::realloc(pointer, nsize);
		}

		const std::size_t held = pointer != nullptr ? osize : 0;
		if (nsize == 0)
		{
			// Lua freeing a block. Nothing is refused here: a budget that could
			// not shrink would leak the very memory it bounds.
			if (pointer != nullptr)
			{
				memory->live = memory->live > held ? memory->live - held : 0;
				std::free(pointer);
			}
			return nullptr;
		}

		if (nsize > held && memory->budget != 0 && memory->live + (nsize - held) > memory->budget)
		{
			// Refused. The block Lua already holds stays valid and stays
			// counted, so the failure leaves the state consistent for the
			// error path that unwinds it.
			++memory->refused;
			return nullptr;
		}

		void* block = std::realloc(pointer, nsize);
		if (block == nullptr)
		{
			// A real allocation failure rather than a budget refusal. Counted
			// with them: from the run's side both are "the allocator said no".
			++memory->refused;
			return nullptr;
		}
		memory->live = memory->live - held + nsize;
		if (memory->live > memory->peak)
		{
			memory->peak = memory->live;
		}
		return block;
	}

	//! Why a run failed with LUA_ERRMEM, in words: which bound was hit, what was
	//! measured against it, and how many allocations were refused. Lua's own
	//! text ("not enough memory") says none of that, and the instruction
	//! budget's message is this tree's precedent for naming the limit.
	QString budgetExceededMessage(const QString& luaMessage) const
	{
		return QStringLiteral("memory budget exceeded (%1 bytes live, peak %2, budget %3, "
			"%4 refused allocation(s)) - script aborted: %5")
			.arg(live).arg(peak).arg(budget).arg(refused).arg(luaMessage);
	}

	//! Why a state could not be opened at all: the budget, named, because
	//! "could not create a Lua state" alone cannot tell a client whether the
	//! machine refused the allocation or the budget did.
	QString stateRefusedMessage() const
	{
		return QStringLiteral("could not create a Lua state within the %1-byte memory budget "
			"(the budget is set with script.set_memory_budget)").arg(budget);
	}
};

} // namespace lmms

#endif // LMMS_SCRIPT_MEMORY_BUDGET_H
