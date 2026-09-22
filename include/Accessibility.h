/*
 * Accessibility.h - one-call accessible name/description helpers for UI sweeps
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

#include <QString>
#include <QWidget>

namespace lmms::a11y
{

//! Announce a control's accessible name - and optionally its description - in
//! one call. This is the pattern every accessibility sweep copies (SPEC-zene-ui
//! -v0 §5 item 1): a sweep walks one surface, calls announce() once per
//! interactive control, and the surface becomes readable to a screen reader.
//!
//! The name is explicit text. A control whose visible label is decorated (a
//! FOCUS chip prefixed with a glyph) passes the clean text here so the glyph is
//! never spoken; a control whose label carries live state (a density button
//! that reads "Density: Standard") passes its CURRENT text here, re-announced
//! every time the text changes. An empty description is left unset rather than
//! clearing a description an earlier sweep set.
inline void announce(QWidget* control, const QString& name,
	const QString& description = QString())
{
	if (control == nullptr) { return; }
	control->setAccessibleName(name);
	if (!description.isEmpty()) { control->setAccessibleDescription(description); }
}

//! Set only the description, never the name: for a control whose accessible
//! name already falls back to its visible text and must keep tracking that
//! text (a label, a state-bearing button) instead of being frozen to a
//! construction-time string.
inline void describe(QWidget* control, const QString& description)
{
	if (control == nullptr || description.isEmpty()) { return; }
	control->setAccessibleDescription(description);
}

} // namespace lmms::a11y
