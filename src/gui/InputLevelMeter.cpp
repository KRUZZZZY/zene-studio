/*
 * InputLevelMeter.cpp - 0.5.0 L08 (MX-134): the engine's audio input, metered live
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

#include "InputLevelMeter.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QTimer>

#include "DesignTokens.h"
#include "InputMeter.h"
#include "MixerViewOptions.h"

namespace lmms::gui
{

namespace
{

double dbOf(float level) { return level > 0.f ? 20.0 * std::log10(level) : -120.0; }

QString figure(float level) { return level > 0.f ? QString::number(dbOf(level), 'f', 1) : QStringLiteral("-inf"); }

//! Where @a level sits on the -60..0 dB scale, 0..1.
double shareOf(float level) { return std::clamp((dbOf(level) + 60.0) / 60.0, 0.0, 1.0); }

} // namespace

InputLevelMeter::InputLevelMeter(QWidget* parent) :
	QWidget(parent)
{
	setObjectName(QStringLiteral("inputLevelMeter"));
	setAccessibleName(tr("Input level"));
	setToolTip(tr("The audio input, live (click to clear the clip light)"));
	setMinimumSize(48, 120);
	for (QElapsedTimer& since : m_heldSince) { since.start(); }
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &InputLevelMeter::poll);
	timer->start(40);
	describe();
}

void InputLevelMeter::tick(float left, float right)
{
	const float fallOff = mixerviewoptions::meterFallOff();
	const int holdMs = mixerviewoptions::peakHoldMs(mixerviewoptions::options().peakHold);
	const std::array<float, 2> reading{left, right};
	for (std::size_t side = 0; side < 2; ++side)
	{
		m_level[side] = std::max(reading[side], m_level[side] / fallOff);
		if (m_level[side] >= m_held[side] || holdMs <= 0 || m_heldSince[side].elapsed() > holdMs)
		{
			m_held[side] = m_level[side];
			m_heldSince[side].restart();
		}
		m_clipped = m_clipped || reading[side] >= 1.f;
	}
	describe();
	update();
}

void InputLevelMeter::poll()
{
	float left = 0.f;
	float right = 0.f;
	inputmeter::live(&left, &right);
	tick(left, right);
}

void InputLevelMeter::describe()
{
	QString text = tr("Input left %1 dB, right %2 dB").arg(figure(m_level[0]), figure(m_level[1]));
	if (m_clipped) { text += tr(", clipped"); }
	if (accessibleDescription() != text) { setAccessibleDescription(text); }
}

void InputLevelMeter::mousePressEvent(QMouseEvent*)
{
	m_clipped = false;
	describe();
	update();
}

void InputLevelMeter::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.panel);
	p.setPen(pal.text);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.drawText(QRect(0, 0, width(), 16), Qt::AlignCenter, m_clipped ? tr("IN CLIP") : tr("IN"));
	const int top = 18;
	const int bottom = height() - 4;
	const int barWidth = std::max(4, width() / 2 - 6);
	for (int side = 0; side < 2; ++side)
	{
		const QRect bar(4 + side * (barWidth + 4), top, barWidth, bottom - top);
		p.fillRect(bar, pal.control);
		const double db = dbOf(m_level[side]);
		const int y = bar.bottom() - static_cast<int>(shareOf(m_level[side]) * bar.height());
		p.fillRect(QRect(bar.left(), y, bar.width(), bar.bottom() - y), db > -6.0 ? pal.meterHigh : db > -18.0 ? pal.meterMid : pal.meterLow);
		const int hold = bar.bottom() - static_cast<int>(shareOf(m_held[side]) * bar.height());
		p.fillRect(QRect(bar.left(), hold, bar.width(), 2), m_clipped ? pal.record : pal.text);
	}
}

} // namespace lmms::gui
