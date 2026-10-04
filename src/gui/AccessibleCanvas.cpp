/*
 * AccessibleCanvas.cpp - custom-painted editing surfaces as a screen reader and a keyboard user meet them
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

#include "AccessibleCanvas.h"

#include <QEvent>
#include <QPainter>

#include "AccessibleWidgets.h"
#include "DesignTokens.h"

namespace lmms::a11y
{

namespace
{

const char* const DescriptionName = "zeneCanvasDescription";
const char* const CursorName = "zeneCanvasKeyCursor";

//! What describeCanvas() keeps on the canvas: its role and how to read its value. A child object, so it goes with
//! the canvas and needs no member in the canvas's class.
class CanvasDescription : public QObject
{
public:
	CanvasDescription(QWidget* canvas, QAccessible::Role role, std::function<QString()> value)
		: QObject(canvas)
		, m_role(role)
		, m_value(std::move(value))
	{
		setObjectName(QLatin1String(DescriptionName));
	}

	QAccessible::Role role() const { return m_role; }
	QString value() const { return m_value ? m_value() : QString(); }

private:
	QAccessible::Role m_role;
	std::function<QString()> m_value;
};

const CanvasDescription* descriptionOf(const QWidget* widget)
{
	return widget != nullptr ? widget->findChild<CanvasDescription*>(QLatin1String(DescriptionName), Qt::FindDirectChildrenOnly)
		: nullptr;
}

} // namespace

void describeCanvas(QWidget* canvas, QAccessible::Role role, std::function<QString()> value, bool ring)
{
	if (canvas == nullptr) { return; }
	delete descriptionOf(canvas);  // described again: the newest description counts
	new CanvasDescription(canvas, role, std::move(value));
	if ((canvas->focusPolicy() & Qt::TabFocus) == 0) { canvas->setFocusPolicy(Qt::StrongFocus); }
	if (ring) { canvas->setProperty("zeneFocusRing", true); }
	installAccessibleWidgets();  // the factory that reports the role and the value
	// Qt keeps the first interface it made for a widget, and makes one whenever a property event names it - an
	// accessible name set in the canvas's constructor, before this, made a nameless "client". Drop that one, so
	// the next ask goes to the factory.
	QAccessibleInterface* made = QAccessible::queryAccessibleInterface(canvas);
	if (made != nullptr && made->role() != role) { QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(made)); }
}

QAccessible::Role canvasRole(const QWidget* widget)
{
	const CanvasDescription* description = descriptionOf(widget);
	return description != nullptr ? description->role() : QAccessible::NoRole;
}

QString canvasValue(const QWidget* widget)
{
	const CanvasDescription* description = descriptionOf(widget);
	return description != nullptr ? description->value() : QString();
}

void paintCanvasCues(QPainter& painter, const QWidget* canvas)
{
	const gui::tokens::Palette& pal = gui::tokens::palette();
	painter.save();
	painter.setBrush(Qt::NoBrush);
	if (!canvas->isEnabled())
	{
		QColor dim = pal.panel;
		dim.setAlpha(150);
		painter.fillRect(canvas->rect(), dim);
		painter.setPen(QPen(pal.textDisabled, 1, Qt::DashLine));
		painter.drawRect(canvas->rect().adjusted(0, 0, -1, -1));
	}
	else if (canvas->underMouse() && !canvas->hasFocus())
	{
		painter.setPen(QPen(pal.controlHover, 1));
		painter.drawRect(canvas->rect().adjusted(0, 0, -1, -1));
	}
	painter.restore();
}

void paintKeyMark(QPainter& painter, const QRect& rect)
{
	if (rect.isEmpty()) { return; }
	const qreal width = gui::tokens::focusRingWidth();
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(gui::tokens::palette().focusRing, width));
	painter.setBrush(Qt::NoBrush);
	const qreal inset = width / 2.0;
	painter.drawRoundedRect(QRectF(rect).adjusted(inset, inset, -inset, -inset), gui::tokens::radius::sm,
		gui::tokens::radius::sm);
	painter.restore();
}

CanvasKeyCursor::CanvasKeyCursor(QWidget* canvas)
	: QFocusFrame(canvas)
{
	setObjectName(QLatin1String(CursorName));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setFocusPolicy(Qt::NoFocus);
	setGeometry(canvas->rect());
	canvas->setAttribute(Qt::WA_Hover);  // the canvas repaints, and with it the hover cue, as the pointer comes and goes
	canvas->installEventFilter(this);
	show();
}

CanvasKeyCursor* CanvasKeyCursor::of(QWidget* canvas)
{
	auto* cursor = canvas->findChild<CanvasKeyCursor*>(QLatin1String(CursorName), Qt::FindDirectChildrenOnly);
	return cursor != nullptr ? cursor : new CanvasKeyCursor(canvas);
}

void CanvasKeyCursor::setMark(std::function<QRect()> where)
{
	m_where = std::move(where);
	update();
}

bool CanvasKeyCursor::eventFilter(QObject* watched, QEvent* event)
{
	// It covers the canvas whatever its size, stays above its other children, and shows only while it has focus.
	if (watched == parent())
	{
		const QEvent::Type type = event->type();
		if (type == QEvent::Resize) { setGeometry(parentWidget()->rect()); }
		if (type == QEvent::FocusIn || type == QEvent::FocusOut || type == QEvent::ChildAdded || type == QEvent::EnabledChange)
		{
			raise();
			update();
		}
	}
	return false;
}

void CanvasKeyCursor::paintEvent(QPaintEvent*)
{
	// Theme 23: the canvas's hover and disabled cues, then - while it has the focus - where the keys act.
	if (parentWidget() == nullptr) { return; }
	QPainter painter(this);
	paintCanvasCues(painter, parentWidget());
	if (parentWidget()->hasFocus()) { paintKeyMark(painter, mark()); }
}

} // namespace lmms::a11y
