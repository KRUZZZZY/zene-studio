/*
 * ScriptConsole.h - script console output for the LMMS/Zene Lua API
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

#ifndef LMMS_SCRIPT_CONSOLE_H
#define LMMS_SCRIPT_CONSOLE_H

#include <QString>

namespace lmms
{

/*! \brief The script console: script-visible output on the DAW's logging path.
 *
 *  Before this existed, `print()` and `lmms.log()` only appended to
 *  ScriptEngine's in-memory capture buffer (`m_logMessages`), which nothing
 *  renders: a script that printed for debugging was silent unless a host
 *  happened to call takeLogMessages() afterwards. The console streams each
 *  captured line onto Qt's message log - the same path every other qInfo() /
 *  qWarning() in the product takes, so it lands in the terminal (or in whatever
 *  handler a build installs) while the script is still running.
 *
 *  Threading: streamLine() runs on the script worker thread (`print` and
 *  `lmms.log()` calls) and on the apply side (the MIDI-out mirror in
 *  ScriptEngine::applyCommand). It takes no lock the audio thread can observe
 *  and is never called from an audio-thread path. setEnabled() is a diagnostic
 *  switch and is typically called once, before a run.
 */
namespace ScriptConsole
{

//! Turn streaming on or off. On by default; only a host that already prints the
//! captured lines itself (the headless `--run-script` path, which owns stdout)
//! should turn it off, or every line is emitted twice.
void setEnabled(bool enabled);
bool enabled();

/*! Write one already-captured line to the console.
 *
 *  The line is prefixed with "lua:" so DAW diagnostics stay distinguishable
 *  from script output. A no-op while disabled, and for an empty line.
 */
void streamLine(const QString& line);

} // namespace ScriptConsole

} // namespace lmms

#endif // LMMS_SCRIPT_CONSOLE_H
