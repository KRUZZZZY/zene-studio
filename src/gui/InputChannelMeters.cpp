/*
 * InputChannelMeters.cpp - a meter for every capture channel and for every record-armed track's input
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

#include "InputChannelMeters.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPointer>
#include <QTimer>

#include "DesignTokens.h"
#include "GuiApplication.h"
#include "InputReadings.h"
#include "MainWindow.h"
#include "MixerViewOptions.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

QPointer<InputChannelMeters> s_open;

double shareOf(float level) { return std::clamp((inputreadings::peakDb(level) + 60.0) / 60.0, 0.0, 1.0); }

//! @a reading over @a meter: taken when higher, else the bar falls by the mixer's fall-off.
void follow(InputChannelMeters::Meter& meter, float reading)
{
	meter.level = std::max(reading, meter.level / mixerviewoptions::meterFallOff());
	if (meter.level < 1e-6f) { meter.level = 0.f; }  // fallen to silence, not a stale crumb
}

constexpr int BarWidth = 22;
constexpr int Gap = 6;

} // namespace

InputChannelMeters::InputChannelMeters(QWidget* parent) :
	QDialog(parent)
{
	setObjectName(QStringLiteral("inputChannelMeters"));
	setWindowTitle(tr("Input Meters"));
	setAccessibleName(tr("Input Meters"));
	setAttribute(Qt::WA_DeleteOnClose);
	setMinimumSize(200, 260);
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &InputChannelMeters::poll);
	timer->start(40);
	poll();
}

void InputChannelMeters::poll()
{
	const inputreadings::Readings readings = inputreadings::take(inputmeter::Reader::Interface);
	m_channels.resize(static_cast<std::size_t>(readings.channelCount));
	for (int c = 0; c < readings.channelCount; ++c)
	{
		Meter& meter = m_channels[static_cast<std::size_t>(c)];
		meter.label = tr("In %1").arg(c + 1);
		meter.inputChannel = c;
		follow(meter, readings.peaks[static_cast<std::size_t>(c)]);
	}
	std::vector<Meter> armed;
	for (const inputreadings::Armed& entry : readings.armed)
	{
		Meter meter;
		// The bar a route had keeps its level (and falls from it); a route armed since starts from silence.
		const auto was = std::find_if(m_armed.begin(), m_armed.end(), [&entry](const Meter& m) {
			return m.route == entry.route && m.inputChannel == entry.inputChannel; });
		if (was != m_armed.end()) { meter = *was; }
		meter.route = entry.route;
		meter.inputChannel = entry.inputChannel;
		meter.label = tr("%1 - In %2").arg(entry.track != nullptr ? entry.track->name() : tr("Route %1").arg(entry.route + 1),
			QString::number(entry.inputChannel + 1));
		follow(meter, entry.peak);
		armed.push_back(meter);
	}
	m_armed = std::move(armed);
	describe();
	update();
}

void InputChannelMeters::describe()
{
	QStringList parts;
	for (const auto* list : {&m_channels, &m_armed})
	{
		for (const Meter& meter : *list)
		{
			parts << tr("%1 %2 dB").arg(meter.label, meter.level > 0.f ? QString::number(inputreadings::peakDb(meter.level), 'f', 1) : tr("-inf"));
		}
	}
	const QString text = parts.isEmpty() ? tr("No capture input") : parts.join(QStringLiteral(", "));
	if (accessibleDescription() != text) { setAccessibleDescription(text); }
}

void InputChannelMeters::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.panel);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.setPen(pal.text);
	const int half = height() / 2;
	const auto row = [&](const std::vector<Meter>& meters, int top, int bottom, const QString& empty) {
		if (meters.empty()) { p.drawText(QRect(0, top, width(), bottom - top), Qt::AlignCenter, empty); return; }
		for (std::size_t i = 0; i < meters.size(); ++i)
		{
			const QRect bar(Gap + static_cast<int>(i) * (BarWidth + Gap), top + 4, BarWidth, bottom - top - 26);
			p.fillRect(bar, pal.control);
			const double db = inputreadings::peakDb(meters[i].level);
			const int y = bar.bottom() - static_cast<int>(shareOf(meters[i].level) * bar.height());
			p.fillRect(QRect(bar.left(), y, bar.width(), bar.bottom() - y), db > -6.0 ? pal.meterHigh : db > -18.0 ? pal.meterMid : pal.meterLow);
			p.drawText(QRect(bar.left() - Gap / 2, bar.bottom() + 2, BarWidth + Gap, 20), Qt::AlignCenter | Qt::TextWordWrap,
				p.fontMetrics().elidedText(meters[i].label, Qt::ElideMiddle, BarWidth * 3));
		}
	};
	row(m_channels, 0, half, tr("No capture input"));
	row(m_armed, half, height(), tr("No record-armed track"));
}

InputChannelMeters* showInputMeters()
{
	if (s_open == nullptr)
	{
		QWidget* owner = getGUI() != nullptr ? static_cast<QWidget*>(getGUI()->mainWindow()) : nullptr;
		s_open = new InputChannelMeters(owner);
	}
	s_open->show();
	s_open->raise();
	return s_open;
}

InputChannelMeters* openInputMeters() { return s_open; }

} // namespace lmms::gui
