/*
 * EngineServe.h - the engine as a process of its own (`zene --engine`)
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
 */

#ifndef LMMS_ENGINE_SERVE_H
#define LMMS_ENGINE_SERVE_H

#include <QString>

#include "lmms_export.h"

// Serve mode (Q050-517, Rune engine item E1; docs/ENGINE-SERVE.md).
//
// `zene --engine --control-socket <path> [project]` runs the engine with the control server and an
// event loop and NOTHING else: a QCoreApplication, no gui::GuiApplication, no main window, no widget.
// A front end in another process (Rune, an agent, a script) is then the only interface there is.
//
// What it is NOT: a render (`render`, `exportstems`, `master`) or a script run (`--run-script`). Those
// are core-only too, but they open the engine render-only, do one job and exit. Serve mode opens the
// configured audio and MIDI devices, as the application does, and stays until it is told to quit
// (control.quit, SIGTERM or SIGINT).
//
// lmms::isEngineServe() (UnattendedRun.h) is the predicate the rest of the process reads; this
// header is what main() calls.

namespace lmms::engineserve
{

struct StartOptions
{
	//! The --control-socket path. Serve mode refuses to start without one: with no interface, the
	//! control surface is the only way to reach the process, and an engine nobody can reach or stop
	//! is a leak, not a service.
	QString controlSocket;
	//! The project named on the command line, or empty for a new project.
	QString fileToLoad;
	//! The file given to --import, or empty.
	QString fileToImport;
};

//! Bring the engine up for serving: the working directory, Engine::init(false), the initial project,
//! the crash/safe-start project hints, the quit-time save and the termination signals. Runs on the
//! main thread before the event loop; main() then marks the registry ready and calls exec().
//! Returns false with a message when the process must not continue (the caller owns the exit).
LMMS_EXPORT bool start(const StartOptions& options, QString* error);

//! The refusal start() gives for these options, or an empty string. A pure function so the rule has
//! a unit test that needs no engine.
LMMS_EXPORT QString startRefusal(const StartOptions& options);

//! What a clean quit owes the project: true when control.quit asked for a save and there is a file to
//! save to. Pure, for the same reason.
LMMS_EXPORT bool quitShouldSave(bool quitPending, bool saveAnswer, bool hasProjectFile);

} // namespace lmms::engineserve

#endif // LMMS_ENGINE_SERVE_H
