/*
 * TouchInput.cpp - a single finger acts as the left mouse button
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

#include "TouchInput.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTouchEvent>
#include <QWidget>

namespace lmms::gui::touch
{

namespace
{

//! Qt 6 renamed QTouchEvent::touchPoints() to points() and QTouchEvent::TouchPoint to QEventPoint.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using TouchPoint = QEventPoint;
using TouchPointList = QList<QEventPoint>;
QPoint positionOf(const TouchPoint& point) { return point.position().toPoint(); }
const TouchPointList& touchPoints(const QTouchEvent* event) { return event->points(); }
#else
using TouchPoint = QTouchEvent::TouchPoint;
using TouchPointList = QList<QTouchEvent::TouchPoint>;
QPoint positionOf(const TouchPoint& point) { return point.pos().toPoint(); }
const TouchPointList& touchPoints(const QTouchEvent* event) { return event->touchPoints(); }
#endif

bool isTouch(QEvent::Type type)
{
	return type == QEvent::TouchBegin || type == QEvent::TouchUpdate || type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel;
}

/*! One finger's touch sequence, as a mouse gesture. The press goes to the deepest visible child under
 *  the finger and every later event of the sequence goes to the same child - the implicit grab a mouse
 *  press has - so a drag started on a clip keeps reaching that clip. */
class SingleTouch final : public QObject
{
public:
	explicit SingleTouch(QWidget* target) :
		QObject(target),
		m_target(target)
	{
		m_target->setAttribute(Qt::WA_AcceptTouchEvents, true);
		m_target->setProperty("zeneSingleTouch", true);
		m_target->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched != m_target || !isTouch(event->type())) { return false; }
		auto* touch = static_cast<QTouchEvent*>(event);
		switch (event->type())
		{
		case QEvent::TouchBegin: return begin(touch);
		case QEvent::TouchUpdate: return update(touch);
		case QEvent::TouchEnd: return end(touch);
		case QEvent::TouchCancel: cancel(); return !m_suppressed;
		default: return false;
		}
	}

private:
	bool begin(QTouchEvent* event)
	{
		reset();
		if (touchPoints(event).size() != 1)
		{
			// Two fingers from the start: a gesture (the arranger's pinch), not a mouse click.
			m_suppressed = true;
			return false;
		}
		m_pressed = true;
		m_start = m_last = positionOf(touchPoints(event).first());
		m_receiver = receiverAt(m_start);
		send(QEvent::MouseButtonPress, m_start, Qt::LeftButton, Qt::LeftButton);
		return true;
	}

	bool update(QTouchEvent* event)
	{
		if (m_suppressed) { return false; }
		if (touchPoints(event).size() != 1)
		{
			// A second finger turns the sequence into a gesture: drop the mouse press it started with
			// and let Qt's recognisers have the rest.
			cancel();
			m_suppressed = true;
			return false;
		}
		const QPoint at = positionOf(touchPoints(event).first());
		if (!m_dragging && (at - m_start).manhattanLength() >= QApplication::startDragDistance())
		{
			m_dragging = true;
		}
		if (m_dragging) { send(QEvent::MouseMove, at, Qt::NoButton, Qt::LeftButton); }
		m_last = at;
		return true;
	}

	bool end(QTouchEvent* event)
	{
		const bool consumed = !m_suppressed && m_pressed;
		if (consumed)
		{
			const QPoint at = touchPoints(event).isEmpty() ? m_last : positionOf(touchPoints(event).last());
			send(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
		}
		reset();
		return consumed;
	}

	void cancel()
	{
		if (m_pressed && !m_suppressed) { send(QEvent::MouseButtonRelease, m_last, Qt::LeftButton, Qt::NoButton); }
		m_pressed = false;
		m_dragging = false;
		m_receiver.clear();
	}

	void reset()
	{
		cancel();
		m_suppressed = false;
	}

	//! The widget a mouse press at @a rootPos reaches: the deepest visible child under it, as Qt's own
	//! hit test does. @a rootPos is in this filter's target's coordinates.
	QWidget* receiverAt(QPoint rootPos) const
	{
		QWidget* widget = m_target;
		QPoint at = rootPos;
		for (;;)
		{
			QWidget* child = widget->childAt(at);
			if (child == nullptr || child == widget || !child->isVisible()
				|| child->testAttribute(Qt::WA_TransparentForMouseEvents))
			{
				break;
			}
			at = child->mapFrom(widget, at);
			widget = child;
		}
		return widget;
	}

	//! Deliver @a type as a left-button mouse event to the sequence's receiver, in its own coordinates.
	void send(QEvent::Type type, const QPoint& rootPos, Qt::MouseButton button, Qt::MouseButtons buttons)
	{
		QWidget* receiver = m_receiver.data();
		if (receiver == nullptr) { return; }
		const QPoint global = m_target->mapToGlobal(rootPos);
		const QPointF local = receiver->mapFromGlobal(global);
		QMouseEvent mouse(type, local, QPointF(global), button, buttons, Qt::NoModifier);
		QCoreApplication::sendEvent(receiver, &mouse);
	}

	QPointer<QWidget> m_target;
	QPointer<QWidget> m_receiver;
	QPoint m_start;
	QPoint m_last;
	bool m_pressed = false;
	bool m_dragging = false;
	bool m_suppressed = false;  //!< a multi-finger sequence: a gesture owns it until it ends
};

} // namespace

void installSingleTouch(QWidget* target)
{
	if (target == nullptr || target->property("zeneSingleTouch").toBool()) { return; }
	new SingleTouch(target);
}

bool handlesSingleTouch(const QWidget* target)
{
	return target != nullptr && target->property("zeneSingleTouch").toBool();
}

} // namespace lmms::gui::touch
