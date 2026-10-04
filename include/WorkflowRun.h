/*
 * WorkflowRun.h - running a workflow's steps: where it stopped, what applied, and resuming
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

#ifndef LMMS_GUI_WORKFLOW_RUN_H
#define LMMS_GUI_WORKFLOW_RUN_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

/*! Design theme 19: a workflow's run (workflow.run, workflow.resume), split from ControlCommandsMacros.cpp.
 *  Each step runs on the next turn of the event loop as a top-level registry command of its own (so each
 *  records and undoes as itself), in order, stopping at the first that fails. The outcome says plainly what
 *  happened - which step failed and why, which steps applied (they stay applied: each undoes as its own
 *  record says), which were skipped and which did not run - as fields and as one sentence ('message'), and
 *  the interface shows it in a report that stays until closed (WorkflowRunReport). A run that stopped can be
 *  resumed: the failed step retried, or skipped and the rest run; the applied and skipped steps add up over
 *  the resumes of one run. */
namespace lmms::workflow
{

//! Whether a run is under way (UI thread only): a second is refused.
LMMS_EXPORT bool running();

//! Queue @a steps of workflow @a name from the first; workflow.run's reply ({name, queued, from, poll}).
LMMS_EXPORT ControlResult start(const QString& name, const QJsonArray& steps);

/*! Resume the last run, which stopped at a failing step, from that step or (@a skip) the one after it, with the
 *  steps @a library (the library in use) holds for it now. Refused when the last run did not stop at a failure,
 *  or another is running; NotFound when the workflow is no longer saved. */
LMMS_EXPORT ControlResult resume(bool skip, const QJsonObject& library);

/*! workflow.status's body: {state: idle | running | done}, and once done {name, total, from, ran, stopped_at
 *  (0-based, -1 when every step ran), results (each run step: step (1-based), command, name, ok, error),
 *  steps (every step: step, command, name), applied_steps, skipped_steps, not_run (1-based numbers), failed
 *  ({step, command, name, error, args}, when one failed), resumable, message}. */
LMMS_EXPORT QJsonObject lastRun();

} // namespace lmms::workflow

#endif // LMMS_GUI_WORKFLOW_RUN_H
