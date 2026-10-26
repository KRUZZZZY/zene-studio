/*
 * SampleSelectionView.cpp - the sample editor's waveform: a selection with handles, zoomed and scrolled
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

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "AccessibleCanvas.h"
#include "DesignTokens.h"
#include "SampleClip.h"
#include "SampleEditorDialog.h"

namespace lmms::gui
{

SampleSelectionView::SampleSelectionView(QWidget* parent) :
	QWidget(parent)
{
	setMinimumHeight(120);
	setMouseTracking(true);  // the cursor says when it is over an edge's handle
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(tr("Waveform"));
	setToolTip(tr("Drag across the waveform to select a range, or drag an edge's handle to move that edge; Ctrl+wheel "
		"zooms around the pointer and the wheel scrolls. From the keyboard: Left/Right move the cursor, Shift+Left/Right "
		"extend the selection, Home/End go to the ends, Ctrl+Plus/Minus zoom, Ctrl+0 shows all and Ctrl+A selects all. "
		"Each button below edits the clip's audio over the range, one undo step per edit."));
	a11y::describeCanvas(this, QAccessible::Canvas, [this] { return readout(); });
	a11y::CanvasKeyCursor::of(this)->setMark([this] {
		return QRect(xOf(to()) - 2, 1, 5, height() - 2);
	});
}

void SampleSelectionView::setClip(SampleClip* clip)
{
	const auto frameCount = clip != nullptr
		? sampleeditor::detail::representableFrameCount(clip->sample().sampleSize()) : std::optional<int>{0};
	const int frames = frameCount.value_or(0);
	// The same clip at the same length (after a gain, silence, reverse or fade) keeps its selection and its
	// zoom, so the next action applies to it too; a new clip or length starts afresh (review L17 #5).
	if (clip != m_clip || frames != m_frames)
	{
		m_anchor = m_end = 0;
		m_viewFrom = m_viewSpan = 0;
	}
	m_clip = clip;
	m_frames = frames;
	m_unsupportedFrameCount = clip != nullptr && !frameCount.has_value();
	m_thumbnail = clip != nullptr && !m_unsupportedFrameCount ? SampleThumbnail(clip->sample()) : SampleThumbnail();
	update();
	emit selectionChanged();
}

void SampleSelectionView::setRange(int from, int to)
{
	const int a = std::clamp(std::min(from, to), 0, m_frames);
	const int b = std::clamp(std::max(from, to), 0, m_frames);
	if (a == this->from() && b == this->to()) { return; }
	m_anchor = a;
	m_end = b;
	update();
	emit selectionChanged();
}

int SampleSelectionView::viewFrames() const
{
	return m_viewSpan > 0 ? m_viewSpan : m_frames;
}

int SampleSelectionView::frameAt(int x) const
{
	const double perPixel = static_cast<double>(viewFrames()) / std::max(1, width());
	return std::clamp(static_cast<int>(m_viewFrom + x * perPixel), 0, m_frames);
}

int SampleSelectionView::xOf(int frame) const
{
	return static_cast<int>(std::lround(static_cast<double>(frame - m_viewFrom) * width() / std::max(1, viewFrames())));
}

SampleSelectionView::Edge SampleSelectionView::edgeAt(int x) const
{
	if (to() <= from()) { return Edge::None; }
	const int toStart = std::abs(x - xOf(from()));
	const int toEnd = std::abs(x - xOf(to()));
	if (std::min(toStart, toEnd) > tokens::targetSize() / 2) { return Edge::None; }
	return toEnd <= toStart ? Edge::End : Edge::Start;
}

void SampleSelectionView::setView(int first, int span)
{
	const int least = std::min(m_frames, std::max(1, width()));  // a frame a pixel at the most
	span = std::clamp(span, least, std::max(least, m_frames));
	m_viewSpan = span >= m_frames ? 0 : span;
	m_viewFrom = std::clamp(first, 0, std::max(0, m_frames - viewFrames()));
	update();
}

void SampleSelectionView::zoomBy(double factor, int anchorX)
{
	if (m_frames <= 0 || factor <= 0.0) { return; }
	const int anchor = frameAt(anchorX);
	const int span = static_cast<int>(std::lround(viewFrames() / factor));
	setView(anchor - static_cast<int>(std::lround(static_cast<double>(anchorX) / std::max(1, width()) * span)), span);
}

void SampleSelectionView::zoomToSelection()
{
	if (to() <= from()) { return; }
	const int pad = (to() - from()) / 20;  // a little room either side, so both handles show
	setView(from() - pad, to() - from() + 2 * pad);
}

void SampleSelectionView::showAll()
{
	setView(0, m_frames);
}

void SampleSelectionView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.control);
	if (m_frames <= 0) { return; }
	const int left = xOf(from());
	QColor selection = pal.accent;
	selection.setAlpha(70);
	p.fillRect(QRect(left, 0, std::max(1, xOf(to()) - left), height()), selection);
	p.setPen(pal.text);
	const double scale = static_cast<double>(width()) / std::max(1, viewFrames());
	const QRect whole(static_cast<int>(-std::lround(m_viewFrom * scale)), 0, static_cast<int>(std::lround(m_frames * scale)), height());
	m_thumbnail.visualize({.sampleRect = whole, .viewportRect = rect()}, p);
	if (to() > from())
	{
		paintHandle(p, from(), Edge::Start);
		paintHandle(p, to(), Edge::End);
	}
	else { p.fillRect(QRect(left, 0, 1, height()), pal.text); }  // a click without a drag: where it was
}

void SampleSelectionView::paintHandle(QPainter& p, int frame, Edge edge) const
{
	const tokens::Palette& pal = tokens::palette();
	const int x = xOf(frame);
	p.fillRect(QRect(x - 1, 0, 2, height()), pal.accent);
	// A flag at the top and the bottom pointing into the range - "[" at the start, "]" at the end - so the two
	// edges differ in shape, not only in place.
	const int w = tokens::space::md;
	const int h = tokens::space::lg;
	const int flagX = edge == Edge::Start ? x : x - w;
	for (const int y : {0, height() - h})
	{
		const QRect flag(flagX, y, w, h);
		p.fillRect(flag, pal.accent);
		tokens::strokeCrisp(p, QRectF(flag), pal.text);
	}
}

int SampleSelectionView::keyboardStep() const
{
	return std::max(1, viewFrames() / 100);
}

QString SampleSelectionView::readout() const
{
	if (m_unsupportedFrameCount) { return tr("This sample is too long to select"); }
	if (m_frames <= 0) { return tr("No audio"); }
	return tr("Frames %1 to %2 of %3; showing %4 frames from %5")
		.arg(from()).arg(to()).arg(m_frames).arg(viewFrames()).arg(m_viewFrom);
}

namespace
{

bool zoomKey(SampleSelectionView* view, int key, bool ctrl)
{
	if (!ctrl) { return false; }
	switch (key)
	{
		case Qt::Key_Plus:
		case Qt::Key_Equal: view->zoomBy(2.0, view->width() / 2); return true;
		case Qt::Key_Minus: view->zoomBy(0.5, view->width() / 2); return true;
		case Qt::Key_0: view->showAll(); return true;
		default: return false;
	}
}

bool cursorKey(int key, int current, int step, int frames, int* target)
{
	switch (key)
	{
		case Qt::Key_Left: *target = std::clamp(current - step, 0, frames); return true;
		case Qt::Key_Right: *target = std::clamp(current + step, 0, frames); return true;
		case Qt::Key_Home: *target = 0; return true;
		case Qt::Key_End: *target = frames; return true;
		default: return false;
	}
}

} // namespace

void SampleSelectionView::keyPressEvent(QKeyEvent* event)
{
	if (m_frames <= 0 || m_unsupportedFrameCount) { QWidget::keyPressEvent(event); return; }
	const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
	if (ctrl && event->key() == Qt::Key_A)
	{
		m_anchor = 0;
		m_end = m_frames;
		update();
		emit selectionChanged();
		return;
	}
	if (zoomKey(this, event->key(), ctrl))
	{
		update();
		emit selectionChanged();
		return;
	}
	int target = 0;
	if (cursorKey(event->key(), to(), keyboardStep(), m_frames, &target))
	{
		if (event->modifiers().testFlag(Qt::ShiftModifier)) { m_end = target; }
		else { m_anchor = m_end = target; }
		update();
		emit selectionChanged();
		return;
	}
	QWidget::keyPressEvent(event);
}

void SampleSelectionView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }  // as the drag: only the left button selects
	switch (edgeAt(event->pos().x()))
	{
	case Edge::Start: m_anchor = to(); m_end = from(); break;  // the other edge stays where it is
	case Edge::End: m_anchor = from(); m_end = to(); break;
	case Edge::None: m_anchor = m_end = frameAt(event->pos().x()); break;
	}
	update();
	emit selectionChanged();
}

void SampleSelectionView::mouseMoveEvent(QMouseEvent* event)
{
	if (!(event->buttons() & Qt::LeftButton))
	{
		setCursor(edgeAt(event->pos().x()) != Edge::None ? Qt::SizeHorCursor : Qt::IBeamCursor);
		return;
	}
	m_end = frameAt(event->pos().x());
	update();
	emit selectionChanged();
}

void SampleSelectionView::wheelEvent(QWheelEvent* event)
{
	const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
	if (delta == 0 || m_frames <= 0) { return; }
	if (event->modifiers().testFlag(Qt::ControlModifier)) { zoomBy(delta > 0 ? 1.25 : 0.8, static_cast<int>(event->position().x())); }
	else { setView(m_viewFrom - static_cast<int>(static_cast<qint64>(delta) * viewFrames() / 1200), viewFrames()); }  // a notch: a tenth
	event->accept();
}

} // namespace lmms::gui
