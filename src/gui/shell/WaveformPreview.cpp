/*
 * WaveformPreview.cpp - the browser's waveform of the selected sample (browser.peaks)
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

#include "WaveformPreview.h"

#include <algorithm>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include "ControlRegistry.h"
#include "DesignTokens.h"

namespace lmms::gui
{

namespace
{

//! How many buckets one preview asks for: enough for a wide panel, a fraction of the cache's base.
constexpr int PreviewBuckets = 512;
constexpr int PreviewHeight = 56;

} // namespace

WaveformPreview::WaveformPreview(QWidget* parent) :
	QWidget(parent)
{
	setObjectName(QStringLiteral("waveformPreview"));
	setAccessibleName(tr("Sample waveform"));
	setMinimumHeight(PreviewHeight);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	hide();
}

QSize WaveformPreview::sizeHint() const
{
	return QSize(200, PreviewHeight);
}

bool WaveformPreview::setPath(const QString& path)
{
	m_path = path;
	m_peaks.clear();
	m_message.clear();
	m_seconds = 0;
	setVisible(!path.isEmpty());
	if (path.isEmpty()) { return false; }
	const ControlResult read = ControlRegistry::instance()->invoke(QStringLiteral("browser.peaks"),
		{{QStringLiteral("path"), path}, {QStringLiteral("buckets"), PreviewBuckets}});
	if (!read.ok)
	{
		m_message = read.errorMessage;
		update();
		return false;
	}
	for (const QJsonValue& value : read.result.value(QStringLiteral("peaks")).toArray())
	{
		const QJsonObject peak = value.toObject();
		m_peaks.append({static_cast<float>(peak.value(QStringLiteral("min")).toDouble()),
			static_cast<float>(peak.value(QStringLiteral("max")).toDouble())});
	}
	m_seconds = read.result.value(QStringLiteral("duration_seconds")).toDouble();
	setAccessibleDescription(tr("%1, %2 seconds").arg(QFileInfo(path).fileName()).arg(m_seconds, 0, 'f', 2));
	update();
	return !m_peaks.isEmpty();
}

void WaveformPreview::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.base);
	if (m_peaks.isEmpty())
	{
		p.setPen(pal.textMuted);
		p.drawText(rect().adjusted(tokens::space::sm, 0, -tokens::space::sm, 0), Qt::AlignVCenter | Qt::TextWordWrap, m_message);
		return;
	}
	const double mid = height() / 2.0;
	const double scale = height() / 2.0 - 2;
	const int columns = std::max(1, width());
	p.setPen(pal.clipSample);
	for (int x = 0; x < columns; ++x)
	{
		const int index = static_cast<int>(static_cast<long long>(x) * m_peaks.size() / columns);
		const auto& [low, high] = m_peaks.at(std::min(index, static_cast<int>(m_peaks.size()) - 1));
		p.drawLine(QPointF(x + 0.5, mid - high * scale), QPointF(x + 0.5, mid - low * scale));
	}
	p.setPen(pal.textMuted);
	p.drawText(rect().adjusted(0, 0, -tokens::space::xs, 0), Qt::AlignRight | Qt::AlignBottom,
		QStringLiteral("%1 s").arg(m_seconds, 0, 'f', 2));
}

} // namespace lmms::gui
