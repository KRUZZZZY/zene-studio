/*
 * VideoWindow.h - the floating video window, following the transport
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

#ifndef LMMS_GUI_VIDEO_WINDOW_H
#define LMMS_GUI_VIDEO_WINDOW_H

#include <QImage>
#include <QString>
#include <QWidget>

#include "lmms_export.h"

class QTimer;

namespace lmms
{

class VideoFramePump;
struct VideoAttachment;

namespace gui
{

/*! 0.5.0 ED-054: the project's video in a floating window (video.window_show, View > Video Window). A timer on the
 *  interface thread reads the play position - so play, a seek and a loop jump are all just "where the song is
 *  now" - turns it into the video's time through the tempo map and the video's offset, shows the timecode at
 *  once, and asks the frame pump (VideoFramePump, a worker thread) for that frame only when the picture is stale
 *  AND no frame is still being decoded: a tick that comes while one is in flight is skipped, so when decoding falls
 *  behind, frames are dropped and the picture never lags the music. Outside the video's span it says so. With
 *  several clips (Q050-139/Q050-180.1) it follows the clip active at the play position; a frame of another clip is
 *  taken down the moment the play position leaves it, and an answer that arrives for a clip no longer active is
 *  dropped, so no stale picture shows before, after or between clips. The timer runs only while the window is
 *  shown. */
class LMMS_EXPORT VideoWindow : public QWidget
{
	Q_OBJECT
public:
	explicit VideoWindow(QWidget* parent);

	//! The video window of @a main, or nullptr while none was opened.
	static VideoWindow* find(QWidget* main);

	//! What the window shows: the timecode, a status line (empty while a frame shows), the time of the frame shown.
	QString timecode() const { return m_timecode; }
	QString status() const { return m_status; }
	double frameSeconds() const { return m_frame.isNull() ? -1.0 : m_frameAt; }
	//! Whether a picture is actually shown (a status line hides the last frame).
	bool hasFrame() const { return !m_frame.isNull() && m_status.isEmpty(); }
	//! One step of the timer (a test's way to drive it).
	void follow();

protected:
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	//! The play position is in the clip whose source key (id and file) is @a activeSource (empty: in none): a picture
	//! of any other source is taken down.
	void noteActiveSource(const QString& activeSource);
	//! No video attached: the picture and the timecode go, and the window says so.
	void showNoVideo();
	//! Where the picture goes: the window above the timecode band.
	QRect picture() const;
	//! Whether the frame shown no longer fits @a seconds of @a video in this window's size.
	bool stale(double seconds, const VideoAttachment& video) const;
	void take(const QImage& image, double seconds);

	VideoFramePump* m_pump;
	QTimer* m_timer;
	QImage m_frame;
	double m_frameAt = -1.0;
	QString m_framePath;   //!< the file the frame shown (or asked for) came from
	QString m_requestSource;  //!< the source (clip id and file) the last frame was asked for
	QString m_frameSource;    //!< the source the frame shown came from
	QString m_activeSource;   //!< the source at the play position on the last follow(); empty outside every clip
	QSize m_frameBox;
	QString m_timecode;
	QString m_status;
	bool m_waiting = false;  //!< a frame asked for and not yet arrived
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_VIDEO_WINDOW_H
