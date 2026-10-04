/*
 * XyPad.h - two rack macros on one square pad
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
#ifndef LMMS_GUI_XY_PAD_H
#define LMMS_GUI_XY_PAD_H

#include <functional>

#include <QDialog>
#include <QPointF>
#include <QString>
#include <QWidget>

#include "lmms_export.h"

class QComboBox;
class QLabel;

namespace lmms::gui
{

//! The square: a dot at (x, y) in 0..1 (y up); a press or a drag moves it and reports the new place.
class LMMS_EXPORT XyPadArea : public QWidget
{
	Q_OBJECT
public:
	explicit XyPadArea(QWidget* parent = nullptr);
	void setPoint(QPointF point);
	QPointF point() const { return m_point; }
	std::function<void(QPointF)> moved;
	QSize sizeHint() const override { return {220, 220}; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;

private:
	void moveTo(const QPoint& at);
	QPointF m_point{0.5, 0.5};
};

/*! 0.5.0 L10 (MX-110): a mixer channel rack's two macros played together on one pad - X drives one
 *  macro, Y the other, both through ONE rack.macro_set_pair per move (W1: a drag is one undo step, and
 *  an agent's macro edits and the pad are one path, every bound parameter following). When both axes
 *  name the same macro, X drives it alone through rack.macro_set. A refused move is shown on the
 *  status line under the pad. */
class LMMS_EXPORT XyPadDialog : public QDialog
{
	Q_OBJECT
public:
	XyPadDialog(const QString& channel, QWidget* parent = nullptr);

private:
	void fillMacros();
	//! The dot at the chosen macros' current values, and the pad's tooltip for the choice.
	void syncPoint();
	void send(QPointF point);
	QString m_channel;
	QComboBox* m_x;
	QComboBox* m_y;
	XyPadArea* m_area;
	//! W1: the last refused move's reason, cleared by the next move that lands (one line, never a box per move).
	QLabel* m_status;
};

} // namespace lmms::gui

#endif // LMMS_GUI_XY_PAD_H
