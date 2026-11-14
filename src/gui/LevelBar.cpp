/*
 * LevelBar.cpp - a thin level meter that reads its level from a function
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
#include "LevelBar.h"

#include <algorithm>
#include <cmath>

#include <QEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>

#include "AudioBusHandle.h"
#include "AutomatableModel.h"
#include "DesignTokens.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

//! A linear level in dB, floored at the meter's -60.
double levelDb(float level)
{
	return level > 0.f ? std::max(-60.0, 20.0 * std::log10(level)) : -60.0;
}

} // namespace

LevelBar::LevelBar(std::function<float()> level, const QString& name, QWidget* parent) :
	QWidget(parent),
	m_source(std::move(level)),
	m_name(name),
	m_timer(new QTimer(this))
{
	setFixedSize(6, 22);
	setToolTip(name);
	setAccessibleName(name);
	connect(m_timer, &QTimer::timeout, this, [this] {
		m_level = std::max(m_source(), m_level * 0.85f);
		update();
	});
	m_timer->setInterval(50);  // started while shown (showEvent): a hidden meter wakes nothing
}

bool LevelBar::event(QEvent* event)
{
	// The tooltip carries the reading at the moment it opens.
	if (event->type() == QEvent::ToolTip)
	{
		const double db = levelDb(m_level);
		setToolTip(tr("%1: %2 dB").arg(m_name, db > -60.0 ? QString::number(db, 'f', 1) : QStringLiteral("< -60")));
	}
	return QWidget::event(event);
}

void LevelBar::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	m_timer->start();
}

void LevelBar::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	m_timer->stop();
}

void LevelBar::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.control);
	const double db = levelDb(m_level);
	const int h = static_cast<int>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0) * height());
	p.fillRect(QRect(0, height() - h, width(), h), db > -6.0 ? pal.meterHigh : db > -18.0 ? pal.meterMid : pal.meterLow);
}

LevelBar* trackLevelBar(Track* track, QWidget* parent)
{
	const QPointer<Track> watched(track);  // the view can outlive its track by an event-loop turn
	return new LevelBar([watched] {
		// The track's own output, never its mixer channel's: tracks share channels (every new one feeds Master),
		// so the channel's level lit every track's meter when one note played (owner, Windows tester 2026-10-29).
		AudioBusHandle* bus = nullptr;
		if (auto* instrument = qobject_cast<InstrumentTrack*>(watched.data())) { bus = instrument->audioBusHandle(); }
		else if (auto* sample = qobject_cast<SampleTrack*>(watched.data())) { bus = sample->audioBusHandle(); }
		return bus != nullptr ? bus->takePeak() : 0.f;
	}, QObject::tr("The track's level"), parent);
}

} // namespace lmms::gui
