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

namespace lmms::gui
{

//! The square: a dot at (x, y) in 0..1 (y up); a press or a drag moves it and reports the new place.
class LMMS_EXPORT XyPadArea : public QWidget
{
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
 *  macro, Y the other, each through rack.macro_set (so an agent's macro edits and the pad are one
 *  path, and every bound parameter follows). */
class LMMS_EXPORT XyPadDialog : public QDialog
{
public:
	XyPadDialog(const QString& channel, QWidget* parent = nullptr);

private:
	void fillMacros();
	void send(QPointF point);
	QString m_channel;
	QComboBox* m_x;
	QComboBox* m_y;
	XyPadArea* m_area;
};

} // namespace lmms::gui

#endif // LMMS_GUI_XY_PAD_H
