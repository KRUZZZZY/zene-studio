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

#include <cmath>

#include <QCursor>
#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#ifndef QT_NO_GESTURES
#include <QGestureEvent>
#include <QPinchGesture>
#endif

#include "ArrangerZoom.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "GuiApplication.h"
#include "SongEditor.h"
#include "TrackContainerView.h"
#include "TouchInput.h"

namespace lmms::gui
{

namespace
{

constexpr int PollMs = 33;          //!< about 30 times a second, only while shown

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

#ifndef QT_NO_GESTURES
/*! A pinch on the arranger. 0.5.0 theme 9: it zooms continuously (as finely as the zoom model goes) around
 *  the tick under the pinch's centre where it began, and the zoom it started from joins the history
 *  arrangement.zoom_back walks once, when it ends or is canceled - not one entry per step. */
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
		SongEditor* editor = getGUI() != nullptr && getGUI()->songEditor() != nullptr ? getGUI()->songEditor()->m_editor : nullptr;
		if (pinch == nullptr || editor == nullptr) { return false; }
		if (pinch->state() == Qt::GestureStarted) { begin(editor, pinch); }
		m_scale *= pinch->scaleFactor();
		const int value = editor->zoomValueFor(static_cast<int>(std::lround(m_startPixelsPerBar * m_scale)));
		if (value != editor->zoomingModel()->value()) { arrangerzoom::zoomAround(editor, value, m_anchor); }
		arrangerzoom::finishPinch(m_startValue, editor->zoomingModel()->value(), pinch->state());
		return true;
	}

private:
	void begin(SongEditor* editor, const QPinchGesture* pinch)
	{
		m_scale = 1.0;
		m_startPixelsPerBar = editor->pixelsPerBar();
		m_startValue = editor->zoomingModel()->value();
		m_anchor = arrangerzoom::tickAtGlobal(editor, pinch->startCenterPoint().toPoint());
	}

	qreal m_scale = 1.0;
	double m_startPixelsPerBar = 1.0;
	int m_startValue = 0;
	tick_t m_anchor = 0;
};
#endif // QT_NO_GESTURES

QPointer<CrosshairOverlay> s_overlay;
bool s_shown = false;

} // namespace

void installArrangerAids(QWidget* arranger)
{
	if (arranger == nullptr || !s_overlay.isNull()) { return; }
	s_overlay = new CrosshairOverlay(arranger);
#ifndef QT_NO_GESTURES
	new PinchZoom(arranger);
#endif
	// A tap or one-finger drag selects clips: install where the content is, so the finger's event is
	// delivered to the viewport under it rather than swallowed by the arranger that accepts touch for
	// the pinch above.
	auto* container = qobject_cast<TrackContainerView*>(arranger);
	touch::installSingleTouch(container != nullptr && container->contentWidget() != nullptr
		? container->contentWidget()->viewport() : arranger);
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
