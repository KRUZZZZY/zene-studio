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

#include "BrowserPeakCache.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "PeakJobs.h"

namespace lmms::gui
{

namespace
{

//! How many buckets one preview asks for: enough for a wide panel, a fraction of the cache's base.
constexpr int PreviewBuckets = 512;
constexpr int PreviewHeight = 56;
//! At or below this a file is read where it was selected: milliseconds, like the read of a small sample always was.
constexpr qint64 SynchronousReadBytes = 1024 * 1024;

QString taskName(int id)
{
	return QStringLiteral("peak-%1").arg(id);
}

} // namespace

WaveformPreview::WaveformPreview(QWidget* parent) :
	QWidget(parent)
{
	setObjectName(QStringLiteral("waveformPreview"));
	setAccessibleName(tr("Sample waveform"));
	setMinimumHeight(PreviewHeight);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	hide();
	connect(&PeakJobs::instance(), &PeakJobs::ready, this, [this](int id) {
		if (m_pendingTask != taskName(id)) { return; }
		m_pendingTask.clear();
		m_message.clear();
		readPeaks();
	});
	connect(&PeakJobs::instance(), &PeakJobs::ended, this, [this](int id, const QString&, const QString& error, bool cancelled) {
		if (m_pendingTask != taskName(id)) { return; }
		m_pendingTask.clear();
		showMessage(cancelled ? tr("Waveform analysis cancelled") : error);
	});
}

QSize WaveformPreview::sizeHint() const
{
	return QSize(200, PreviewHeight);
}

bool WaveformPreview::setPath(const QString& path)
{
	cancelPending();
	m_path = path;
	m_peaks.clear();
	m_message.clear();
	m_seconds = 0;
	setAccessibleDescription(QString());  // never the previous sample's name and length (review r4 L20 #5)
	setVisible(!path.isEmpty());
	if (path.isEmpty()) { return false; }
	// A short operation does not report (TaskCenter.h): a click through small samples must not fill the Tasks list.
	const QFileInfo file(path);
	if (BrowserPeakCache::instance().isCurrent(path) || (file.isFile() && file.size() <= SynchronousReadBytes)) { return readPeaks(); }
	const ControlResult started = ControlRegistry::instance()->invoke(QStringLiteral("browser.peaks_start"), {{QStringLiteral("path"), path}});
	if (!started.ok)
	{
		showMessage(started.errorMessage);
		return false;
	}
	if (started.result.value(QStringLiteral("cached")).toBool()) { return readPeaks(); }
	m_pendingTask = started.result.value(QStringLiteral("task")).toString();
	showMessage(tr("Analysing waveform..."));
	return false;
}

bool WaveformPreview::readPeaks()
{
	const ControlResult read = ControlRegistry::instance()->invoke(QStringLiteral("browser.peaks"),
		{{QStringLiteral("path"), m_path}, {QStringLiteral("buckets"), PreviewBuckets}});
	if (!read.ok)
	{
		showMessage(read.errorMessage);
		return false;
	}
	for (const QJsonValue& value : read.result.value(QStringLiteral("peaks")).toArray())
	{
		const QJsonObject peak = value.toObject();
		m_peaks.append({static_cast<float>(peak.value(QStringLiteral("min")).toDouble()),
			static_cast<float>(peak.value(QStringLiteral("max")).toDouble())});
	}
	m_seconds = read.result.value(QStringLiteral("duration_seconds")).toDouble();
	setAccessibleDescription(tr("%1, %2 seconds").arg(QFileInfo(m_path).fileName()).arg(m_seconds, 0, 'f', 2));
	update();
	return !m_peaks.isEmpty();
}

void WaveformPreview::showMessage(const QString& message)
{
	m_message = message;
	setAccessibleDescription(m_message);
	update();
}

void WaveformPreview::cancelPending()
{
	if (m_pendingTask.isEmpty()) { return; }
	const QString task = m_pendingTask;
	m_pendingTask.clear();  // first: a cancel of a queued job reports back inside the call below
	ControlRegistry::instance()->invoke(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), task}});
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
