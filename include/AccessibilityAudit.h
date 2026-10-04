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

/*! 0.5.0 W3 L14 (WF-129): a walk over @a root and the controls under it - buttons, sliders, knobs, faders, fields,
 *  lists, anything that takes the keyboard - counting those a screen reader would announce with no name.
 *  With @a fill, each nameless one is given the name it already carries elsewhere: its parameter's name (a
 *  knob, a fader, a LED button), its constructed name, its tooltip or its text; a control with none stays in the report.
 *  0.5.0 UX decision (theme 17): it checks what the platform's accessibility bridge is told too - the role (a
 *  control announced as a bare "client" or "pane" says nothing about what it is) and, for a control that shows a
 *  value (a slider, a spin box, a combo box, a knob, a fader, an LCD number), whether the value is given - and
 *  counts the controls a keyboard cannot reach. Answers {checked, named, filled, implied (nameless, with a name to
 *  give when 'fill' is not set), unnamed: [{class, object, parent}] - nothing to name them by - and unnamed_count,
 *  roleless / roleless_count and valueless / valueless_count ([{class, object, parent, role}]), and
 *  unreachable_count (no Tab focus)}; each list holds at most 50, its count all of them. */
LMMS_EXPORT QJsonObject auditAccessibility(QWidget* root, bool fill);

//! The name @a widget could be announced by when it has none: its constructed name (the title a custom control keeps
//! it in, not a window's) or its parameter's - either without a caption's trailing colon - else its tooltip or its text.
LMMS_EXPORT QString impliedAccessibleName(const QWidget* widget);

} // namespace lmms::gui

#endif // LMMS_GUI_ACCESSIBILITY_AUDIT_H
