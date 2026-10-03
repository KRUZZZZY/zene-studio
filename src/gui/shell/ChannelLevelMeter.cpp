/*
 * ChannelLevelMeter.cpp - the level of the mixer channel a Device panel's devices play into
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

#include "ChannelLevelMeter.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>

#include "ControlMixerSupport.h"
#include "DesignTokens.h"
#include "Mixer.h"
#include "lmms_math.h"

namespace lmms::gui
{

namespace
{

constexpr float kFloorDb = -60.f;
constexpr int kRefreshMs = 50;

float levelDb(float rms)
{
	return rms > 0.f ? ampToDbfs(rms) : -std::numeric_limits<float>::infinity();
}

//! How much of a bar @a db fills, 0..1, on the -60..0 dBFS scale.
float fill(float db)
{
	return std::clamp((db - kFloorDb) / -kFloorDb, 0.f, 1.f);
}

//! A bar's colour at @a db, on the mixer meters' bands (LevelBar, the mixer strips): high above -6 dBFS, mid above -18.
QColor barColour(const tokens::Palette& palette, float db)
{
	return db > -6.f ? palette.meterHigh : db > -18.f ? palette.meterMid : palette.meterLow;
}

//! The channel's name for people (its strip's name), or its id when it has none or is gone.
QString shownName(const MixerChannel* channel, const QString& id)
{
	return channel != nullptr && !channel->m_name.isEmpty() ? channel->m_name : id;
}

//! @a db as the tooltip reads it: one decimal, or -inf for silence (as the mixer's peak readouts say).
QString dbText(float db)
{
	return std::isfinite(db) ? QString::number(static_cast<double>(db), 'f', 1) : ChannelLevelMeter::tr("-inf");
}

} // namespace

ChannelLevelMeter::ChannelLevelMeter(const QString& channelId, QWidget* parent) :
	QWidget(parent),
	m_channel(channelId),
	m_left(-std::numeric_limits<float>::infinity()),
	m_right(-std::numeric_limits<float>::infinity())
{
	setObjectName(QStringLiteral("deviceChannelLevel"));
	ControlResult error;
	setAccessibleName(tr("Level of %1").arg(shownName(control::resolveMixerChannel(channelId, &error), channelId)));
	m_timer.setInterval(kRefreshMs);
	connect(&m_timer, &QTimer::timeout, this, [this] { refresh(); });
}

QSize ChannelLevelMeter::sizeHint() const
{
	return {96, 14};
}

void ChannelLevelMeter::refresh()
{
	ControlResult error;
	const MixerChannel* channel = control::resolveMixerChannel(m_channel, &error);
	m_left = levelDb(channel != nullptr ? channel->m_rmsLeft.get() : 0.f);
	m_right = levelDb(channel != nullptr ? channel->m_rmsRight.get() : 0.f);
	const QString name = shownName(channel, m_channel);
	const QString accessible = tr("Level of %1").arg(name);
	if (accessibleName() != accessible) { setAccessibleName(accessible); }  // a renamed strip; not 20 announcements a second
	setToolTip(tr("%1: left %2, right %3 dBFS (RMS)").arg(name, dbText(m_left), dbText(m_right)));
	update();
}

void ChannelLevelMeter::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	const tokens::Palette& palette = tokens::palette();
	painter.fillRect(rect(), palette.control);
	const int bar = std::max(1, height() / 2 - 1);
	painter.fillRect(QRect(0, 0, static_cast<int>(width() * fill(m_left)), bar), barColour(palette, m_left));
	painter.fillRect(QRect(0, height() - bar, static_cast<int>(width() * fill(m_right)), bar), barColour(palette, m_right));
}

void ChannelLevelMeter::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	m_timer.start();
	refresh();
}

void ChannelLevelMeter::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	m_timer.stop();
}

} // namespace lmms::gui
