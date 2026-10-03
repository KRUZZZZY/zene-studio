/*
 * ArrangerAids.cpp - the arranger's cross-hair cursor and pinch to zoom
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

#include "ArrangerAids.h"

#include <QCursor>
#include <QEvent>
#include <QGestureEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "ControlRegistry.h"
#include "DesignTokens.h"

namespace lmms::gui
{

namespace
{

constexpr int PollMs = 33;          //!< about 30 times a second, only while shown
constexpr qreal PinchStep = 1.25;   //!< the pinch scale that is one zoom step

//! The guides: a transparent child over the whole arranger that follows the pointer. It never takes a
//! mouse event, so the arranger's own gestures are untouched.
class CrosshairOverlay : public QWidget
{
public:
	explicit CrosshairOverlay(QWidget* arranger) :
		QWidget(arranger),
		m_timer(new QTimer(this))
	{
		setObjectName(QStringLiteral("arrangerCrosshair"));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setFocusPolicy(Qt::NoFocus);
		arranger->installEventFilter(this);
		connect(m_timer, &QTimer::timeout, this, [this] { follow(); });
		hide();
	}

	void setShown(bool shown)
	{
		setGeometry(parentWidget()->rect());
		setVisible(shown);
		raise();
		if (shown) { m_timer->start(PollMs); } else { m_timer->stop(); }
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == parentWidget() && event->type() == QEvent::Resize) { setGeometry(parentWidget()->rect()); }
		return false;
	}

	void paintEvent(QPaintEvent*) override
	{
		if (!rect().contains(m_at)) { return; }
		QPainter p(this);
		QColor line = tokens::palette().playhead;
		line.setAlpha(110);
		p.setPen(QPen(line, 1, Qt::DashLine));
		p.drawLine(m_at.x(), 0, m_at.x(), height());
		p.drawLine(0, m_at.y(), width(), m_at.y());
	}

private:
	void follow()
	{
		const QPoint at = mapFromGlobal(QCursor::pos());
		const QPoint next = rect().contains(at) ? at : QPoint(-1, -1);
		if (next == m_at) { return; }
		m_at = next;
		update();
	}

	QTimer* m_timer;
	QPoint m_at{-1, -1};
};

//! A pinch on the arranger: a zoom step each time the scale passes another quarter in or out.
class PinchZoom : public QObject
{
public:
	explicit PinchZoom(QWidget* arranger) :
		QObject(arranger)
	{
		arranger->setAttribute(Qt::WA_AcceptTouchEvents);
		arranger->grabGesture(Qt::PinchGesture);
		arranger->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject*, QEvent* event) override
	{
		if (event->type() != QEvent::Gesture) { return false; }
		auto* pinch = static_cast<QPinchGesture*>(static_cast<QGestureEvent*>(event)->gesture(Qt::PinchGesture));
		if (pinch == nullptr) { return false; }
		if (pinch->state() == Qt::GestureStarted) { m_scale = 1.0; }
		m_scale *= pinch->scaleFactor();
		zoomWhilePassing();
		return true;
	}

private:
	void zoomWhilePassing()
	{
		while (m_scale >= PinchStep || m_scale <= 1.0 / PinchStep)
		{
			const bool in = m_scale >= PinchStep;
			m_scale = in ? m_scale / PinchStep : m_scale * PinchStep;
			ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), in ? 1 : -1}});
		}
	}

	qreal m_scale = 1.0;
};

QPointer<CrosshairOverlay> s_overlay;
bool s_shown = false;

} // namespace

void installArrangerAids(QWidget* arranger)
{
	if (arranger == nullptr || !s_overlay.isNull()) { return; }
	s_overlay = new CrosshairOverlay(arranger);
	new PinchZoom(arranger);
	s_overlay->setShown(s_shown);
}

void showCrosshair(bool shown)
{
	s_shown = shown;
	if (!s_overlay.isNull()) { s_overlay->setShown(shown); }
}

bool crosshairShown()
{
	return s_shown;
}

} // namespace lmms::gui
