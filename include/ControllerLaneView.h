/*
 * ControllerLaneView.h - the note editor's controller lane: a clip's CC, pitch-bend or aftertouch curve
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

#ifndef LMMS_GUI_CONTROLLER_LANE_VIEW_H
#define LMMS_GUI_CONTROLLER_LANE_VIEW_H

#include <map>

#include <QWidget>

#include "lmms_export.h"

class QPainter;

namespace lmms
{
struct ControllerLane;
}

namespace lmms::gui
{

class NoteEditor;
struct NoteEditorState;

/*! 0.5.0 L07 (ED-130): the lane under the notes when the chooser names a controller - the clip's
 *  curve for that CC, pitch bend or aftertouch, drawn over the same time axis as the notes. A drag
 *  draws a point on every grid step it crosses (Alt, or the right button, erases the span instead);
 *  the release sends ONE clip.controllers_set, so a stroke is one undo step and an agent's edit is
 *  the same call. */
class LMMS_EXPORT ControllerLaneView : public QWidget
{
	Q_OBJECT

public:
	explicit ControllerLaneView(NoteEditor* editor);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	const ControllerLane* lane() const;
	int maximum() const;
	int valueAtY(int y) const;
	int yOfValue(int value) const;
	int snappedTick(int x) const;
	void paintCurve(QPainter& p, const ControllerLane& shown) const;
	void paintStroke(QPainter& p) const;
	void drawTo(const QPoint& pos);
	void commit();

	NoteEditor* m_editor;
	NoteEditorState& m_state;
	std::map<int, int> m_drawn;  //!< tick -> value of the stroke in progress
	int m_firstTick = -1;
	int m_lastTick = -1;
	bool m_drawing = false;
	bool m_erasing = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CONTROLLER_LANE_VIEW_H
