/*
 * TakeLaneRows.cpp - a sample track's take lanes as rows in the arrangement
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

#include "TakeLaneRows.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QResizeEvent>

#include "Clip.h"
#include "SampleClip.h"
#include "TakeLane.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{
constexpr int WatchMs = 300;
}

TakeLaneRows::TakeLaneRows(Track* track, TrackView* view) :
	QWidget(view->getTrackContentWidget()),
	m_track(track),
	m_view(view)
{
	setObjectName(QStringLiteral("takeLaneRows"));
	setAccessibleName(tr("Take lanes"));
	// Display only: a click belongs to the clip view under the rows, exactly as before.
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setGeometry(parentWidget()->rect());
	parentWidget()->installEventFilter(this);
	connect(&m_timer, &QTimer::timeout, this, &TakeLaneRows::watch);
	m_timer.start(WatchMs);
	watch();
}

int TakeLaneRows::xOfTick(const tick_t tick) const
{
	const TrackContainerView* tcv = m_view->trackContainerView();
	const double ticks = static_cast<double>(tick - tcv->currentPosition().getTicks());
	return static_cast<int>(std::lround(ticks * tcv->pixelsPerBar() / TimePos::ticksPerBar()));
}

int TakeLaneRows::rowOfLane(const int lane) const
{
	const auto found = std::find(m_lanes.begin(), m_lanes.end(), lane);
	return found == m_lanes.end() ? -1 : static_cast<int>(std::distance(m_lanes.begin(), found));
}

void TakeLaneRows::refreshNow()
{
	watch();
}

void TakeLaneRows::watch()
{
	std::vector<int> lanes;
	if (m_track != nullptr)
	{
		for (const TakeLane& lane : m_track->takeLanes().lanes()) { lanes.push_back(lane.index); }
	}

	std::vector<Band> bands;
	const int rows = static_cast<int>(lanes.size());
	if (rows > 0 && height() > 0)
	{
		const int rowHeight = std::max(1, height() / rows);
		for (Clip* clip : m_track->getClips())
		{
			if (clip == nullptr) { continue; }
			const int row = static_cast<int>(std::distance(lanes.begin(),
				std::find(lanes.begin(), lanes.end(), clip->laneIndex())));
			if (row < 0 || row >= rows) { continue; }
			const int left = xOfTick(clip->startPosition().getTicks());
			const int right = std::max(left + 1, xOfTick(clip->endPosition().getTicks()));
			const int top = row * rowHeight;
			bands.push_back({clip->laneIndex(), clip->startPosition().getTicks(), clip->endPosition().getTicks(),
				QRect(left, top + 2, right - left, std::max(2, rowHeight - 4)),
				QPointer<Clip>(clip)});
		}
	}

	const bool visible = !lanes.empty();
	if (visible != isVisible()) { setVisible(visible); }
	if (!visible)
	{
		if (!m_bands.empty() || !m_lanes.empty())
		{
			m_bands.clear();
			m_lanes.clear();
			update();
		}
		return;
	}
	setGeometry(parentWidget()->rect());
	raise();
	if (lanes != m_lanes || bands != m_bands)
	{
		m_lanes = std::move(lanes);
		m_bands = std::move(bands);
		update();
	}
	else
	{
		setGeometry(parentWidget()->rect());
	}
}

bool TakeLaneRows::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == parentWidget() && event->type() == QEvent::Resize) { setGeometry(parentWidget()->rect()); }
	return QWidget::eventFilter(watched, event);
}

void TakeLaneRows::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(30, 34, 38));
	if (m_lanes.empty() || m_bands.empty()) { return; }

	const int rows = static_cast<int>(m_lanes.size());
	const int rowHeight = std::max(1, height() / rows);
	for (int row = 0; row < rows; ++row)
	{
		const QRect rowRect(0, row * rowHeight, width(), (row == rows - 1 ? height() : (row + 1) * rowHeight) - row * rowHeight);
		p.fillRect(rowRect, QColor(30, 34, 38));
		p.setPen(QColor(64, 70, 78));
		p.drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());
		p.setPen(QColor(200, 206, 214));
		p.drawText(rowRect.adjusted(6, 0, -4, 0), Qt::AlignTop | Qt::AlignLeft,
			tr("Lane %1").arg(m_lanes[static_cast<std::size_t>(row)]));
	}

	for (const Band& band : m_bands)
	{
		const QRect visible = band.rect.intersected(rect());
		if (visible.isEmpty()) { continue; }
		const QColor fill(78, 112, 145, 120);
		p.fillRect(visible, fill);
		p.setPen(QColor(142, 178, 210));
		p.setBrush(Qt::NoBrush);
		p.drawRect(band.rect.adjusted(0, 0, -1, -1));
		if (const auto* sample = dynamic_cast<const SampleClip*>(band.clip.data()); sample != nullptr)
		{
			m_waves.paint(p, *sample, band.rect.adjusted(1, 1, -1, -1), visible, QColor(226, 235, 244, 210));
		}
	}
}

} // namespace lmms::gui
