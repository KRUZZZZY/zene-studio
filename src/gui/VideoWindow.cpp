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

#include <algorithm>
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

//! What a frame was decoded from: the clip AND its file. Ids are per project and video.attach can give a new file
//! the id of the clip it replaced, so the id alone does not say a frame is still the active clip's picture.
QString sourceKey(const VideoAttachment& clip)
{
	return clip.id + QLatin1Char('\n') + clip.path;
}

//! What the window says instead of a picture when @a tick is not inside @a clips.
QString statusAt(tick_t tick, const QVector<VideoAttachment>& clips)
{
	if (clips.isEmpty()) { return VideoWindow::tr("No video is attached to the project"); }
	tick_t first = clips.first().offset;
	tick_t last = first;
	for (const VideoAttachment& clip : clips)
	{
		first = std::min(first, clip.offset);
		last = std::max(last, VideoReference::instance().endTick(clip));
	}
	if (tick < first) { return VideoWindow::tr("Before the video starts"); }
	if (tick >= last) { return VideoWindow::tr("After the video ends"); }
	return VideoWindow::tr("No video at this position");
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
	const QVector<VideoAttachment>& clips = VideoReference::instance().clips();
	if (clips.isEmpty())
	{
		showNoVideo();
		return;
	}
	// The arrangement's position: a clip is placed on the song's timeline, whichever editor is playing.
	const tick_t tick = Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks();
	const VideoAttachment* active = VideoReference::instance().clipAt(tick);
	const VideoAttachment& timing = active != nullptr ? *active : clips.first();
	noteActiveSource(active != nullptr ? sourceKey(*active) : QString());
	const double seconds = VideoReference::instance().secondsAt(tick, timing);
	const QString timecode = VideoReference::timecode(seconds, timing.fps);
	const QString status = active != nullptr ? QString() : statusAt(tick, clips);
	if (timecode != m_timecode || status != m_status) { update(); }  // a still transport repaints nothing
	m_timecode = timecode;
	m_status = status;
	// One frame in flight at most: a tick that comes while one decodes is dropped, never queued behind it.
	if (!m_status.isEmpty() || m_waiting || !stale(seconds, timing)) { return; }
	m_waiting = true;
	m_framePath = timing.path;
	m_requestSource = sourceKey(timing);
	m_frameBox = picture().size();
	m_pump->request(timing.path, seconds, m_frameBox);
}

void VideoWindow::noteActiveSource(const QString& activeSource)
{
	m_activeSource = activeSource;
	if (m_frame.isNull() || m_frameSource == m_activeSource) { return; }
	// Another clip or file (or none) is at the play position now: its picture goes at once, never left up meanwhile.
	m_frame = QImage();
	m_frameSource.clear();
	m_frameAt = -1.0;  // stale from here: coming back to the clip asks for its frame again
	update();
}

void VideoWindow::showNoVideo()
{
	const QString status = tr("No video is attached to the project");
	if (m_framePath.isEmpty() && m_status == status) { return; }  // already says so: no repaint per tick
	m_frame = QImage();
	m_framePath.clear();
	m_frameSource.clear();
	m_requestSource.clear();
	m_activeSource.clear();
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
	return m_requestSource != sourceKey(video) || m_frameBox != picture().size()
		|| std::abs(seconds - m_frameAt) >= halfFrame;
}

void VideoWindow::take(const QImage& image, double seconds)
{
	m_waiting = false;
	// A late answer for a clip - or a file under a reused clip id - no longer at the play position is dropped: the
	// next follow() asks again.
	if (m_requestSource != m_activeSource)
	{
		m_frameAt = -1.0;
		follow();
		return;
	}
	m_frame = image;
	m_frameAt = seconds;
	m_frameSource = m_requestSource;
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
