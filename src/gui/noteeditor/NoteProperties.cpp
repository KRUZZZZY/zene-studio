/*
 * NoteProperties.cpp - the note editor's note properties box (FL Studio's double-click on a note)
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

#include "NoteProperties.h"

#include <cmath>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QSpinBox>

#include "Accessibility.h"
#include "ControlVocabulary.h"
#include "Note.h"
#include "NoteEditor.h"
#include "NoteGrid.h"
#include "Song.h"
#include "panning.h"
#include "volume.h"

namespace lmms::gui
{

NotePropertiesDialog::NotePropertiesDialog(NoteEditor* editor, const Note& note)
	: QDialog(editor)
	, m_editor(editor)
	, m_clip(editor->clipId())
	, m_note(control::noteIdOf(&note))
{
	setObjectName(QStringLiteral("noteProperties"));
	setWindowTitle(tr("Note Properties: %1").arg(NoteGrid::keyName(note.key())));
	auto* form = new QFormLayout(this);
	addNumber(form, "key", tr("Key"), {0, NumKeys - 1}, note.key());
	addNumber(form, "position", tr("Start (ticks)"), {0, MaxSongLength}, note.pos().getTicks());
	addNumber(form, "length", tr("Length (ticks)"), {1, MaxSongLength}, note.length().getTicks());
	addNumber(form, "velocity", tr("Velocity"), {MinVolume, MaxVolume}, note.getVolume());
	addNumber(form, "pan", tr("Pan"), {PanningLeft, PanningRight}, note.getPanning());
	addNumber(form, "release_velocity", tr("Release velocity (-1 none)"), {-1, MaxVolume}, note.releaseVelocity());
	addNumber(form, "timbre", tr("Mod X (timbre)"), {0, 127}, note.mpeTimbre());
	addNumber(form, "pressure", tr("Mod Y (pressure)"), {0, 127}, note.mpePressure());
	addNumber(form, "probability", tr("Chance (%)"), {0, 100}, static_cast<int>(std::lround(note.probability() * 100)), 100.0);
	addFlag(form, "slide", tr("Slide (portamento)"), note.slide());
	addFlag(form, "muted", tr("Muted"), note.muted());
	// The key's name beside its number, as the keyboard column shows it.
	QSpinBox* key = m_fields.front().box;
	const auto name = [key](int value) { key->setSuffix(QStringLiteral("  (%1)").arg(NoteGrid::keyName(value))); };
	name(key->value());
	connect(key, qOverload<int>(&QSpinBox::valueChanged), this, name);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(this, &QDialog::accepted, this, &NotePropertiesDialog::apply);
	form->addRow(buttons);
}

void NotePropertiesDialog::addNumber(QFormLayout* form, const char* name, const QString& label, std::pair<int, int> range,
	int value, double perUnit)
{
	auto* box = new QSpinBox(this);
	box->setObjectName(QLatin1String(name));
	box->setRange(range.first, range.second);
	box->setValue(value);
	a11y::announce(box, label, tr("The note's %1; OK writes it as one edit, one undo step.").arg(label.toLower()));
	form->addRow(label, box);
	m_fields.push_back({name, box, box->value(), perUnit});
}

void NotePropertiesDialog::addFlag(QFormLayout* form, const char* name, const QString& label, bool value)
{
	auto* box = new QCheckBox(label, this);
	box->setObjectName(QLatin1String(name));
	box->setChecked(value);
	form->addRow(QString(), box);
	m_flags.push_back({name, box, value});
}

QJsonObject NotePropertiesDialog::changes() const
{
	QJsonObject edit{{QStringLiteral("note"), m_note}};
	for (const Field& field : m_fields)
	{
		if (field.box->value() == field.initial) { continue; }
		if (field.perUnit == 1.0) { edit.insert(QLatin1String(field.name), field.box->value()); }
		else { edit.insert(QLatin1String(field.name), field.box->value() / field.perUnit); }
	}
	for (const Flag& flag : m_flags)
	{
		if (flag.box->isChecked() != flag.initial) { edit.insert(QLatin1String(flag.name), flag.box->isChecked() ? 1 : 0); }
	}
	return edit;
}

void NotePropertiesDialog::apply()
{
	const QJsonObject edit = changes();
	if (edit.size() <= 1) { return; }  // only the note's id: nothing changed, so no edit and no undo step
	m_editor->run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_clip}, {QStringLiteral("edits"), QJsonArray{edit}}});
}

void showNoteProperties(NoteEditor* editor, const Note& note)
{
	auto* box = new NotePropertiesDialog(editor, note);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->open();
}

} // namespace lmms::gui
