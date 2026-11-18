/*
 * CommandFormFields.h - the editor a command form gives each argument
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

#ifndef LMMS_GUI_COMMAND_FORM_FIELDS_H
#define LMMS_GUI_COMMAND_FORM_FIELDS_H

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "lmms_export.h"

class QLabel;
class QWidget;

/*! 0.5.0 theme 5: a semantic editor per argument, chosen from its schema - a choice list for an
 *  enum or a boolean (each value in words, the "(Default)" entry first when it is optional), a
 *  track/channel picker for a reference the project can list, a key-capture for a shortcut, a
 *  comma-separated list for an array of plain values, a typed field with its unit beside it for a
 *  number, and Open / Save As / Choose Folder / Choose Colour beside a path or a colour. Raw JSON
 *  remains only for an object or a list of records, and the form puts those under Advanced. */
namespace lmms::gui::formfield
{

struct Editor
{
	QWidget* row = nullptr;    //!< what the form lays out: the value widget, with its unit and picker
	QWidget* value = nullptr;  //!< the widget that holds the value
};

//! True for an argument only raw JSON can express (an object, a list of records): the form shows
//! it under Advanced.
LMMS_EXPORT bool isRawField(const QJsonObject& spec);

//! The editor for argument @a key (schema @a spec) of command @a id, its widgets parented to @a parent.
LMMS_EXPORT Editor makeEditor(QWidget* parent, const QString& id, const QString& key, const QJsonObject& spec,
	bool required);

/*! What @a editor holds, as the argument's JSON value: Undefined when it is blank. False, with the
 *  reason in @a error (without the field's label), when the text is not of the field's type or is
 *  outside its bounds. */
LMMS_EXPORT bool readValue(const QWidget* editor, const QJsonObject& spec, QJsonValue* value, QString* error);

//! Put @a value into @a editor; false when a choice does not offer it, or when @a value is not of
//! the field's type (@a spec) and so could never be read back from it.
LMMS_EXPORT bool writeValue(QWidget* editor, const QJsonObject& spec, const QJsonValue& value);

//! A muted, word-wrapped caption (a field's guidance, a unit, a command's id) in the design tokens' caption type.
LMMS_EXPORT QLabel* captionLabel(const QString& text, QWidget* parent);

//! A fixed argument as a person reads it: a track's, channel's or clip's name with its id beside it.
LMMS_EXPORT QString fixedText(const QString& key, const QJsonValue& value);

} // namespace lmms::gui::formfield

#endif // LMMS_GUI_COMMAND_FORM_FIELDS_H
