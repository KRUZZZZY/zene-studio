/*
 * StepGridPanel.h - the melodic step grid: a MIDI clip's notes as a grid of steps and pitches,
 *                   edited through note.step_set (Q050-294.2, stage 2 of 3)
 *
 * WHAT IT IS. The interface half of the melodic step state (Q050-294.1). Columns are steps of
 * the chosen resolution, rows are pitches, and every lit cell is one ORDINARY note of the clip -
 * the panel stores nothing of its own. It reads the clip with note.step_get_state and writes it
 * with note.step_set, the same two registered commands an agent uses, so the piano roll, the
 * socket and the grid always show one clip. Each gesture is one note.step_set call, so one
 * control.undo takes it back.
 *
 * WHAT IT DOES NOT DO. No music theory of its own: whether a row is in the scale, and which key
 * a written pitch snaps to, come from the commands' answers (the `scale` block's pitch classes and
 * note.step_set's scale_mode snap). Shape and random fills run through note.step_fill, one checkpoint
 * each, so one Undo takes a fill back; the grid still stores nothing of its own. No live step input.
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

#ifndef LMMS_GUI_STEP_GRID_PANEL_H
#define LMMS_GUI_STEP_GRID_PANEL_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSpinBox>
#include <QVector>
#include <QWidget>

#include "lmms_export.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace lmms
{
class MidiClip;
}

namespace lmms::gui
{

class StepGridPanel;

//! The grid itself: paints the panel's rows and steps and turns a click into a cell gesture.
class LMMS_EXPORT StepGridCanvas : public QWidget
{
public:
	static constexpr int KeyColumn = 44;  //!< width of the key names on the left
	static constexpr int Ruler = 18;      //!< height of the step numbers on top
	static constexpr int CellWidth = 24;
	static constexpr int CellHeight = 16;

	explicit StepGridCanvas(StepGridPanel* panel);
	//! The rectangle of cell (@a step, @a key) in this widget; empty when the key is not a row.
	QRect cellRect(int step, int key) const;
	//! Re-size to the panel's rows and steps and repaint.
	void relayout();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	void paintRows(QPainter& p) const;
	void paintNotes(QPainter& p) const;
	void paintNote(QPainter& p, const QJsonObject& note) const;

	StepGridPanel* m_panel;
};

//! The pitch field: its arrows and wheel move to the next key IN the scale (every key with none),
//! so stepping never lands on a key note.step_set would snap straight back.
class LMMS_EXPORT ScaleKeySpinBox : public QSpinBox
{
public:
	explicit ScaleKeySpinBox(QWidget* parent) : QSpinBox(parent) {}
	//! The scale's pitch classes (0 = C); empty means every key is a step.
	void setPitchClasses(const QVector<int>& classes) { m_classes = classes; }
	void stepBy(int steps) override;

private:
	QVector<int> m_classes;
};

/*! Edit > Step Grid... and the note editor's Notes > Tools > Open in Step Grid (window.step_grid).
 *  Left click on an empty cell adds a note there (snapped into the scale), on a note selects it;
 *  right click removes the note. The inspector edits the selected note's pitch, velocity, chance
 *  and ratchet. Every one of those is one note.step_set call and one undo step. */
class LMMS_EXPORT StepGridPanel : public QDialog
{
public:
	explicit StepGridPanel(QWidget* parent = nullptr);

	//! Open @a clipId (a MIDI clip) in the grid. Empty on success, the refusal otherwise.
	QString setClip(const QString& clipId);
	QString clipId() const { return m_clipId; }
	//! The grid resolution in ticks (12 = a sixteenth); the resolution box's value.
	int stepTicks() const;
	bool setStepTicks(int ticks);
	//! The scale the grid reads against: "" follows the project key (scale.set), "none" is no
	//! scale, any other name is that scale on @a root (0 = C). False for an unknown name.
	bool setScaleChoice(const QString& scale, int root);
	QString scaleChoice() const;
	//! Show only the scale's rows (plus the row of every note, so none is hidden).
	void setScaleRowsOnly(bool only);
	//! Re-read the clip (note.step_get_state) and repaint. Runs on every change of the clip.
	void refresh();

	//! What the grid shows: the last note.step_get_state answer and the rows, highest key first.
	const QJsonObject& state() const { return m_state; }
	const QVector<int>& rows() const { return m_rows; }
	int steps() const;
	bool inScale(int key) const;
	//! The note shown in cell (@a step, @a key), or {}.
	QJsonObject noteAt(int step, int key) const;
	StepGridCanvas* canvas() const { return m_canvas; }
	//! The panel's state for window.step_grid: clip, resolution, scale, rows, cells and selection.
	QJsonObject describe() const;

	//! The selected note's id ("" for none) and selecting one by id.
	QString selectedNote() const { return m_selected; }
	bool selectNote(const QString& noteId);

	//! The gestures. Each is ONE note.step_set call; empty on success, the refusal otherwise.
	QString clickCell(int step, int key);
	QString removeCell(int step, int key);
	QString editSelected(const QJsonObject& fields);

private:
	void buildHeader(QVBoxLayout* layout);
	void buildInspector(QVBoxLayout* layout);
	void buildFills(QVBoxLayout* layout);
	//! Enables the Fill row's controls: all need a clip, the contour only for shape, the density only for random.
	void showFill();
	//! note.step_fill with the row's kind, contour, seed, spread and density plus the grid's own clip and scale.
	void fillSteps();
	void fillScales();
	void watchClip(MidiClip* clip);
	QJsonObject readArgs() const;
	QString runSet(const QJsonArray& edits, QJsonObject* reply = nullptr);
	void computeRows();
	void showSelected();
	void say(const QString& message);

	QString m_clipId;
	QPointer<MidiClip> m_clip;
	QJsonObject m_state;
	QVector<int> m_rows;
	QVector<int> m_classes;  //!< the scale's pitch classes from the last read; empty = no scale
	QString m_selected;
	bool m_scaleRowsOnly = false;
	bool m_filling = false;

	StepGridCanvas* m_canvas = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_status = nullptr;
	QComboBox* m_resolution = nullptr;
	QComboBox* m_scale = nullptr;
	QComboBox* m_root = nullptr;
	QCheckBox* m_rowsOnly = nullptr;
	ScaleKeySpinBox* m_key = nullptr;
	QSpinBox* m_velocity = nullptr;
	QSpinBox* m_chance = nullptr;
	QSpinBox* m_ratchet = nullptr;
	QPushButton* m_remove = nullptr;
	QComboBox* m_fillKind = nullptr;
	QComboBox* m_fillContour = nullptr;
	QSpinBox* m_fillSeed = nullptr;
	QSpinBox* m_fillSpread = nullptr;
	QSpinBox* m_fillDensity = nullptr;
	QPushButton* m_fill = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_STEP_GRID_PANEL_H
