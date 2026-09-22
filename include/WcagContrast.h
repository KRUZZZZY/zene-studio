/*
 * WcagContrast.h - the in-project WCAG contrast helper (SPEC-zene-ui-v0 §4.5)
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

#pragma once

#include <algorithm>
#include <cmath>

#include <QColor>

namespace lmms::a11y
{

//! One sRGB channel, 0..1, linearised per the WCAG 2.x relative-luminance
//! definition (the piecewise 12.92 / 2.4 transform).
inline qreal linearChannel(qreal channel)
{
	return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

//! Relative luminance of an opaque colour, in [0, 1]. Alpha is ignored: WCAG
//! defines contrast over opaque sRGB, and a translucent role is composited
//! against its backdrop by the painter before anything measures it here.
inline qreal relativeLuminance(const QColor& color)
{
	const qreal r = linearChannel(color.redF());
	const qreal g = linearChannel(color.greenF());
	const qreal b = linearChannel(color.blueF());
	return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

//! Contrast ratio in [1, 21], symmetric in its arguments: the brighter
//! colour's luminance + 0.05 over the darker's + 0.05.
inline qreal contrastRatio(const QColor& a, const QColor& b)
{
	const qreal luminanceA = relativeLuminance(a);
	const qreal luminanceB = relativeLuminance(b);
	const qreal lighter = std::max(luminanceA, luminanceB);
	const qreal darker = std::min(luminanceA, luminanceB);
	return (lighter + 0.05) / (darker + 0.05);
}

//! SC 1.4.3 AA for text: >= 4.5:1 normally; pass 3.0 for large text (>=18pt /
//! 14pt bold). The default is the normal-text threshold on purpose - the one
//! most likely to fail in a dense dark theme.
inline bool meetsTextContrast(const QColor& foreground, const QColor& background,
	qreal minimum = 4.5)
{
	return contrastRatio(foreground, background) >= minimum;
}

//! SC 1.4.11 non-text contrast: >= 3:1 for UI components and graphical
//! objects - focus rings, borders, level indicators.
inline bool meetsComponentContrast(const QColor& foreground, const QColor& background)
{
	return contrastRatio(foreground, background) >= 3.0;
}

} // namespace lmms::a11y
