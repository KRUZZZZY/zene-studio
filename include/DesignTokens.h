/*
 * DesignTokens.h - the 0.5.0 interface's one source of colour, spacing, type and size
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

#ifndef LMMS_GUI_DESIGN_TOKENS_H
#define LMMS_GUI_DESIGN_TOKENS_H

#include <vector>

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QPainter;
class QRectF;

//! The design system of the 0.5.0 shell (docs/adr/ADR-0001-UI-TOOLKIT.md, docs/UI-SHELL.md).
//!
//! Every new widget paints from these values and nothing else: no colour literal, no magic
//! spacing. The application stylesheet for stock Qt widgets is GENERATED from the same values
//! (applicationStyleSheet()), so the theme cannot fork into a second palette. The dark theme
//! is flat (surfaces differ by lightness only, no gradients) and high-contrast: every
//! text-on-surface pair it uses is listed in contrastPairs() and held to WCAG AA by
//! DesignTokensTest.
namespace lmms::gui::tokens
{

//! Colour roles. Surfaces step up in lightness from the window base to a pressed control.
struct Palette
{
	QColor base;         //!< the window behind every panel; seams between panels
	QColor panel;        //!< a panel's body
	QColor raised;       //!< panel headers, the transport bar, the footer
	QColor control;      //!< buttons, fields, track headers
	QColor controlHover; //!< a control under the pointer
	QColor selected;     //!< a selected neutral item (a row, a track)
	QColor separator;    //!< hairlines inside a panel

	QColor text;         //!< primary text
	QColor textMuted;    //!< secondary text, captions, units
	QColor textDisabled; //!< inactive controls (WCAG 1.4.3 exempts them)
	QColor textOnAccent; //!< text drawn on the accent fill

	QColor accent;       //!< the one interaction colour: active toggles, focus of attention
	QColor accentHover;
	QColor focusRing;    //!< keyboard focus; >= 3:1 against every surface (WCAG 1.4.11)

	QColor play;         //!< transport running
	QColor record;       //!< armed / recording
	QColor warning;
	QColor meterLow;
	QColor meterMid;
	QColor meterHigh;

	QColor gridBar;      //!< editor grid: bar lines
	QColor gridBeat;     //!< editor grid: beat lines
	QColor gridStep;     //!< editor grid: sub-beat lines
	QColor keyWhiteRow;  //!< piano-roll row of a white key
	QColor keyBlackRow;  //!< piano-roll row of a black key
	QColor scaleRow;     //!< piano-roll row of an in-scale key when a scale is highlighted
	QColor playhead;

	QColor clipMidi;      //!< arranger clip bodies, by kind; their content draws dark on top
	QColor clipSample;
	QColor clipAutomation;
	QColor clipPattern;
};

//! The palette of the shipped dark theme.
LMMS_EXPORT const Palette& palette();

//! Spacing scale, device-independent pixels.
namespace space
{
constexpr int xxs = 2;
constexpr int xs = 4;
constexpr int sm = 6;
constexpr int md = 8;
constexpr int lg = 12;
constexpr int xl = 16;
constexpr int xxl = 24;
} // namespace space

//! Corner radii.
namespace radius
{
constexpr int sm = 2;
constexpr int md = 4;
constexpr int lg = 6;
} // namespace radius

//! Component sizes. A clickable target is never below `target` (WCAG 2.5.8, 24 x 24); accessible
//! mode, the touch-first layout, raises every target to Appearance's accessibleTargetPixels (44 x 44).
namespace size
{
constexpr int target = 24;
constexpr int icon = 16;
constexpr int headerHeight = 56;
constexpr int footerHeight = 30;
constexpr int panelHeader = 28;
constexpr int inspectorWidth = 240;
constexpr int browserWidth = 280;
constexpr int bottomHeight = 300;
} // namespace size

//! Type scale in pixels. `display` is the transport's position readout.
enum class TextRole
{
	Caption,
	Small,
	Body,
	Title,
	Display
};
LMMS_EXPORT QFont font(TextRole role);
//! 0.5.0 L14 (WF-130): the palette by name - dark, high_contrast or light (Appearance.h); palette() is the
//! chosen one (high_contrast in accessible mode) with the user's theme overrides (themeOverrides()) put on it.
LMMS_EXPORT Palette paletteNamed(const QString& name);

//! 0.5.0 W3 (WF-117 theme editing): the colour roles' names - the stylesheet's @tokens, theme.set_token's names.
LMMS_EXPORT QStringList tokenNames();
//! A role of @a palette by name; invalid (and *known false) when @a name is not a role.
LMMS_EXPORT QColor roleColor(const Palette& palette, const QString& name, bool* known = nullptr);
//! @a base with every {role: "#rrggbb"} of @a colors put on it; @a rejected collects the names that are not
//! roles or whose colour does not parse (they are skipped).
LMMS_EXPORT Palette withOverrides(Palette base, const QJsonObject& colors, QStringList* rejected = nullptr);
//! The user's colour overrides, {role: "#rrggbb"} (config ui/theme, written by theme.set_token / theme.import).
LMMS_EXPORT QJsonObject themeOverrides();

//! 0.5.0 UX decision (themes 17 and 23): whether accessible mode is on (Appearance.h) - read once, like the palette.
LMMS_EXPORT bool accessibleMode();
//! Accessible mode's reduced motion: no animated menus, tooltips, docks or smooth scrolling.
LMMS_EXPORT bool reducedMotion();
//! A keyboard focus ring's width in device-independent pixels: 2 (WCAG 2.4.13's minimum), 3 in accessible mode.
LMMS_EXPORT qreal focusRingWidth();
//! How much accessible mode enlarges click targets and paddings: 1, or Appearance's accessibleTargetFactor
//! (it replaces the W3 density presets; the one global zoom does all other sizing).
LMMS_EXPORT double targetFactor();
//! A clickable target's side: size::target times targetFactor(), never below it (44 in accessible mode).
LMMS_EXPORT int targetSize();
//! 0.5.0 L14 (Q050-235): whether this run draws the touch workspace (Appearance.h) - read once, like the palette.
LMMS_EXPORT bool touchWorkspace();
/*! The side a named tap target is held to: targetSize(), and at least Appearance's touchTargetSide (44 Qt logical
 *  pixels) in the touch workspace. A widget whose explicit size bypasses targetSize() (the launcher's cells, a
 *  fader) takes this so its real hit rectangle - not only its artwork - meets the touch minimum. */
