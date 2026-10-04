/*
 * HelpTasks.h - help by task: the manual's sections as the tasks commands, shortcuts and controls belong to
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

#ifndef LMMS_GUI_HELP_TASKS_H
#define LMMS_GUI_HELP_TASKS_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QMenuBar;
class QWidget;

/*! 0.5.0 design theme 21: help a musician finds without knowing the program's inner words. The tasks are
 *  the manual's own sections (data/manual/index.html's <h2 id>s, under the same titles), so the command
 *  reference, the shortcuts page and in-context help group by the words the manual uses, and every
 *  command is named by CommandMetadata's friendly name, its id secondary. */
namespace lmms::gui::help
{

//! The manual's sections, in its order: the anchors app.help opens it at ("window", "arrange", ... "access").
LMMS_EXPORT const QStringList& topics();
//! The section's title as the manual's heading has it ("mix" -> "Mixing"); empty for an unknown anchor.
LMMS_EXPORT QString topicTitle(const QString& topic);
//! More words a musician may look a task up by ("mix": "mixer level volume pan send fader ...").
LMMS_EXPORT QString topicWords(const QString& topic);
//! The task a command belongs to, by its group ("mixer.set_volume" -> "mix"); empty for a group no section covers.
LMMS_EXPORT QString commandTopic(const QString& id);
//! A menu entry's task: its command's, else its top-level menu's (@a menuPath "File ▸ Export" -> "projects").
LMMS_EXPORT QString menuTopic(const QString& command, const QString& menuPath);
//! The task the part of the window @a widget is in: the nearest "helpTopic" property, else its command's,
//! else what kind of panel holds it (a mixer, a piano roll ...); "window" when nothing says.
LMMS_EXPORT QString widgetTopic(const QWidget* widget);

/*! In-context help for @a widget that needs no hovering (F1, app.help's 'context'): {name - what the control
 *  is called; help - its What's This, tooltip or description as plain text; command, command_name and
 *  command_help - the registry command it runs, by its friendly name, with the first sentence of its
 *  description; shortcut - the key a menu gives that command, when one does; topic and topic_title - the
 *  manual section it belongs to}. Only {topic, topic_title} for no widget. */
LMMS_EXPORT QJsonObject contextHelp(const QWidget* widget);
//! @a context as the help centre shows it: a heading and a few lines of HTML.
LMMS_EXPORT QString contextHtml(const QJsonObject& context);

//! The key each command has in @a bar's menus (command id -> shortcut, as the menu shows it).
LMMS_EXPORT QHash<QString, QString> commandShortcuts(QMenuBar* bar);
/*! The command reference as HTML: every registered command whose id, friendly name, description, shortcut
 *  or task (its title or the words it is looked up by) contains @a filter - under its task's heading (the
 *  manual's sections in its order, each linking to its section as "topic:<anchor>"; "Other commands" last),
 *  by friendly name, each with its id, its shortcut (@a shortcuts) and its arguments. */
LMMS_EXPORT QString referenceHtml(const QString& filter, const QHash<QString, QString>& shortcuts);

} // namespace lmms::gui::help

#endif // LMMS_GUI_HELP_TASKS_H
