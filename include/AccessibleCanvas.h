/*
 * AccessibleCanvas.h - custom-painted editing surfaces as a screen reader and a keyboard user meet them
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

#ifndef LMMS_GUI_ACCESSIBLE_CANVAS_H
#define LMMS_GUI_ACCESSIBLE_CANVAS_H

#include <functional>

#include <QAccessible>
#include <QFocusFrame>
#include <QRect>
#include <QString>

#include "lmms_export.h"

class QPainter;

/*! 0.5.0 UX decisions, themes 10, 17 and 23: the editing canvases - the note area, the expression and controller
 *  lanes, the XY pad, the comp strip - are painted by hand, so Qt's bridge (UI Automation, NSAccessibility, AT-SPI)
 *  would call each a nameless "client" with no value. describeCanvas() gives one its role and a value that is read
 *  when asked for (the item the keyboard is on, or a summary), and AccessibleWidgets' factory reports both; a change
 *  of that value is announced the way a knob's is - for the focused canvas only, at most every 250 ms.
 *  The canvas takes keyboard focus (Tab reaches it) and asks for the shared focus ring when the keyboard moved there.
 *
 *  CanvasKeyCursor marks where the keyboard is on a canvas - a note, a lane value, a controller point, a comp range -
 *  with the focus ring's colour and width while the canvas has focus, so the place keys act on is never shown by
 *  colour alone (an outline, not a tint) and needs no change to the canvas's own painting. paintCanvasCues() is the
 *  standard hover and disabled look for a canvas that paints itself (theme 23). */
namespace lmms::a11y
{

//! Make @a canvas a keyboard-reachable control of @a role whose value is @a value() (empty: no value). With
//! @a ring, AccessibleMode's focus ring is laid over it; a canvas that paints its own (the note area) passes false.
LMMS_EXPORT void describeCanvas(QWidget* canvas, QAccessible::Role role, std::function<QString()> value, bool ring = true);
//! The role describeCanvas gave @a widget, or QAccessible::NoRole when it was not described.
LMMS_EXPORT QAccessible::Role canvasRole(const QWidget* widget);
//! The value describeCanvas gave @a widget now, or a null string when it was not described.
LMMS_EXPORT QString canvasValue(const QWidget* widget);

//! The hover and disabled cues every self-painted canvas shares: under the pointer, a hairline in the hover colour;
//! disabled, the surface dimmed and a dashed outline (not colour alone). Paint it last.
LMMS_EXPORT void paintCanvasCues(QPainter& painter, const QWidget* canvas);
//! The keyboard mark itself: @a rect outlined in the focus ring's colour and width.
LMMS_EXPORT void paintKeyMark(QPainter& painter, const QRect& rect);

//! The keyboard mark laid over a canvas that does not paint it itself, with the canvas's hover and disabled cues. A focus frame, so Qt's accessibility leaves
//! it out of the tree (as AccessibleMode's focus ring); clicks pass through it.
class LMMS_EXPORT CanvasKeyCursor : public QFocusFrame
{
public:
	explicit CanvasKeyCursor(QWidget* canvas);
	//! The canvas's cursor, for @a canvas (made on first use).
	static CanvasKeyCursor* of(QWidget* canvas);
	//! Mark what @a where answers (canvas coordinates), asked again at every repaint - the canvas's repaints repaint
	//! the mark, so it follows a scroll or a zoom; an empty rect marks nothing.
	void setMark(std::function<QRect()> where);
	QRect mark() const { return m_where ? m_where() : QRect(); }

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	std::function<QRect()> m_where;
};

} // namespace lmms::a11y

#endif // LMMS_GUI_ACCESSIBLE_CANVAS_H
