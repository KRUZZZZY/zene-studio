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
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QSpinBox>

#include "Accessibility.h"
#include "ControlVocabulary.h"
#include "Note.h"
#include "NoteConditions.h"
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
	addRecurrence(form, note);  // Q050-242: the pass pattern the note repeats over
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

/*! Q050-242, ED-133: the recurrence row. "Recurrence (passes)" is the pattern's length - 0 means
 *  every pass, which is the neutral value an absent recl/recm pair loads as - beside one check box
 *  per pass bit, "1".."16" this many passes into the pattern. A stored mask uses bits below the
 *  length (NoteConditions::recursOnPass takes pass % recurLength); the boxes are shown for the
 *  length in force so a mask can never name a pass the model would drop, and they are disabled at
 *  length 0 because that mask is not saved at all.
 *
 *  The two travel together: note.edit cuts a new mask to the new length, so a length change alone
 *  would silently rewrite the mask. changes() reflects the mask the boxes show for the length the
 *  user picked, and sends both fields whenever either changed - one entry, one checkpoint. */
void NotePropertiesDialog::addRecurrence(QFormLayout* form, const Note& note)
{
	const NoteConditions& conditions = note.conditions();
	m_initialRecurLength = conditions.recurLength;
	m_initialRecurMask = conditions.recurMask;
	auto* row = new QWidget(this);
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* length = new QSpinBox(row);
	length->setObjectName(QStringLiteral("recur_length"));
	length->setRange(0, NoteConditions::MaxRecurLength);
	length->setValue(m_initialRecurLength);
	a11y::announce(length, tr("Recurrence (passes)"),
		tr("How many passes of the loop the note's recurrence repeats over. 0 plays it on every pass."));
	layout->addWidget(length);
	// A pass bit is 1 above its zero-based index, so "1" is pass 0 (the first pass of the pattern).
	for (int pass = 0; pass < MaxPassBits; ++pass)
	{
		auto* bit = new QCheckBox(QString::number(pass + 1), row);
		bit->setObjectName(QStringLiteral("recur_pass_%1").arg(pass + 1));
		bit->setChecked((m_initialRecurMask >> pass) & 1);
		a11y::announce(bit, tr("Pass %1").arg(pass + 1),
			tr("Whether the note plays on pass %1 of the recurrence pattern.").arg(pass + 1));
		layout->addWidget(bit);
		m_passBits.push_back(bit);
	}
	auto* hint = new QLabel(tr("0 passes plays every pass; a mask with no pass ticked plays none."), row);
	hint->setWordWrap(true);
	form->addRow(tr("Recurrence (passes)"), row);
	form->addRow(QString(), hint);
	m_fields.push_back({"recur_length", length, m_initialRecurLength, 1.0});
	m_recurLength = length;
	connect(length, qOverload<int>(&QSpinBox::valueChanged), this, &NotePropertiesDialog::showPassBits);
	showPassBits(m_initialRecurLength);
}

int NotePropertiesDialog::passMask() const
{
	int mask = 0;
	for (int pass = 0; pass < static_cast<int>(m_passBits.size()); ++pass)
	{
		if (m_passBits[static_cast<std::size_t>(pass)]->isChecked()) { mask |= 1 << pass; }
	}
	return mask;
}

void NotePropertiesDialog::showPassBits(int length)
{
	// A positive length enables exactly its own pass bits. At 0, every pass plays, so show all bits
	// disabled; choosing a positive length enables the corresponding mask controls.
	const bool everyPass = length <= 0;
	const int shown = everyPass ? MaxPassBits : length;
	for (int pass = 0; pass < static_cast<int>(m_passBits.size()); ++pass)
	{
		QCheckBox* bit = m_passBits[static_cast<std::size_t>(pass)];
		bit->setVisible(pass < shown);
		bit->setEnabled(!everyPass);
	}
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
	addRecurrenceChange(&edit);
	return edit;
}

/*! The recurrence pair travels together (Q050-242): note.edit cuts a new mask to a new length, so a
 *  length change alone would rewrite the mask behind the user's back. At length 0 no mask is saved
 *  and the model zeroes it, so 0 is what the box reports for "every pass". */
void NotePropertiesDialog::addRecurrenceChange(QJsonObject* edit) const
{
	const int length = m_recurLength != nullptr ? m_recurLength->value() : m_initialRecurLength;
	const int mask = length > 0 ? (passMask() & ((1 << length) - 1)) : 0;
	if (length == m_initialRecurLength && mask == m_initialRecurMask) { return; }
	edit->insert(QStringLiteral("recur_length"), length);
	edit->insert(QStringLiteral("recur_mask"), mask);
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
