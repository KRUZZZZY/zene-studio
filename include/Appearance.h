/*
 * Appearance.h - interface scale, text size and the palette
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
#ifndef LMMS_APPEARANCE_H
#define LMMS_APPEARANCE_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms::appearance
{

/*! 0.5.0 L14 (WF-130 / WF-131): how the interface looks, the user's preference - set by
 *  window.appearance, read when the application starts:
 *  - 'scale' (0.5..3): the whole interface's scale factor. Qt takes it only before the application
 *    object exists, which is before the config file can be found, so it is also kept in a one-line
 *    file beside it in the home folder (~/.zenestudio-scale; ZENE_UI_SCALE_FILE names another) that
 *    main() reads first - QT_SCALE_FACTOR, when set, still wins;
 *  - 'font_scale' (0.75..2): the text size the design tokens draw with;
 *  - 'palette': dark (the default), high_contrast (text 7:1 on every surface, a bright focus ring) or
 *    light (dark text on light surfaces, every pair held to AA - 0.5.0 W3 WF-118);
 *  - 'density' (0.5.0 W3 WF-134 / WF-133): standard (the default), comfortable (more room around every
 *    control, fewer on screen) or touch (targets large enough for a finger) - the stylesheet's paddings and
 *    minimum sizes and the shell's buttons are scaled by densityFactor().
 *  Each applies from the next start. */
struct Options
{
	double scale = 1.0;
	double fontScale = 1.0;
	QString palette = QStringLiteral("dark");
	QString density = QStringLiteral("standard");
};

LMMS_EXPORT const QStringList& paletteNames();
LMMS_EXPORT const QStringList& densityNames();
//! How much larger than standard the metrics of density @a name are: 1 standard, 1.35 comfortable, 1.8 touch.
LMMS_EXPORT double densityFactor(const QString& name);
LMMS_EXPORT Options options();
//! False when the scale file main() reads could not be written (the config values are set either way).
LMMS_EXPORT bool setOptions(const Options& next);
LMMS_EXPORT QJsonObject toJson(const Options& options);
//! The file main() reads the scale from.
LMMS_EXPORT QString scaleFilePath();
//! Write the configured scale to that file (after the config's ui/scale was written some other way);
//! false when the file could not be written in full and committed.
LMMS_EXPORT bool syncScaleFile();
//! main(), before the application object: the stored scale, or 0 when none.
LMMS_EXPORT double storedScale();
//! main(), before the application object: hand the stored scale to Qt (an explicit QT_SCALE_FACTOR wins).
LMMS_EXPORT void applyStoredScale();

} // namespace lmms::appearance

#endif // LMMS_APPEARANCE_H
