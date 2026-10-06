/*
 * PhaseScopeView.cpp - 0.5.0 L08 (MX-137): the stereo phase view - a goniometer beside the correlation
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

#include "PhaseScopeView.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>

#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "PhaseScopeTap.h"

namespace lmms::gui
{

namespace
{

constexpr std::size_t Shown = 1024;  //!< frames drawn: 21 ms at 48 kHz
constexpr double InvSqrt2 = 0.70710678118654752;
constexpr qint64 StaleMs = 250;      //!< no new frames for this long: the channel is silent to the view

//! The mixer channel whose stable id is @a id, as an index; -1 when there is none.
int indexOfId(int id)
{
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		if (Engine::mixer()->mixerChannel(i)->id() == id) { return i; }
	}
	return -1;
}

} // namespace

PhaseScopeView::PhaseScopeView(QWidget* parent) :
	QWidget(parent)
{
	setObjectName(QStringLiteral("phaseScopeView"));
	setAccessibleName(tr("Stereo phase view"));
	setAccessibleDescription(tr("A goniometer: mono stands up the middle, out of phase lies flat"));
	setMinimumSize(160, 160);
}

void PhaseScopeView::setFrames(const float* left, const float* right, std::size_t count)
{
	m_points.clear();
	m_peak = 0.0;
	for (std::size_t i = 0; left != nullptr && right != nullptr && i < count; ++i)
	{
		m_points.emplace_back((right[i] - left[i]) * InvSqrt2, (left[i] + right[i]) * InvSqrt2);
		m_peak = std::max({m_peak, std::abs(m_points.back().x()), std::abs(m_points.back().y())});
	}
	update();
}

void PhaseScopeView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.control);
	const QPointF centre(width() / 2.0, height() / 2.0);
	const double radius = std::min(width(), height()) / 2.0 - 12.0;
	p.setPen(pal.separator);
	p.drawLine(QPointF(centre.x(), centre.y() - radius), QPointF(centre.x(), centre.y() + radius));  // mid
	p.drawLine(QPointF(centre.x() - radius, centre.y()), QPointF(centre.x() + radius, centre.y()));  // side
	const double d = radius * InvSqrt2;
	p.drawLine(centre + QPointF(-d, -d), centre + QPointF(d, d));
	p.drawLine(centre + QPointF(d, -d), centre + QPointF(-d, d));
	p.setPen(pal.textMuted);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.drawText(centre + QPointF(-d - 10, -d - 2), tr("L"));
	p.drawText(centre + QPointF(d + 4, -d - 2), tr("R"));
	p.drawText(centre + QPointF(4, -radius + 10), tr("M"));
	if (m_points.empty() || m_peak <= 1e-6)
	{
		p.drawText(rect(), Qt::AlignHCenter | Qt::AlignBottom, tr("No signal"));
		return;
	}
	const double scale = 0.9 * radius / std::max(m_peak, 1e-3);  // the loudest frame near the rim
	QColor dot = pal.accent;
	dot.setAlpha(140);
	p.setPen(QPen(dot, 1.5));
	for (const QPointF& point : m_points) { p.drawPoint(centre + QPointF(point.x() * scale, -point.y() * scale)); }
}

PhaseScopeWindow::PhaseScopeWindow(QWidget* parent) :
	QDialog(parent),
	m_channels(new QComboBox(this)),
	m_view(new PhaseScopeView(this)),
	m_correlation(new QLabel(this)),
	m_correlationBar(new QProgressBar(this)),
	m_left(Shown),
	m_right(Shown)
{
	setWindowTitle(tr("Phase Scope"));
	setAccessibleName(tr("Phase Scope"));
	setProperty("controlCommand", QStringLiteral("window.phase_scope"));
	m_channels->setAccessibleName(tr("Channel"));
	m_channels->setProperty("controlCommand", QStringLiteral("window.phase_scope"));
	m_correlationBar->setRange(-100, 100);
	m_correlationBar->setTextVisible(false);
	m_correlationBar->setAccessibleName(tr("Phase correlation"));
	auto* row = new QHBoxLayout;
	row->addWidget(new QLabel(tr("Channel:"), this));
	row->addWidget(m_channels, 1);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(row);
	layout->addWidget(m_view, 1);
	layout->addWidget(m_correlation);
	layout->addWidget(m_correlationBar);
	fillChannels();
	connect(m_channels, &QComboBox::activated, this, [this](int row) { setChannel(indexOfId(m_channels->itemData(row).toInt())); });
	m_sinceNew.start();
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this] { refresh(); });
	timer->start(40);
	resize(320, 380);
	setChannel(0);
}

PhaseScopeWindow::~PhaseScopeWindow()
{
	PhaseScopeTap::instance().setChannel(PhaseScopeTap::Off);  // nothing left to look at
}

void PhaseScopeWindow::fillChannels()
{
	m_channels->clear();
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		const MixerChannel* channel = Engine::mixer()->mixerChannel(i);
		m_channels->addItem(i == 0 ? tr("Master") : channel->m_name, channel->id());
	}
}

void PhaseScopeWindow::setChannel(int index)
{
	if (index < 0 || index >= Engine::mixer()->numChannels()) { index = 0; }
	const int id = Engine::mixer()->mixerChannel(index)->id();
	// Asked again for the channel it shows (window.phase_scope raising it): keep the picture and the count.
	if (id == m_channelId && PhaseScopeTap::instance().channel() == index) { return; }
	m_channelId = id;
	PhaseScopeTap::instance().setChannel(index);
	m_lastWritten = 0;
	m_view->setFrames(nullptr, nullptr, 0);
	refresh();
}

int PhaseScopeWindow::channel() const
{
	return std::max(0, indexOfId(m_channelId));
}

QString PhaseScopeWindow::refresh()
{
	if (m_channels->count() != Engine::mixer()->numChannels()) { fillChannels(); }
	const int index = channel();  // followed by its id: a deleted channel falls back to the master
	if (PhaseScopeTap::instance().channel() != index) { setChannel(index); }
	m_channels->setCurrentIndex(m_channels->findData(m_channelId));
	PhaseScopeTap& tap = PhaseScopeTap::instance();
	const std::uint64_t written = tap.written();
	if (written != m_lastWritten)
	{
		m_lastWritten = written;
		m_sinceNew.restart();
		const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(written, Shown));
		// An overrun copy keeps the last picture; the next refresh reads again.
		if (count > 0 && tap.latest(m_left.data(), m_right.data(), count)) { m_view->setFrames(m_left.data(), m_right.data(), count); }
	}
	else if (m_sinceNew.elapsed() > StaleMs) { m_view->setFrames(nullptr, nullptr, 0); }
	const float correlation = Engine::mixer()->mixerChannel(index)->m_correlation.get();
	m_correlationBar->setValue(static_cast<int>(std::lround(correlation * 100.f)));
	m_correlation->setText(tr("Correlation %1  (+1 mono, 0 wide, -1 out of phase)").arg(correlation, 0, 'f', 2));
	return m_correlation->text();
}

PhaseScopeWindow* openPhaseScope()
{
	if (getGUI() == nullptr || getGUI()->mainWindow() == nullptr) { return nullptr; }
	for (PhaseScopeWindow* open : getGUI()->mainWindow()->findChildren<PhaseScopeWindow*>())
	{
		if (open->isVisible()) { return open; }  // a closed one is waiting for its deferred delete
	}
	return nullptr;
}

PhaseScopeWindow* showPhaseScope(int index)
{
	PhaseScopeWindow* window = openPhaseScope();
	if (window == nullptr)
	{
		if (getGUI() == nullptr || getGUI()->mainWindow() == nullptr) { return nullptr; }
		window = new PhaseScopeWindow(getGUI()->mainWindow());
		window->setAttribute(Qt::WA_DeleteOnClose);
		window->setModal(false);
	}
	window->setChannel(index);
	window->show();
	window->raise();
	window->activateWindow();
	return window;
}

} // namespace lmms::gui
