/*
 * ContextHelp.h - hover and What's This help from the commands every control runs
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
#ifndef LMMS_GUI_CONTEXT_HELP_H
#define LMMS_GUI_CONTEXT_HELP_H

#include <QObject>
#include <QString>

#include "lmms_export.h"

class QWidget;

namespace lmms::gui
{

/*! 0.5.0 L14 (WF-137): hover and context help with nothing to write per widget. Every control that
 *  runs a registry command says which (its "controlCommand" property, which the agent-surface sweep
 *  already checks), and every command has a description - so:
 *  - hovering a control that has no tooltip of its own shows its command's first sentence;
 *  - What's This (Shift+F1, Help > What's This?, then a click) shows the whole description and the
 *    command's id, for any control that runs one.
 *  One application-wide event filter, installed once by the shell. */
class LMMS_EXPORT ContextHelp : public QObject
{
	Q_OBJECT
public:
	static void install();

	//! The help a widget carries: its command's description (or its parents', two levels up); empty when none.
	static QString helpFor(const QWidget* widget, bool full);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CONTEXT_HELP_H
