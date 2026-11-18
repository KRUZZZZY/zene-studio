/*
 * NoteEditorParts.h - the note editor's key column, ruler and expression lane
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

#ifndef LMMS_GUI_NOTE_EDITOR_PARTS_H
#define LMMS_GUI_NOTE_EDITOR_PARTS_H

#include <map>

#include <QPoint>
#include <QWidget>

#include "lmms_export.h"

class QPainter;

namespace lmms
{
class Note;
}

namespace lmms::gui
{

class NoteEditor;
struct LaneScale;
struct NoteEditorState;

//! The key column: one row per visible key, the C of each octave named, the in-scale keys
//! marked. A press auditions the key through the clip's instrument.
class LMMS_EXPORT NoteKeyboardView : public QWidget
{
	Q_OBJECT

public:
	explicit NoteKeyboardView(NoteEditor* editor);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;

private:
	//! A row's label: the track's name for @a key (key_names.*) at any row height, else the
	//! key's note name on every C and wherever the rows are tall enough.
	void paintLabel(QPainter& p, const QRect& row, int key) const;

	NoteEditor* m_editor;
	NoteEditorState& m_state;
	int m_pressed = -1;
	int m_zoomY = -1;     //!< FL: a middle-button vertical drag on the keys zooms the rows - where it started
	int m_zoomRows = 0;   //!< ... and the row height then
	int m_zoomKey = -1;   //!< ... and the key under it, kept under the pointer
};

//! The bar ruler, with the clip's loop region - the span the clip plays and repeats - drawn
//! as an accent bar whose end can be dragged to resize the clip (clip.resize).
class LMMS_EXPORT NoteRuler : public QWidget
{
	Q_OBJECT

public:
	explicit NoteRuler(NoteEditor* editor);
	QSize sizeHint() const override;
	//! Drop a clip-end drag in flight (the clip changed or went): the release resizes nothing.
	void cancelDrag();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	int loopEndX() const;
	bool overLoopEnd(int x) const;  //!< whether x is on the clip end's drag target
	int draggedLength(int x) const;

	NoteEditor* m_editor;
	NoteEditorState& m_state;
	bool m_dragging = false;
	int m_dragX = 0;
};

//! The expression lane below the notes: one bar per visible note for the chosen lane
//! (velocity, pan, chance, timbre, pressure, micro-pitch, release velocity, velocity spread,
//! repeats). Dragging across notes draws values; on release they are written as one note.edit
//! (velocity, pan, chance, release velocity, spread, repeats) or one
//! note.expression_set per note (the MPE axes, which note.edit does not carry).
class LMMS_EXPORT NoteLaneView : public QWidget
{
	Q_OBJECT

public:
	explicit NoteLaneView(NoteEditor* editor);
	QSize sizeHint() const override;
	//! The value a note shows, including an uncommitted drag's.
	int shownValue(const Note& note) const;
	//! Drop a stroke in flight (the clip changed or went): the release writes nothing.
	void cancelDrag();
	//! 0.5.0 theme 11: what the lane reads out now - the value under the pointer (and the note there) while it
	//! hovers or draws, else the selected notes' values ("" with neither). The header shows the selection's.
	QString readout() const;
	QString selectionReadout() const;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	int valueAtY(int y) const;
	int yOfValue(int value) const;
	void drawAt(const QPoint& pos);
	//! drawAt for every point of the path from @a from to @a to, so a fast stroke takes the stems it passed over.
	void drawAlong(const QPoint& from, const QPoint& to);
	void commit();
	//! The stems, each in its value's place, its state in its head's shape (NoteLanePaint.cpp).
	void paintStems(QPainter& p, int baseY) const;
	//! The visible note whose stem is nearest @a x within the stem tolerance, or null.
	const Note* stemAt(int x) const;
	QString pointerReadout(const LaneScale& scale) const;

	NoteEditor* m_editor;
	NoteEditorState& m_state;
	std::map<int, int> m_pending;  //!< Note::id() -> value drawn but not yet written
	int m_lastValue = 0;           //!< the value the stroke drew last: the new notes' velocity on the Velocity lane
	bool m_drawing = false;
	QPoint m_drawAt{-1, -1};       //!< where the stroke in flight last drew
	QPoint m_hover{-1, -1};        //!< the pointer over the lane, or x < 0 when it is elsewhere
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_EDITOR_PARTS_H
