/*
 * ControlReversibilityTableTask.cpp - the task.* group's rows of the SPEC A16 table
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
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

using RC = ReversibilityClass;

//! A literal row: R(id, class, reversible, reason, mechanism, fallback).
#define R(id, cls, rev, reason, mechanism, fallback) \
	{ id, cls, reason, mechanism, fallback, rev, nullptr }

// 0.5.0 UX decision (theme 24): the background jobs' list (TaskCenter.h) - session and interface state.
const ReversibilityRow kTaskRows[] = {
	R("task.list", RC::NotMutating, false, "it lists the session's background jobs. Nothing is written", "no write", ""),
	R("task.cancel", RC::NotMutating, false, "it stops a background job, as render.queue_cancel and stem.job_cancel do: session "
		"state - the project is not touched and the job removes what it had written", "no write", ""),
	R("task.clear_finished", RC::NotMutating, false, "it forgets the ended jobs in the list: session state", "no write", ""),
	R("task.show", RC::NotMutating, false, "it shows or hides the task list: interface state", "no write", ""),
};

#undef R

constexpr int kTaskRowCount = static_cast<int>(sizeof(kTaskRows) / sizeof(kTaskRows[0]));

} // namespace

const ReversibilityRow* reversibilityTaskRowTable(int* rowCount)
{
	if (rowCount != nullptr) { *rowCount = kTaskRowCount; }
	return kTaskRows;
}

} // namespace control
} // namespace lmms
