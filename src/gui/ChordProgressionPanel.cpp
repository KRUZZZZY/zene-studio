/*
 * ChordProgressionPanel.cpp - the progression panel (include/ChordProgressionPanel.h)
 *
 * The panel is a thin view over two registered commands and no arithmetic of its
 * own: chord.progression_list (with `scale`, `root`, `octave`) is the suggestions
 * it shows, and chord.progression_generate is the one writer - the same command a
 * headless client dispatches. Nothing here names a chord or lays out a note.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ChordProgressionPanel.h"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ChordVocabulary.h" // MinOctave / MaxOctave
#include "ControlRegistry.h"
#include "ControlVocabulary.h" // clipIdOf
#include "MidiClip.h"
#include "ShellSelection.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const char* command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

//! The twelve key names in the sharp spelling the wire uses (ControlScaleShared.h).
QStringList rootNames()
{
	return {QStringLiteral("C"), QStringLiteral("C#"), QStringLiteral("D"), QStringLiteral("D#"),
		QStringLiteral("E"), QStringLiteral("F"), QStringLiteral("F#"), QStringLiteral("G"),
		QStringLiteral("G#"), QStringLiteral("A"), QStringLiteral("A#"), QStringLiteral("B")};
}

//! The chord names a suggestion spells, comma-separated. A suggestion the scale
//! cannot be built on carries no chords, so its own reason is shown instead.
QString spelled(const QJsonObject& suggestion)
{
	QStringList names;
	for (const QJsonValue& value : suggestion.value(QStringLiteral("chords")).toArray())
	{
		const QString name = value.toObject().value(QStringLiteral("chord")).toString();
		names << (name.isEmpty() ? QStringLiteral("?") : name);
	}
	if (names.isEmpty()) { return suggestion.value(QStringLiteral("reason")).toString(); }
	return names.join(QStringLiteral(", "));
}

//! True when a suggestion spells at least one chord (a playable suggestion).
bool spellsChords(const QJsonObject& suggestion)
{
	return !suggestion.value(QStringLiteral("chords")).toArray().isEmpty();
}

} // namespace

ChordProgressionPanel::ChordProgressionPanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Progression"));
	setAccessibleName(tr("Chord progression"));

	m_scale = new QComboBox(this);
	m_scale->setObjectName(QStringLiteral("progressionScale"));
	m_scale->setAccessibleName(tr("Scale"));
	m_root = new QComboBox(this);
	m_root->setObjectName(QStringLiteral("progressionRoot"));
	m_root->setAccessibleName(tr("Key"));
	m_root->addItems(rootNames());
	m_octave = new QSpinBox(this);
	m_octave->setObjectName(QStringLiteral("progressionOctave"));
	m_octave->setAccessibleName(tr("Octave"));
	m_octave->setRange(ChordVocabulary::MinOctave, ChordVocabulary::MaxOctave);
	m_octave->setValue(4);

	m_table = new QTableWidget(0, 2, this);
	m_table->setObjectName(QStringLiteral("progressionTable"));
	m_table->setHorizontalHeaderLabels({tr("Progression"), tr("Chords in this key")});
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

	m_preview = new QLabel(this);
	m_preview->setWordWrap(true);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_target = new QLabel(this);
	m_target->setWordWrap(true);

	auto* write = new QPushButton(tr("Write to Clip"), this);
	write->setObjectName(QStringLiteral("progressionWrite"));
	write->setProperty("controlCommand", QStringLiteral("chord.progression_generate"));
	write->setToolTip(tr("Write the selected progression into the selected MIDI clip as notes"));
	connect(write, &QPushButton::clicked, this, [this] { writeSelected(); });

	connect(m_scale, QOverload<int>::of(&QComboBox::activated), this, [this] { showSuggestions(); });
	connect(m_root, QOverload<int>::of(&QComboBox::activated), this, [this] { showSuggestions(); });
	connect(m_octave, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { showSuggestions(); });
	connect(m_table, &QTableWidget::currentCellChanged, this, [this] { showSelected(); });
	connect(ShellSelection::instance(), &ShellSelection::changed, this,
		[this] { m_target->setText(tr("Target clip: %1").arg(targetClip().isEmpty() ? tr("none selected")
			: targetClip())); });

	auto* key = new QHBoxLayout;
	auto* scaleLabel = new QLabel(tr("Scale"), this);
	scaleLabel->setBuddy(m_scale);
	auto* rootLabel = new QLabel(tr("Key"), this);
	rootLabel->setBuddy(m_root);
	auto* octaveLabel = new QLabel(tr("Octave"), this);
	octaveLabel->setBuddy(m_octave);
	key->addWidget(scaleLabel);
	key->addWidget(m_scale);
	key->addWidget(rootLabel);
	key->addWidget(m_root);
	key->addWidget(octaveLabel);
	key->addWidget(m_octave);
	key->addStretch();

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(key);
	layout->addWidget(m_target);
	layout->addWidget(m_table, 1);
	layout->addWidget(m_preview);
	layout->addWidget(m_status);
	layout->addWidget(write, 0, Qt::AlignRight);

	resize(520, 400);
	refresh();
}

void ChordProgressionPanel::refresh()
{
	const QJsonObject catalogue = run("chord.progression_list").result;
	fillScales(catalogue.value(QStringLiteral("scales")).toArray());
	// The key the group resolves against, when it has one; otherwise the first scale
	// the catalogue offers. The panel does not write the context back.
	const QJsonObject context = run("scale.get_state").result;
	setScale(context.value(QStringLiteral("resolved_scale")).toString(),
		context.value(QStringLiteral("resolved_root")).toInt());
	m_target->setText(tr("Target clip: %1").arg(targetClip().isEmpty() ? tr("none selected")
		: targetClip()));
}

void ChordProgressionPanel::fillScales(const QJsonArray& scales)
{
	m_scale->clear();
	for (const QJsonValue& value : scales) { m_scale->addItem(value.toString()); }
}

void ChordProgressionPanel::setScale(const QString& scale, int root)
{
	int index = m_scale->findText(scale);
	if (index < 0) { index = m_scale->count() > 0 ? 0 : -1; }
	m_filling = true;
	m_scale->setCurrentIndex(index);
	m_root->setCurrentIndex(std::clamp(root, 0, 11));
	m_filling = false;
	showSuggestions();
}

QString ChordProgressionPanel::scaleName() const { return m_scale->currentText(); }

int ChordProgressionPanel::rootPitchClass() const { return m_root->currentIndex(); }

int ChordProgressionPanel::octave() const { return m_octave->value(); }

void ChordProgressionPanel::showSuggestions()
{
	if (m_filling || m_scale->currentIndex() < 0) { return; }
	const ControlResult listed = run("chord.progression_list",
		{{QStringLiteral("scale"), m_scale->currentText()},
			{QStringLiteral("root"), m_root->currentIndex()},
			{QStringLiteral("octave"), m_octave->value()}});
	if (!listed.ok) { m_suggestions = QJsonArray(); say(listed.errorMessage); return; }
	m_suggestions = listed.result.value(QStringLiteral("suggestions")).toArray();
	m_table->setRowCount(static_cast<int>(m_suggestions.size()));
	int firstPlayable = -1;
	for (int row = 0; row < m_suggestions.size(); ++row)
	{
		const QJsonObject suggestion = m_suggestions.at(row).toObject();
		const bool playable = spellsChords(suggestion);
		if (playable && firstPlayable < 0) { firstPlayable = row; }
		m_table->setItem(row, 0, new QTableWidgetItem(suggestion.value(QStringLiteral("name")).toString()));
		m_table->setItem(row, 1, new QTableWidgetItem(spelled(suggestion)));
		if (!playable)
		{
			m_table->item(row, 0)->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
			m_table->item(row, 1)->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
		}
	}
	m_table->setCurrentCell(firstPlayable, 0);
	say(QString());
	showSelected();
}

void ChordProgressionPanel::showSelected()
{
	const int row = m_table->currentRow();
	if (row < 0 || row >= m_suggestions.size()) { m_preview->clear(); return; }
	const QJsonObject suggestion = m_suggestions.at(row).toObject();
	const QString name = suggestion.value(QStringLiteral("name")).toString();
	if (!spellsChords(suggestion))
	{
		m_preview->setText(tr("%1 cannot be spelt in %2: %3").arg(name, scaleName(),
			suggestion.value(QStringLiteral("reason")).toString()));
		return;
	}
	m_preview->setText(tr("%1 in %2: %3").arg(name, scaleName(), spelled(suggestion)));
}

void ChordProgressionPanel::say(const QString& message)
{
	m_status->setText(message);
	m_status->setVisible(!message.isEmpty());
}

int ChordProgressionPanel::rowCount() const { return m_table->rowCount(); }

QString ChordProgressionPanel::progressionAt(int row) const
{
	if (row < 0 || row >= m_suggestions.size()) { return {}; }
	return m_suggestions.at(row).toObject().value(QStringLiteral("name")).toString();
}

QString ChordProgressionPanel::chordsAt(int row) const
{
	if (row < 0 || row >= m_suggestions.size()) { return {}; }
	return spelled(m_suggestions.at(row).toObject());
}

bool ChordProgressionPanel::fitsAt(int row) const
{
	if (row < 0 || row >= m_suggestions.size()) { return false; }
	return spellsChords(m_suggestions.at(row).toObject());
}

QString ChordProgressionPanel::chordsFor(const QString& progression) const
{
	for (int row = 0; row < m_suggestions.size(); ++row)
	{
		if (progressionAt(row) == progression) { return chordsAt(row); }
	}
	return {};
}

bool ChordProgressionPanel::selectProgression(const QString& name)
{
	for (int row = 0; row < m_suggestions.size(); ++row)
	{
		if (progressionAt(row) == name) { m_table->setCurrentCell(row, 0); return true; }
	}
	return false;
}

QString ChordProgressionPanel::targetClip() const
{
	if (ShellSelection* selection = ShellSelection::instance())
	{
		Clip* clip = selection->clip();
		if (dynamic_cast<MidiClip*>(clip) != nullptr) { return control::clipIdOf(clip); }
	}
	return {};
}

QString ChordProgressionPanel::writeTo(const QString& clipId, const QString& progression)
{
	if (clipId.isEmpty()) { return tr("Select a MIDI clip in the arranger first."); }
	const ControlResult written = run("chord.progression_generate",
		{{QStringLiteral("clip"), clipId}, {QStringLiteral("progression"), progression},
			{QStringLiteral("scale"), scaleName()}, {QStringLiteral("root"), rootPitchClass()},
			{QStringLiteral("octave"), octave()}});
	if (!written.ok) { say(written.errorMessage); return written.errorMessage; }
	say(tr("Wrote %1 notes into %2.").arg(written.result.value(QStringLiteral("notes_written")).toInt())
		.arg(clipId));
	return {};
}

QString ChordProgressionPanel::writeSelected()
{
	const int row = m_table->currentRow();
	if (row < 0 || row >= m_suggestions.size()) { return tr("Select a progression first."); }
	if (!fitsAt(row)) { return tr("'%1' does not fit %2.").arg(progressionAt(row), scaleName()); }
	return writeTo(targetClip(), progressionAt(row));
}

} // namespace lmms::gui
