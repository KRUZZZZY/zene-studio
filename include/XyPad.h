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
//! 0.5.0 theme 10: the keys move it too - Left / Right and Up / Down 1 % (with Shift, or Page Up / Page Down for Y,
//! 10 %), Home / End put X at its ends; each press reports the new place like a drag step. A screen reader reads it
//! as a slider whose value is both axes ("X Cutoff 30 %, Y Drive 90 %"); Ctrl+Shift+L opens the editable axis list.
class LMMS_EXPORT XyPadArea : public QWidget
{
	Q_OBJECT
public:
	explicit XyPadArea(QWidget* parent = nullptr);
	void setPoint(QPointF point);
	QPointF point() const { return m_point; }
	//! The names the value is read with ("Cutoff", "Drive").
	void setAxisNames(const QString& x, const QString& y);
	QString accessibleValue() const;
	std::function<void(QPointF)> moved;
	QSize sizeHint() const override { return {220, 220}; }

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	void moveTo(const QPoint& at);
	//! The place a key moves the dot to, or the current one for a key the pad does not use.
	QPointF keyed(const QKeyEvent* event) const;
	QPointF m_point{0.5, 0.5};
	QString m_xName;
	QString m_yName;
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

	//! What the editable axis list (theme 10) reads and writes: the rack's channel, an axis's macro id
	//! ("macro-<n>", empty when the rack has none) and name, and the pad.
	QString channel() const { return m_channel; }
	QString macroId(bool y) const;
	QString macroName(bool y) const;
	XyPadArea* area() const { return m_area; }
	//! Show where the chosen macros stand now (after an edit made elsewhere).
	void refresh() { syncPoint(); }

signals:
	//! The dot moved or the axes changed: the list re-reads.
	void changed();

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
