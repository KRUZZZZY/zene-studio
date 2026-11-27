/*
 * ControlRenderStaging.h - how a render the control surface runs is staged and
 *                          published (cluster 5735).
 *
 * The render child never writes the caller's path: it writes a staged file of its
 * own beside the file the request resolves to, and that file is moved over the
 * destination only when the child finished and succeeded. These are the pieces
 * runCliRender (src/core/ControlCommandsProject.cpp) is built from.
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

#ifndef LMMS_CONTROL_RENDER_STAGING_H
#define LMMS_CONTROL_RENDER_STAGING_H

#include <QString>

class QProcess;

namespace lmms
{

//! The most symlinks followed from a destination: Linux's own MAXSYMLINKS. Longer is a loop.
constexpr int MaxRenderSymlinkHops = 40;

/*! The path the render child writes: a name of the render's own, beside the file it publishes.
 *
 *  The child never writes the caller's path. A child that is killed mid-write (a
 *  timeout, a crash) or that exits non-zero then leaves its partial file HERE, where
 *  this file removes it, instead of replacing what a previous render put at the
 *  requested path with a truncated file (cluster 5735). Beside the published file
 *  (resolveRenderTarget) rather than in the temp directory, so publishing is one rename
 *  on one filesystem, and under the same `.zene-render-` prefix RenderQueue stages its
 *  own render jobs with. The extension is the format's own: the CLI renderer names a
 *  single-file render `<base name of -o> + <that format's extension>`, so the name it
 *  writes is exactly the one it was given.
 */
QString stagedRenderPath(const QString& target, const QString& formatName);

/*! The file a render to `out` is published on: every symlink in the chain at `out` followed.
 *
 *  A write through `out` lands on the chain's last file, so that is the file replaced;
 *  each link stays the link it was. Relative texts resolve against the folder of the
 *  link that holds them, one hop at a time. The last file may not exist yet. False
 *  when the chain is longer than MaxRenderSymlinkHops or a link cannot be read.
 */
bool resolveRenderTarget(const QString& out, QString* target);

/*! How long a render child may run: ten minutes.
 *
 *  ZENE_CONTROL_RENDER_TIMEOUT_MS, read only when it is set to a positive number,
 *  replaces it: the seam tests/control-render-output-publish.py drives the timeout
 *  path through, which no render finishes inside a millisecond of.
 */
int renderTimeoutMs();

/*! The end of a render the child ran: published at `out` only when the child succeeded.
 *
 *  Returns whether the child finished, as runCliRender does.
 */
bool finishCliRender(QProcess& renderer, bool finished, const QString& staged,
	const QString& target, const QString& out, int* exitCode, QString* diagnostics);

} // namespace lmms

#endif // LMMS_CONTROL_RENDER_STAGING_H
