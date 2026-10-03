/*
 * AccessibilityAudit.h - which controls a screen reader cannot name, and names for them
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

#ifndef LMMS_GUI_ACCESSIBILITY_AUDIT_H
#define LMMS_GUI_ACCESSIBILITY_AUDIT_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! 0.5.0 W3 L14 (WF-129): a walk over the controls under @a root - buttons, sliders, knobs, faders, fields,
 *  lists, anything that takes the keyboard - counting those a screen reader would announce with no name.
 *  With @a fill, each nameless one is given the name it already carries elsewhere: its parameter's name (a
 *  knob, a fader, a LED button), its tooltip or its text; a control with none stays in the report.
 *  Answers {checked, named, filled, implied (nameless, with a name to give when 'fill' is not set),
 *  unnamed: [{class, object, parent}] - nothing to name them by, at most 50 listed}. */
LMMS_EXPORT QJsonObject auditAccessibility(QWidget* root, bool fill);

//! The name @a widget could be announced by when it has none: its parameter's, its tooltip or its text.
LMMS_EXPORT QString impliedAccessibleName(const QWidget* widget);

} // namespace lmms::gui

#endif // LMMS_GUI_ACCESSIBILITY_AUDIT_H
