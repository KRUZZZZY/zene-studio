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

} // namespace

ChannelLevelMeter::ChannelLevelMeter(const QString& channelId, QWidget* parent) :
	QWidget(parent),
	m_channel(channelId),
	m_left(-std::numeric_limits<float>::infinity()),
	m_right(-std::numeric_limits<float>::infinity())
{
	setObjectName(QStringLiteral("deviceChannelLevel"));
	setAccessibleName(tr("Level of %1").arg(channelId));
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
	setToolTip(tr("%1: %2 / %3 dBFS (RMS)").arg(m_channel).arg(static_cast<double>(std::max(m_left, kFloorDb)), 0, 'f', 1)
		.arg(static_cast<double>(std::max(m_right, kFloorDb)), 0, 'f', 1));
	update();
}

void ChannelLevelMeter::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	const tokens::Palette& palette = tokens::palette();
	painter.fillRect(rect(), palette.control);
	const int bar = std::max(1, height() / 2 - 1);
	painter.fillRect(QRect(0, 0, static_cast<int>(width() * fill(m_left)), bar), palette.accent);
	painter.fillRect(QRect(0, height() - bar, static_cast<int>(width() * fill(m_right)), bar), palette.accent);
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
