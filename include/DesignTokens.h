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
#include <QString>

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

//! Component sizes. A clickable target is never below `target` (WCAG 2.5.8, 24 x 24).
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

//! A note's colour from its velocity on the engine's 0..200 volume scale (100 = MIDI 127's
//! neighbourhood, the default). A perceptual blue -> teal -> yellow ramp: low notes recede,
//! loud notes come forward, and the ramp is ordered in lightness so it survives greyscale and
//! the common colour-vision deficiencies.
LMMS_EXPORT QColor velocityColor(int velocity);

//! One of the sixteen track colours, by index (wraps).
LMMS_EXPORT QColor trackColor(int index);
LMMS_EXPORT int trackColorCount();

//! The stylesheet for stock Qt widgets, generated from palette(). Appended after the legacy
//! theme's style.css, so stock widgets everywhere take the new surfaces.
LMMS_EXPORT QString applicationStyleSheet();

//! A hairline rectangle aligned to the device pixel grid, whatever the screen's scale factor:
//! a 1-device-pixel line drawn at a fractional logical coordinate blurs across two pixels.
LMMS_EXPORT void strokeCrisp(QPainter& painter, const QRectF& rect, const QColor& colour);

} // namespace lmms::gui::tokens

#endif // LMMS_GUI_DESIGN_TOKENS_H