LMMS_EXPORT int tapTargetSize();
//! The gap between adjacent targets: @a standard, or at least Appearance's accessibleTargetSpacing (8 logical
//! pixels) in accessible mode, so a finger cannot hit the neighbour of the target it aimed at.
LMMS_EXPORT int targetSpacing(int standard);
//! @a css with every length of its padding, min-height and min-width declarations scaled by @a factor
//! (accessible mode's stylesheet; exposed for the test).
LMMS_EXPORT QString scaleMetrics(const QString& css, double factor);
//! The rules accessible mode adds to the stylesheet: thick borders on every bordered control, so the focus ring
//! (the border's colour) is strong, and focus rings on check boxes, radio buttons and lists (exposed for the test).
LMMS_EXPORT QString accessibleModeRules();
//! The interface's one typeface (Inter, SIL OFL 1.1, compiled in - data/fonts/OFL.txt); its
//! faces are registered on first use. "sans-serif" if the resource could not be read.
LMMS_EXPORT QString family();

//! One text/surface pair the theme uses, and the contrast it must reach (4.5 for text,
//! 3.0 for large text and for non-text components).
struct ContrastPair
{
	const char* name;
	QColor foreground;
	QColor background;
	double minimum;
};
LMMS_EXPORT std::vector<ContrastPair> contrastPairs();
//! The same pairs for @a palette (0.5.0 L14: the high-contrast palette is held to them too).
LMMS_EXPORT std::vector<ContrastPair> contrastPairs(const Palette& palette);

//! A note's colour from its velocity on the engine's 0..200 volume scale (100 = MIDI 127's
//! neighbourhood, the default). A perceptual blue -> teal -> yellow ramp: low notes recede,
//! loud notes come forward, and the ramp is ordered in lightness so it survives greyscale and
//! the common colour-vision deficiencies.
LMMS_EXPORT QColor velocityColor(int velocity);

//! One of the sixteen track colours, by index (wraps).
LMMS_EXPORT QColor trackColor(int index);
LMMS_EXPORT int trackColorCount();

//! The interface's stylesheet: data/themes/zene/zene.qss (compiled in as :/zene/zene.qss) with
//! every @token resolved from palette() - in accessible mode with accessibleModeRules() after it and its
//! metrics scaled by targetFactor(). It replaces the inherited theme's style.css for the
//! default theme, covering the stock Qt widgets and every inherited editor's colour properties.
LMMS_EXPORT QString applicationStyleSheet();
//! The unresolved template, and the resolver (exposed for DesignTokensTest).
LMMS_EXPORT QString styleSheetTemplate();
//! @a unknown collects any @name that is not a palette role.
LMMS_EXPORT QString resolveStyleSheet(const QString& templ, QStringList* unknown);
//! A palette role by its name in the template ("panel", "textMuted"); invalid if unknown.
LMMS_EXPORT QColor tokenColor(const QString& name, bool* known = nullptr);

//! A hairline rectangle aligned to the device pixel grid, whatever the screen's scale factor:
//! a 1-device-pixel line drawn at a fractional logical coordinate blurs across two pixels.
LMMS_EXPORT void strokeCrisp(QPainter& painter, const QRectF& rect, const QColor& colour);

} // namespace lmms::gui::tokens

#endif // LMMS_GUI_DESIGN_TOKENS_H
