/*
 * ChordProgressionPanel.h - the progression panel: browse the named progressions,
 *                           see them spelt in the selected scale, and write one
 *                           into a clip
 *
 * WHAT IT IS. The interface half of chord.progression_generate (L07, queue unit
 * Q050-019): the catalogue chord.progression_list reports, each named progression
 * SPELLED OUT in the key the panel currently resolves against, so a person can
 * see the chords a progression would write before writing it, and a Write button
 * that dispatches the SAME registered command the headless workflow uses. It adds
 * no music theory of its own - every chord name comes from the engine's answer
 * (`suggestions[]` from chord.progression_list with a `scale`), and the notes are
 * laid down by chord.progression_generate.
 *
 * WHAT IT DOES NOT DO. It does not invent scales or progressions, does not
 * transpose anything, and does not play a preview through the engine: the preview
 * it shows is the chord names the catalogue spells in the selected scale, read
 * from the socket command. Those bounds are stated in docs/CHORD-TRACK.md.
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

#ifndef LMMS_GUI_CHORD_PROGRESSION_PANEL_H
#define LMMS_GUI_CHORD_PROGRESSION_PANEL_H

#include <QDialog>

#include <QJsonArray>

#include "lmms_export.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QTableWidget;

namespace lmms::gui
{

/*! Edit > Progression... (window.progression): the named progressions the
 *  catalogue holds, each spelt in the selected scale, with a Write button that
 *  runs chord.progression_generate on the clip the shell's selection names. The
 *  scale, root and octave are the panel's own choice (seeded from scale.get_state);
 *  they are not written back to the group's context. */
class LMMS_EXPORT ChordProgressionPanel : public QDialog
{
public:
	explicit ChordProgressionPanel(QWidget* parent = nullptr);

	//! Re-read the scale context and the catalogue, refill the selections and the
	//! suggestion rows.
	void refresh();
	//! The scale name, root pitch class and octave the panel currently resolves
	//! against - what a Write would send.
	QString scaleName() const;
	int rootPitchClass() const;
	int octave() const;
	//! Choose a scale by name and a root pitch class (0 = C) and refill. An unknown
	//! or empty scale falls back to the first the catalogue offers; @a root is
	//! clamped into 0..11.
	void setScale(const QString& scale, int root);
	//! One row per named progression: how many, which progression, the chord names
	//! it spells (comma-separated), and whether it fits the selected scale.
	int rowCount() const;
	QString progressionAt(int row) const;
	QString chordsAt(int row) const;
	bool fitsAt(int row) const;
	//! The chord names of \a progression's step, or empty when it does not fit.
	QString chordsFor(const QString& progression) const;
	//! Select the row whose progression is @a name; false when it is absent.
	bool selectProgression(const QString& name);
	//! The MIDI clip the Write button targets: the shell's selected clip, or empty.
	QString targetClip() const;
	//! Write @a progression into @a clipId through chord.progression_generate,
	//! with the panel's selected scale, root and octave. Empty on success, the
	//! refusal's message on failure. The button calls this with targetClip().
	QString writeTo(const QString& clipId, const QString& progression);
	//! Write the selected row into the shell's selected clip (the button's action).
	QString writeSelected();

private:
	//! Fill the scale combo from the catalogue (chord.progression_list `scales`).
	void fillScales(const QJsonArray& scales);
	//! Ask chord.progression_list for the suggestions in the selected scale and
	//! rebuild the table.
	void showSuggestions();
	void showSelected();
	void say(const QString& message);

	QComboBox* m_scale = nullptr;
	QComboBox* m_root = nullptr;
	QSpinBox* m_octave = nullptr;
	QTableWidget* m_table = nullptr;
	QLabel* m_preview = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_target = nullptr;
	QJsonArray m_suggestions;
	bool m_filling = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CHORD_PROGRESSION_PANEL_H
