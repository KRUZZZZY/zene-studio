/*
 * NoteProperties.h - the note editor's note properties box (FL Studio's double-click on a note)
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

#ifndef LMMS_GUI_NOTE_PROPERTIES_H
#define LMMS_GUI_NOTE_PROPERTIES_H

#include <vector>

#include <QDialog>
#include <QJsonObject>

#include "lmms_export.h"

class QCheckBox;
class QFormLayout;
class QSpinBox;

namespace lmms
{
class Note;
}

namespace lmms::gui
{

class NoteEditor;

//! The properties of one note, as FL Studio's piano roll shows them on a double-click: key, start, length,
//! velocity, pan, release velocity, Mod X and Mod Y (the note's MPE timbre and pressure), chance, slide and
//! mute. OK writes the fields that changed as ONE note.edit - one undo step, the command an agent sends - to
//! the note double-clicked. The note is held by its id, not its address: the clip can change while the box
//! is open, and a note that went is refused by the command, not written through a stale pointer.
class LMMS_EXPORT NotePropertiesDialog : public QDialog
{
	Q_OBJECT

public:
	NotePropertiesDialog(NoteEditor* editor, const Note& note);

	//! The note.edit entry for what changed: {note, field: value...}; just {note} when nothing did.
	QJsonObject changes() const;
	//! Send changes() as one note.edit (nothing when nothing changed).
	void apply();

private:
	//! One number field: its note.edit name, its box, the value it opened with, and the box's unit per field unit.
	struct Field
	{
		const char* name;
		QSpinBox* box;
		int initial;
		double perUnit;
	};
	//! A flag field (slide, muted), sent as 0 / 1.
	struct Flag
	{
		const char* name;
		QCheckBox* box;
		bool initial;
	};

	void addNumber(QFormLayout* form, const char* name, const QString& label, std::pair<int, int> range, int value,
		double perUnit = 1.0);
	void addFlag(QFormLayout* form, const char* name, const QString& label, bool value);

	NoteEditor* m_editor;
	QString m_clip;
	QString m_note;
	std::vector<Field> m_fields;
	std::vector<Flag> m_flags;
};

//! Open the properties box for @a note (window-modal, not blocking: OK applies it).
LMMS_EXPORT void showNoteProperties(NoteEditor* editor, const Note& note);

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_PROPERTIES_H
