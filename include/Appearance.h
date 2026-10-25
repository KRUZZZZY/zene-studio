/*
 * Appearance.h - the interface's zoom, palette and accessible mode
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
 *  - 'scale' (0.5..3): the one global zoom, and the only sizing control (0.5.0 UX decision, theme 23: text,
 *    icons and every control scale together, as Bitwig's UI zoom does). Qt takes it only before the application
 *    object exists, so main() reads ui/scale from the selected --config file directly before QApplication
 *    construction. When that file has no scale entry, the home-folder fallback (~/.zenestudio-scale;
 *    ZENE_UI_SCALE_FILE names another) is used. QT_SCALE_FACTOR, when set, still wins;
 *  - 'palette': dark (the default), high_contrast (text 7:1 on every surface, a bright focus ring) or
 *    light (dark text on light surfaces, every pair held to AA - 0.5.0 W3 WF-118);
 *  - 'accessible' (0.5.0 UX decision, themes 17 and 23): accessible mode - larger click targets (accessibleTargetFactor),
 *    strong focus rings on every control, the high-contrast palette whatever 'palette' says, and reduced motion
 *    (no animated menus, tooltips, docks or scrolling). It replaces the W3 layout-density presets: the config
 *    keeps it in ui/density, where "accessible" is on and "standard" off, and the old values comfortable and
 *    touch still read as on.
 *  - 'workspace' (0.5.0 L14, Q050-235): standard, or touch - the touch workspace holds the launcher's cells,
 *    scene buttons and stop buttons to at least touchTargetSide (44) Qt logical pixels at 100% zoom. It is a
 *    preference of its own: it changes no palette, motion or density, so the old 'density: "touch"' keeps meaning
 *    accessible mode exactly as before. It is kept in ui/workspace and, like the rest, applies from the next start.
 *  Each applies from the next start. */
struct Options
{
	double scale = 1.0;
	QString palette = QStringLiteral("dark");
	bool accessible = false;
	QString workspace = QStringLiteral("standard");

	bool operator==(const Options&) const = default;
};

//! How much larger accessible mode's click targets and control paddings are than standard's (24 -> 32 px).
constexpr double accessibleTargetFactor = 1.35;

//! The side a touch workspace's named tap targets are held to, in Qt logical pixels (WCAG 2.5.5's 44 x 44).
constexpr int touchTargetSide = 44;

LMMS_EXPORT const QStringList& paletteNames();
//! The workspaces window.appearance offers: standard (the default) and touch.
LMMS_EXPORT const QStringList& workspaceNames();
//! Whether @a name is the touch workspace.
LMMS_EXPORT bool workspaceIsTouch(const QString& name);
//! The layout-density values window.appearance still accepts from before accessible mode: standard (off),
//! comfortable and touch (both on).
LMMS_EXPORT const QStringList& densityNames();
//! Whether the density value @a name means accessible mode (comfortable, touch and accessible do).
LMMS_EXPORT bool densityIsAccessible(const QString& name);
//! The palette the interface draws with: high_contrast in accessible mode, else the chosen one.
LMMS_EXPORT QString effectivePalette(const Options& options);
LMMS_EXPORT Options options();
//! False when the scale file main() reads could not be written (the config values are set either way).
LMMS_EXPORT bool setOptions(const Options& next);
//! {scale, palette, accessible, workspace, applies, restart_required, applies_note, restart_pending}, plus the two
//! old fields: font_scale (always 1 - text follows the zoom) and density (comfortable in accessible mode, standard
//! otherwise).
LMMS_EXPORT QJsonObject toJson(const Options& options);

/*! 0.5.0 UX decision (theme 20): every change to how the interface looks says when it takes effect. Each one - the
 *  zoom, the palette, accessible mode, a colour override - is read when the interface starts and applies from the next
 *  start (a live switch would leave controls that cached the old colours mixed with new ones), so this is
 *  {applies: "next_start", restart_required: true, applies_note: the sentence a person reads}. */
LMMS_EXPORT QJsonObject applyTiming();
//! The options this run of the interface draws with: captured the first time anything asks - the palette's first read,
//! before any change this session can be made - and unchanged after.
LMMS_EXPORT const Options& startedWith();
/*! Tell the person at the window that a change to the look was saved and when it applies (a non-modal floating
 *  message, spoken to a screen reader too). Nothing without a main window: an agent, a test or the CLI. Implemented
 *  with the theme commands in the interface (src/gui/ControlCommandsTheme.cpp). */
LMMS_EXPORT void noticeAppliesNextStart();
//! The file main() reads the scale from.
LMMS_EXPORT QString scaleFilePath();
//! Write the configured scale to that file (after the config's ui/scale was written some other way);
//! false when the file could not be written in full and committed.
LMMS_EXPORT bool syncScaleFile();
//! main(), before the application object: the stored scale, or 0 when none.
LMMS_EXPORT double storedScale();
//! main(), before the application object: hand the stored fallback to Qt (an explicit QT_SCALE_FACTOR wins).
LMMS_EXPORT void applyStoredScale();
//! Use the selected config's ui/scale when present, falling back to the stored scale file.
LMMS_EXPORT void applyStoredScale(const QString& configFile);

} // namespace lmms::appearance

#endif // LMMS_APPEARANCE_H
