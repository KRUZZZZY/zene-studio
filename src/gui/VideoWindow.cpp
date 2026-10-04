/*
 * VideoWindow.cpp - the floating video window, following the transport
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

#include "VideoWindow.h"

#include <cmath>

#include <QPainter>
#include <QTimer>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MainWindow.h"
#include "Song.h"
#include "VideoCommands.h"
#include "VideoDecoder.h"
#include "VideoFramePump.h"
#include "VideoReference.h"

namespace lmms
{

namespace gui
{

namespace
{

constexpr int FollowMs = 15;  //!< how often the window reads the play position: under a frame at 60 fps
constexpr int TimecodeBand = 22;

//! What the window says instead of a picture at @a seconds of @a video; empty inside the video.
QString statusAt(double seconds, const VideoAttachment& video)
{
	if (seconds < 0.0) { return VideoWindow::tr("Before the video starts"); }
	const bool pastEnd = video.duration > 0.0 && seconds >= video.duration;
	return pastEnd ? VideoWindow::tr("After the video ends") : QString();
}

} // namespace

VideoWindow::VideoWindow(QWidget* parent) :
	QWidget(parent, Qt::Tool),
	m_pump(new VideoFramePump(this)),
	m_timer(new QTimer(this))
{
	setObjectName(QStringLiteral("videoWindow"));
	setWindowTitle(tr("Video"));
	setAccessibleName(tr("Video window"));
	setMinimumSize(160, 90 + TimecodeBand);
	resize(480, 270 + TimecodeBand);
	m_timer->setInterval(FollowMs);
	connect(m_timer, &QTimer::timeout, this, &VideoWindow::follow);
	connect(m_pump, &VideoFramePump::frameReady, this, [this](const QImage& image, double seconds, int) { take(image, seconds); });
	connect(&VideoReference::instance(), &VideoReference::changed, this, &VideoWindow::follow);
}

VideoWindow* VideoWindow::find(QWidget* main)
{
	return main != nullptr ? main->findChild<VideoWindow*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

void VideoWindow::follow()
{
	const VideoAttachment& video = VideoReference::instance().attachment();
	if (!video.attached())
	{
		showNoVideo();
		return;
	}
	// The arrangement's position: the video is placed on the song's timeline, whichever editor is playing.
	const double seconds = VideoReference::instance().secondsAt(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks());
	const QString timecode = VideoReference::timecode(seconds, video.fps);
	const QString status = statusAt(seconds, video);
	if (timecode != m_timecode || status != m_status) { update(); }  // a still transport repaints nothing
	m_timecode = timecode;
	m_status = status;
	// One frame in flight at most: a tick that comes while one decodes is dropped, never queued behind it.
	if (!m_status.isEmpty() || m_waiting || !stale(seconds, video)) { return; }
	m_waiting = true;
	m_framePath = video.path;
	m_frameBox = picture().size();
	m_pump->request(video.path, seconds, m_frameBox);
}

void VideoWindow::showNoVideo()
{
	const QString status = tr("No video is attached to the project");
	if (m_framePath.isEmpty() && m_status == status) { return; }  // already says so: no repaint per tick
	m_frame = QImage();
	m_framePath.clear();
	m_timecode.clear();
	m_status = status;
	update();
}

QRect VideoWindow::picture() const
{
	return rect().adjusted(0, 0, 0, -TimecodeBand);
}

bool VideoWindow::stale(double seconds, const VideoAttachment& video) const
{
	const double halfFrame = 0.5 / (video.fps > 0.0 ? video.fps : 25.0);
	return m_framePath != video.path || m_frameBox != picture().size() || std::abs(seconds - m_frameAt) >= halfFrame;
}

void VideoWindow::take(const QImage& image, double seconds)
{
	m_waiting = false;
	m_frame = image;
	m_frameAt = seconds;
	update();
}

void VideoWindow::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	const QRect area = picture();
	if (!m_frame.isNull() && m_status.isEmpty())
	{
		const QSize size = m_frame.size().scaled(area.size(), Qt::KeepAspectRatio);
		p.drawImage(QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size), m_frame);
	}
	// No status and no picture once an answer came: the file did not open or nothing decoded.
	const bool unreadable = m_status.isEmpty() && m_frame.isNull() && !m_waiting && !m_framePath.isEmpty();
	p.setPen(pal.textMuted);
	p.setFont(tokens::font(tokens::TextRole::Body));
	p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, unreadable ? tr("The video cannot be read (video.get_state says whether "
		"the file is still there)") : m_status);
	const QRect band(0, area.bottom() + 1, width(), TimecodeBand);
	p.fillRect(band, pal.raised);
	p.setPen(pal.text);
	p.setFont(tokens::font(tokens::TextRole::Title));
	p.drawText(band.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, m_timecode);
}

void VideoWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	m_timer->start();
	follow();
}

void VideoWindow::hideEvent(QHideEvent* event)
{
	m_timer->stop();
	QWidget::hideEvent(event);
}

} // namespace gui

namespace
{

ControlResult windowShow(const QJsonObject& args)
{
	if (!VideoDecoder::available()) { return ControlResult::failure(ControlErrorKind::Requires, VideoDecoder::unavailableReason()); }
	gui::MainWindow* main = controlMainWindow();
	if (main == nullptr) { return noInterface(QStringLiteral("video.window_show")); }
	gui::VideoWindow* window = gui::VideoWindow::find(main);
	const bool before = window != nullptr && window->isVisible();
	const bool wanted = args.contains(QStringLiteral("shown")) ? args.value(QStringLiteral("shown")).toBool() : !before;
	if (wanted && window == nullptr) { window = new gui::VideoWindow(main); }
	if (window != nullptr) { window->setVisible(wanted); }
	if (wanted) { window->raise(); }
	return ControlResult::success({{QStringLiteral("shown"), wanted},
		{QStringLiteral("attached"), VideoReference::instance().attachment().attached()}});
}

} // namespace

void registerVideoWindowCommand(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("video", "window_show");
	cmd.description = QStringLiteral("Show the video window ('shown' false hides it; it toggles when left out): the project's "
		"video, following play, seeks and loops - frames are dropped rather than late when decoding falls behind - with its "
		"timecode (HH:MM:SS:FF). Decoding runs on its own thread. Interface state.");
	cmd.argsSchema = control::objectSchema({{QStringLiteral("shown"), control::booleanProperty()}});
	cmd.resultSchema = control::objectProperty();
	cmd.handler = windowShow;
	registry.registerCommand(cmd);
}

} // namespace lmms
