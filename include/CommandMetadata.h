/*
 * CommandMetadata.h - how a registered command and its arguments read to a musician
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

#ifndef LMMS_GUI_COMMAND_METADATA_H
#define LMMS_GUI_COMMAND_METADATA_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

class QWidget;

/*! 0.5.0 themes 4 and 5 (DESIGN-DECISIONS.md): the palette and the command forms show a
 *  command by a name a musician reads, with its id secondary; they flag a command that cannot be
 *  undone; and a form's field has a label, a unit, its bounds and what a blank means. All of it
 *  is DERIVED here - from the id, the argument schema and the reversibility table - so every one
 *  of the registry's commands has it without a hand-written dialog, and a schema that does carry
 *  a `title`, `description`, `default` or `unit` on a property (the validator ignores the keys)
 *  is read first. */
namespace lmms::gui
{

//! "transport.set_tempo" -> "Set Tempo", "track.remove" -> "Remove Track": a command's name.
LMMS_EXPORT QString commandName(const QString& id);
//! "transport.set_tempo" -> "Transport": the group a command belongs to, as a heading.
LMMS_EXPORT QString commandCategory(const QString& id);
//! The registered command's whole description (empty for an unknown id).
LMMS_EXPORT QString commandDescription(const QString& id);

//! True when running @a id leaves no automatic undo step: the reversibility table classes it
//! irreversible, or its recorded state is a manual fallback only (SPEC A16).
LMMS_EXPORT bool commandIsIrreversible(const QString& id);
//! What the person is told before an irreversible command runs: that it cannot be undone and
//! the table's fallback when it names one. Empty when @a id can be undone.
LMMS_EXPORT QString commandUndoWarning(const QString& id);

//! The argument properties of @a id's schema, and its required keys.
LMMS_EXPORT QJsonObject commandProperties(const QString& id);
LMMS_EXPORT QStringList commandRequired(const QString& id);

//! "gain_db" -> "Gain (dB)", "track_id" -> "Track": a field's label (the schema's `title` first).
LMMS_EXPORT QString fieldLabel(const QString& key, const QJsonObject& spec = {});
//! "gain_db" -> "dB", "length_ticks" -> "ticks": a field's unit (the schema's `unit` first), or empty.
LMMS_EXPORT QString fieldUnit(const QString& key, const QJsonObject& spec = {});
//! The schema's bounds as words: "1 to 9", "0 or more", "up to 1"; empty when unbounded.
LMMS_EXPORT QString fieldRange(const QJsonObject& spec);
//! The one-line guidance under a field: its description, range and unit, default, and what a
//! blank means (optional) or that it must be filled (required).
LMMS_EXPORT QString fieldHelp(const QString& id, const QString& key, const QJsonObject& spec, bool required);

//! What a path argument is for, which decides its picker and its wording.
enum class PathRole
{
	None,        //!< not a path
	OpenFile,    //!< an existing file the command reads
	SaveFile,    //!< a file the command writes (created or replaced)
	OpenFolder,  //!< an existing folder the command reads
	SaveFolder,  //!< a folder the command writes into
};
//! The role of argument @a key of @a id: the schema's `path` ("input", "output", "folder",
//! "output_folder") first, else the sandbox's path rule plus the key's and the verb's words.
LMMS_EXPORT PathRole pathRole(const QString& id, const QString& key, const QJsonObject& spec = {});
//! "Input: an existing file this command reads." and its siblings; empty for PathRole::None.
LMMS_EXPORT QString pathRoleText(PathRole role);

//! How a choice reads: "to_grid" -> "To grid", true -> "On"; a number as itself.
LMMS_EXPORT QString choiceLabel(const QJsonValue& value);
//! A value as a sentence-reader sees it (a whole number in full, a list as "a, b").
LMMS_EXPORT QString valueText(const QJsonValue& value);
//! A command's result as lines of "Label: value" (at most @a maxLines, then "..."), the form's
//! and the floating message's reading of it instead of raw JSON.
LMMS_EXPORT QString resultSummary(const QJsonObject& result, int maxLines = 24);

/*! Whether the person at the screen agrees to @a label on @a target, told @a caution. Nobody is
 *  asked headless or with no interface to ask over (a tool embedding the registry): true there. */
LMMS_EXPORT bool confirmAction(QWidget* parent, const QString& label, const QString& target, const QString& caution);

} // namespace lmms::gui

#endif // LMMS_GUI_COMMAND_METADATA_H
