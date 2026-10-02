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
#include <QByteArray>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::gui
{

//! The interface's one icon set: Lucide (https://lucide.dev, ISC licence - the notice is
//! data/themes/zene/icons/LICENSE.txt), compiled in as SVG and rendered at paint time, so every
//! icon is sharp at any size and any device-pixel ratio and drawn in a token colour. The shell
//! names its icons by id; the inherited interface's bitmap icon names are mapped onto the same
//! set (legacyPixmap), so no old bitmap icon is drawn anywhere embed::getIconPixmap is used.
namespace shellicons
{

//! Every id icon() knows, for the test that renders each one.
LMMS_EXPORT QStringList ids();
//! The SVG for @a id; empty for an unknown id.
LMMS_EXPORT QByteArray svg(const QString& id);
//! A QIcon that paints @a id: the text colour normally, the accent when the button is checked
//! (QIcon::On), the disabled text colour when disabled.
LMMS_EXPORT QIcon icon(const QString& id);

//! The inherited icon names the table maps.
LMMS_EXPORT QStringList legacyNames();
//! The Lucide icon an inherited name is drawn with; empty when it is not mapped (artwork, not
//! an icon: knob faces, LCD digits, waveform buttons - widgets that are repainted instead).
LMMS_EXPORT QString lucideForLegacy(const QString& name);
//! The modern pixmap for an inherited icon name at @a width x @a height (the old bitmap's own
//! size when either is <= 0), or a null pixmap when the name is not mapped.
LMMS_EXPORT QPixmap legacyPixmap(const QString& name, int width, int height);

} // namespace shellicons

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_ICONS_H
