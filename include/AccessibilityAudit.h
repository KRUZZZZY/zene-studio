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
#include <QStringList>

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

/*! Theme 17, the staged audit: the same walk running only the checks named in @a checks (accessibilityCheckNames();
 *  empty runs them all, which is what the two-argument form does):
 *  - "name": no accessible name (unnamed, implied, filled - as above);
 *  - "role": a bare client or pane (roleless);
 *  - "value": a control that shows a value and gives none (valueless);
 *  - "focus": a control Tab cannot reach (unreachable, unreachable_count);
 *  - "duplicates": controls a screen reader would announce the same way with nothing to tell them apart - the same
 *    name, role and description inside the same named group (a mixer strip's "Mute" is told apart by its strip's
 *    name; two nameless strips' are not): duplicates [{name, role, context, count, objects}] and duplicates_count
 *    (the controls involved).
 *  The reply adds checks (those run) and summary {check: findings}; the lists of checks not run are left out. */
LMMS_EXPORT QJsonObject auditAccessibility(QWidget* root, bool fill, const QStringList& checks);
//! "name", "role", "value", "focus", "duplicates" - the audit's checks, in the order of the staged rollout.
LMMS_EXPORT const QStringList& accessibilityCheckNames();

//! The name @a widget could be announced by when it has none: its constructed name (the title a custom control keeps
//! it in, not a window's) or its parameter's - either without a caption's trailing colon - else its tooltip or its text.
LMMS_EXPORT QString impliedAccessibleName(const QWidget* widget);

} // namespace lmms::gui

#endif // LMMS_GUI_ACCESSIBILITY_AUDIT_H
