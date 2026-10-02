/*
 * ShellIcons.h - resolution-independent icons for the 0.5.0 shell, painted from paths
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

#ifndef LMMS_GUI_SHELL_ICONS_H
#define LMMS_GUI_SHELL_ICONS_H

#include <QIcon>
#include <QPainterPath>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::gui
{

//! The shell's icon set. Every glyph is a path in a 16 x 16 design grid, scaled at paint
//! time, so it is sharp at any size and any device-pixel ratio - no bitmap is shipped. The
//! glyphs are this project's own drawings (docs/adr/ADR-0001-UI-TOOLKIT.md: no third-party
//! artwork).
namespace shellicons
{

//! Every id icon() knows, for the test that paints each one.
LMMS_EXPORT QStringList ids();

//! The glyph for @a id in the 16-unit grid; empty for an unknown id.
LMMS_EXPORT QPainterPath glyph(const QString& id);

//! A QIcon that paints @a id: the muted text colour normally, the accent when the button is
//! checked (QIcon::On), the disabled text colour when disabled.
LMMS_EXPORT QIcon icon(const QString& id);

} // namespace shellicons

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_ICONS_H
