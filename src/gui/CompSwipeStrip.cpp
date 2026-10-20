/*
 * CompSwipeStrip.cpp - comp by swiping: the take lanes as rows, a drag along one comps that range
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
#include "CompSwipeStrip.h"

#include <algorithm>

#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSet>
#include <QTouchEvent>

#include "AccessibleCanvas.h"
#include "AccessibleWidgets.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MusicalTime.h"
#include "SampleClip.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

//! The span the takes on @a lanes cover on @a trackId's track; false when there are none.
bool takeSpan(const QString& trackId, const std::vector<int>& lanes, int* begin, int* end)
{
	ControlResult error;
	const Track* track = control::resolveTrack(trackId, &error);
	if (track == nullptr) { return false; }
	bool found = false;
	for (const Clip* clip : track->getClips())
	{
		if (std::find(lanes.begin(), lanes.end(), clip->laneIndex()) == lanes.end()) { continue; }
		*begin = found ? std::min<int>(*begin, clip->startPosition().getTicks()) : clip->startPosition().getTicks();
		*end = found ? std::max<int>(*end, clip->endPosition().getTicks()) : clip->endPosition().getTicks();
		found = true;
	}
	return found;
}

//! A beat of the song's metre, in ticks: the cursor's step.
int beatTicks()
{
	Song* song = Engine::getSong();
	const int beats = song != nullptr ? std::max(1, song->getTimeSigModel().getNumerator()) : 4;
	return std::max(1, TimePos::ticksPerBar() / beats);
}

Qt::KeyboardModifiers modsOf(const QKeyEvent* event) { return event->modifiers() & ~Qt::KeypadModifier; }

//! The touch point's position in the widget, in both Qt's touch-point spellings (5.15 and 6).
QPoint touchPos(const QWidget* widget,
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	const QEventPoint& point
#else
	const QTouchEvent::TouchPoint& point
#endif
)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return widget->mapFromGlobal(point.globalPosition().toPoint());
#else
	return widget->mapFromGlobal(point.screenPos().toPoint());
#endif
}

} // namespace

CompSwipeStrip::CompSwipeStrip(const QString& trackId, QWidget* parent) :
	QWidget(parent),
	m_trackId(trackId)
{
	setObjectName(QStringLiteral("compSwipeStrip"));
	setAccessibleName(tr("Comp lanes - drag along a lane to comp that range, double-click to cycle the take"));
	setMouseTracking(false);
	setMinimumHeight(RowHeight);
	setAccessibleDescription(tr("Keys: Up and Down choose the lane, Left and Right move a beat (Ctrl: a bar), Shift marks a "
		"range; Enter comps the range, or the segment under the cursor, to the lane; C cycles the take there; Ctrl+Shift+L "
		"lists the segments to edit them."));
	setAttribute(Qt::WA_Hover);  // theme 23: the hover cue
	// 0.5.0 L03 (Q050-274): the strip is a touch surface; accepting the touch sequence is also
	// what keeps Qt from synthesising a mouse press for the same finger.
	setAttribute(Qt::WA_AcceptTouchEvents, true);
	a11y::describeCanvas(this, QAccessible::Canvas, [this] { return accessibleValue(); });
}

void CompSwipeStrip::setState(const QJsonObject& state)
{
	m_lanes.clear();
	for (const QJsonValue& lane : state.value(QStringLiteral("lanes")).toArray()) { m_lanes.push_back(lane.toObject().value(QStringLiteral("lane")).toInt()); }
	m_segments.clear();
	const QJsonObject composite = state.value(QStringLiteral("composite")).toObject();
	for (const QJsonValue& value : composite.value(QStringLiteral("segments")).toArray())
	{
		const QJsonObject s = value.toObject();
		m_segments.push_back({s.value(QStringLiteral("begin")).toInt(), s.value(QStringLiteral("end")).toInt(), s.value(QStringLiteral("lane")).toInt()});
	}
	// The span drawn: the comp's AND the takes' union - a comp may cover only part of a lane set,
	// and the takes outside it still have to be shown - else the takes' span, else four bars from
	// the start.
	m_begin = composite.value(QStringLiteral("begin")).toInt();
	m_end = composite.value(QStringLiteral("end")).toInt();
	int takeBegin = 0;
	int takeEnd = 0;
	if (takeSpan(m_trackId, m_lanes, &takeBegin, &takeEnd) && takeEnd > takeBegin)
	{
		if (m_end <= m_begin)
		{
			m_begin = takeBegin;
			m_end = takeEnd;
		}
		else
		{
			m_begin = std::min(m_begin, takeBegin);
			m_end = std::max(m_end, takeEnd);
		}
	}
	if (m_end <= m_begin)
	{
		m_begin = 0;
		m_end = 4 * TimePos::ticksPerBar();
	}
	setMinimumHeight(RowHeight * std::max<int>(1, static_cast<int>(m_lanes.size())));
	rebuildTakes();
	m_keyRow = std::clamp(m_keyRow, 0, std::max(0, static_cast<int>(m_lanes.size()) - 1));
	if (m_keyTick < m_begin || m_keyTick >= m_end) { m_keyTick = m_begin; }
	updateGeometry();
	update();
	emit stateChanged();
}

int CompSwipeStrip::tickAt(int x) const
{
	const double fraction = std::clamp(static_cast<double>(x) / std::max(1, width()), 0.0, 1.0);
	return m_begin + static_cast<int>(fraction * (m_end - m_begin));
}

int CompSwipeStrip::laneAt(int y) const
{
	const int row = y / RowHeight;
	return row >= 0 && row < static_cast<int>(m_lanes.size()) ? m_lanes[static_cast<std::size_t>(row)] : -1;
}

QSize CompSwipeStrip::sizeHint() const
{
	return {320, RowHeight * std::max<int>(1, static_cast<int>(m_lanes.size()))};
}

void CompSwipeStrip::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), palette().base());

	// 0.5.0 L03 (Q050-123): each take's own waveform in its lane, at its own place on the strip's
	// span. The active comp segments are drawn OVER the waveforms, translucent, so a musician sees
	// both which take supplies a segment and the audio it was chosen for.
	for (const TakeBand& band : m_takeBands)
	{
		const QRect visible = band.rect.intersected(rect());
		if (visible.isEmpty()) { continue; }
		p.fillRect(visible, QColor(70, 96, 122, 110));
		if (const auto* sample = dynamic_cast<const SampleClip*>(band.clip.data()); sample != nullptr)
		{
			m_waves.paint(p, *sample, band.rect, visible, palette().text().color());
		}
	}

	for (std::size_t row = 0; row < m_lanes.size(); ++row)
	{
		const int y = static_cast<int>(row) * RowHeight;
		p.setPen(palette().mid().color());
		p.drawLine(0, y + RowHeight - 1, width(), y + RowHeight - 1);
		for (const Segment& segment : m_segments)
		{
			if (segment.lane != m_lanes[row]) { continue; }
			const QRect segmentRect(xOf(segment.begin), y + 2, xOf(segment.end) - xOf(segment.begin), RowHeight - 4);
			QColor highlight = palette().highlight().color();
			highlight.setAlpha(90);
			p.fillRect(segmentRect, highlight);
			p.setPen(palette().highlight().color());
			p.setBrush(Qt::NoBrush);
			p.drawRect(segmentRect.adjusted(0, 0, -1, -1));
		}
		if (m_dragLane == m_lanes[row])
		{
			QColor drag = palette().highlight().color();
			drag.setAlpha(110);
			p.fillRect(QRect(xOf(std::min(m_dragFrom, m_dragTo)), y, std::abs(xOf(m_dragTo) - xOf(m_dragFrom)), RowHeight), drag);
		}
		p.setPen(palette().text().color());
		p.drawText(QRect(4, y, width() - 8, RowHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Lane %1").arg(m_lanes[row]));
	}
	paintKeyCursor(p);
	a11y::paintCanvasCues(p, this);
}

int CompSwipeStrip::xOf(int tick) const
{
	return static_cast<int>(static_cast<double>(tick - m_begin) / std::max(1, m_end - m_begin) * width());
}

void CompSwipeStrip::rebuildTakes()
{
	m_takeBands.clear();
	ControlResult error;
	m_track = control::resolveTrack(m_trackId, &error);
	if (m_track == nullptr)
	{
		m_track = nullptr;
		return;
	}
	for (Clip* clip : m_track->getClips())
	{
		if (clip == nullptr) { continue; }
		const auto row = std::find(m_lanes.begin(), m_lanes.end(), clip->laneIndex());
		if (row == m_lanes.end()) { continue; }
		const int y = static_cast<int>(std::distance(m_lanes.begin(), row)) * RowHeight;
		const int left = xOf(clip->startPosition().getTicks());
		const int right = std::max(left + 1, xOf(clip->endPosition().getTicks()));
		m_takeBands.push_back({clip->laneIndex(), clip->startPosition().getTicks(), clip->endPosition().getTicks(),
			QRect(left, y + 2, right - left, RowHeight - 4), QPointer<Clip>(clip)});
	}
}

void CompSwipeStrip::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	rebuildTakes();
}

void CompSwipeStrip::paintKeyCursor(QPainter& p) const
{
	// Theme 10/23: the range the keys marked (an outlined band) and the cursor (a line), on the cursor's lane, while
	// the strip has the focus - an outline, not a tint alone.
	if (!hasFocus() || m_lanes.empty()) { return; }
	const int y = m_keyRow * RowHeight;
	const int x = xOf(m_keyTick);
	if (m_keyAnchor >= 0)
	{
		const int from = xOf(std::min(m_keyAnchor, m_keyTick));
		a11y::paintKeyMark(p, QRect(from, y, std::max(4, std::abs(xOf(m_keyAnchor) - x)), RowHeight));
	}
	a11y::paintKeyMark(p, QRect(x - 3, y, 7, RowHeight));
}

int CompSwipeStrip::keyLane() const
{
	return m_lanes.empty() ? -1 : m_lanes[static_cast<std::size_t>(m_keyRow)];
}

const CompSwipeStrip::Segment* CompSwipeStrip::segmentAt(int tick) const
{
	for (const Segment& segment : m_segments)
	{
		if (tick >= segment.begin && tick < segment.end) { return &segment; }
	}
	return nullptr;
}

QString CompSwipeStrip::accessibleValue() const
{
	if (m_lanes.empty()) { return tr("No take lanes"); }
	QString text = m_keyAnchor >= 0
		? tr("Lane %1, %2 to %3 marked").arg(keyLane()).arg(musicaltime::positionText(std::min(m_keyAnchor, m_keyTick)),
			musicaltime::positionText(std::max(m_keyAnchor, m_keyTick)))
		: tr("Lane %1, at %2").arg(keyLane()).arg(musicaltime::positionText(m_keyTick));
	const Segment* playing = segmentAt(m_keyTick);
	return text + (playing != nullptr ? tr("; the comp plays lane %1 here").arg(playing->lane) : tr("; no comp here"));
}

bool CompSwipeStrip::handlesKey(const QKeyEvent* event) const
{
	static const QSet<int> keys{Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down, Qt::Key_Home, Qt::Key_End,
		Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space, Qt::Key_C, Qt::Key_Escape};
	const Qt::KeyboardModifiers mods = modsOf(event) & ~(Qt::ShiftModifier | Qt::ControlModifier);
	if (event->key() == Qt::Key_L) { return modsOf(event) == (Qt::ControlModifier | Qt::ShiftModifier); }
	return keys.contains(event->key()) && mods == Qt::NoModifier && !m_lanes.empty();
}

bool CompSwipeStrip::event(QEvent* event)
{
	// 0.5.0 L03 (Q050-274): one finger swipes a range, the same gesture as a mouse drag.
	switch (event->type())
	{
		case QEvent::TouchBegin:
		case QEvent::TouchUpdate:
		case QEvent::TouchEnd:
		case QEvent::TouchCancel:
			return touchEvent(event);
		default:
			break;
	}
	// The strip's keys are its own while it has the focus (the arrows and C are not window shortcuts here).
	if (event->type() == QEvent::ShortcutOverride && handlesKey(static_cast<QKeyEvent*>(event)))
	{
		event->accept();
		return true;
	}
	return QWidget::event(event);
}

bool CompSwipeStrip::touchEvent(QEvent* genericEvent)
{
	auto* event = static_cast<QTouchEvent*>(genericEvent);
	const auto eventType = event->type();
	if (eventType == QEvent::TouchCancel)
	{
		m_touchActive = false;
		m_touchId = -1;
		m_dragLane = -1;
		update();
		event->accept();
		return true;
	}

	if (eventType == QEvent::TouchBegin)
	{
		if (m_touchActive) { event->accept(); return true; }
		for (const auto& point : event->points())
		{
			const QPoint pos = touchPos(this, point);
			const int lane = laneAt(pos.y());
			if (static_cast<int>(point.state()) != static_cast<int>(Qt::TouchPointPressed) || lane < 0) { continue; }
			m_touchActive = true;
			m_touchId = point.id();
			m_dragLane = lane;
			m_dragFrom = m_dragTo = tickAt(pos.x());
			update();
			event->accept();
			return true;
		}
		return false;
	}

	if (!m_touchActive) { return false; }

	for (const auto& point : event->points())
	{
		if (point.id() != m_touchId) { continue; }
		const QPoint pos = touchPos(this, point);
		if (eventType == QEvent::TouchEnd || static_cast<int>(point.state()) == static_cast<int>(Qt::TouchPointReleased))
		{
			m_dragTo = tickAt(pos.x());
			const int lane = m_dragLane;
			const int from = m_dragFrom;
			const int to = m_dragTo;
			m_touchActive = false;
			m_touchId = -1;
			m_dragLane = -1;
			commitDrag(lane, from, to);
			event->accept();
			return true;
		}
		m_dragTo = tickAt(pos.x());
		update();
		event->accept();
		return true;
	}
	event->accept();
	return true;
}

bool CompSwipeStrip::moveKey(const QKeyEvent* event)
{
	const int key = event->key();
	if (key == Qt::Key_Up || key == Qt::Key_Down)
	{
		m_keyRow = std::clamp(m_keyRow + (key == Qt::Key_Up ? -1 : 1), 0, static_cast<int>(m_lanes.size()) - 1);
		return true;
	}
	const int moved = movedTick(key, modsOf(event).testFlag(Qt::ControlModifier));
	if (moved < 0) { return false; }
	const bool shift = modsOf(event).testFlag(Qt::ShiftModifier);
	if (shift && m_keyAnchor < 0) { m_keyAnchor = m_keyTick; }
	if (!shift) { m_keyAnchor = -1; }
	m_keyTick = std::clamp(moved, m_begin, m_end);
	return true;
}

int CompSwipeStrip::movedTick(int key, bool byBar) const
{
	const int step = byBar ? TimePos::ticksPerBar() : beatTicks();
	switch (key)
	{
		case Qt::Key_Home: return m_begin;
		case Qt::Key_End: return m_end;
		case Qt::Key_Left: return std::max(0, m_keyTick - step);
		case Qt::Key_Right: return m_keyTick + step;
		default: return -1;
	}
}

void CompSwipeStrip::keyPressEvent(QKeyEvent* event)
{
	if (!handlesKey(event))
	{
		QWidget::keyPressEvent(event);
		return;
	}
	const int key = event->key();
	if (moveKey(event)) { update(); return; }
	if (key == Qt::Key_C) { cycleAtCursor(); }
	else if (key == Qt::Key_Escape) { m_keyAnchor = -1; }
	else if (key == Qt::Key_L)
	{
		ControlRegistry::instance()->invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("comp")}});
	}
	else { compAtCursor(); }
	update();
}

void CompSwipeStrip::compAtCursor()
{
	// The marked range, else the segment under the cursor, else the beat there: to the cursor's lane, ONE comp.select.
	int begin = m_keyAnchor >= 0 ? std::min(m_keyAnchor, m_keyTick) : m_keyTick;
	int end = m_keyAnchor >= 0 ? std::max(m_keyAnchor, m_keyTick) : std::min(m_keyTick + beatTicks(), m_end);
	if (const Segment* under = segmentAt(m_keyTick); under != nullptr && m_keyAnchor < 0)
	{
		begin = under->begin;
		end = under->end;
	}
	if (end <= begin) { return; }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.select"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("begin"), begin}, {QStringLiteral("end"), end}, {QStringLiteral("lane"), keyLane()}});
	m_keyAnchor = -1;
	if (!result.ok) { a11y::announceTransition(this, a11y::Transition::Refused, result.errorMessage); }
	emit ran(result.ok ? tr("Comped %1 to %2 on lane %3").arg(musicaltime::positionText(begin), musicaltime::positionText(end)).arg(keyLane())
		: result.errorMessage);
}

void CompSwipeStrip::cycleAtCursor()
{
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.cycle"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("tick"), m_keyTick}});
	if (!result.ok) { a11y::announceTransition(this, a11y::Transition::Refused, result.errorMessage); }
	emit ran(result.ok ? tr("Lane %1 there now").arg(result.result.value(QStringLiteral("lane")).toInt()) : result.errorMessage);
}

void CompSwipeStrip::mousePressEvent(QMouseEvent* event)
{
	if (m_touchActive) { event->ignore(); return; }
	m_dragLane = event->button() == Qt::LeftButton ? laneAt(event->pos().y()) : -1;
	m_dragFrom = m_dragTo = tickAt(event->pos().x());
	update();
}

void CompSwipeStrip::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragLane < 0) { return; }
	m_dragTo = tickAt(event->pos().x());
	update();
}

void CompSwipeStrip::mouseReleaseEvent(QMouseEvent* event)
{
	// A synthesised mouse release after a touch sequence must not commit the same gesture twice.
	if (m_touchActive) { event->ignore(); return; }
	if (m_dragLane < 0) { return; }
	m_dragTo = tickAt(event->pos().x());
	const int lane = m_dragLane;
	m_dragLane = -1;
	commitDrag(lane, m_dragFrom, m_dragTo);
}

void CompSwipeStrip::commitDrag(const int lane, const int from, const int to)
{
	const int begin = std::min(from, to);
	const int end = std::max(from, to);
	update();
	if (end <= begin || lane < 0) { return; }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.select"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("begin"), begin}, {QStringLiteral("end"), end}, {QStringLiteral("lane"), lane}});
	// Theme 7: the range in the time format (bars|beats by default), not raw ticks.
	emit ran(result.ok ? tr("Comped %1 to %2 on lane %3").arg(musicaltime::positionText(begin), musicaltime::positionText(end)).arg(lane)
		: result.errorMessage);
}

void CompSwipeStrip::mouseDoubleClickEvent(QMouseEvent* event)
{
	m_dragLane = -1;
	// The strip offers a left double-click to cycle the take; comp.cycle changes the project, so
	// no other button's double-click may reach it.
	if (event->button() != Qt::LeftButton) { return; }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("comp.cycle"),
		{{QStringLiteral("track"), m_trackId}, {QStringLiteral("tick"), tickAt(event->pos().x())}});
	emit ran(result.ok ? tr("Lane %1 there now").arg(result.result.value(QStringLiteral("lane")).toInt()) : result.errorMessage);
}

} // namespace lmms::gui
