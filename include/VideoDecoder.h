/*
 * VideoDecoder.h - a video file's frames, sought and scaled through FFmpeg, for the video window and lane
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

#ifndef LMMS_VIDEO_DECODER_H
#define LMMS_VIDEO_DECODER_H

#include <memory>

#include <QImage>
#include <QSize>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! What a video file says it is.
struct VideoProbe
{
	double fps = 0.0;
	double duration = 0.0;  //!< seconds
	int width = 0;
	int height = 0;
	bool hasAudio = false;
};

/*! 0.5.0 ED-054: one video file's picture through FFmpeg (libavformat, libavcodec, libswscale). frameAt() seeks
 *  only when it must - backwards, or further ahead than a short decode - and otherwise decodes forward, passing
 *  over the frames between without scaling them, so a caller that asks for "now" gets the frame for now rather
 *  than the next one in line. Not thread-safe: one thread owns a decoder (VideoFramePump's worker), and it never
 *  runs on the audio thread. In a build without FFmpeg (ZENE_HAVE_FFMPEG undefined) every call fails and
 *  available() says so. */
class LMMS_EXPORT VideoDecoder
{
public:
	VideoDecoder();
	~VideoDecoder();
	VideoDecoder(const VideoDecoder&) = delete;
	VideoDecoder& operator=(const VideoDecoder&) = delete;

	//! Whether this build has FFmpeg.
	static bool available();
	//! The refusal a command gives in a build without it.
	static QString unavailableReason();
	//! Open @a path just long enough to read what it is; false, with @a error, when it is not a video FFmpeg reads.
	static bool probe(const QString& path, VideoProbe* out, QString* error);

	bool open(const QString& path, QString* error);
	void close();
	bool isOpen() const;
	const VideoProbe& info() const;

	//! The frame showing at @a seconds of the video (clamped into it), scaled to fit @a box with its aspect kept
	//! (its own size when @a box is empty). A null image when nothing is open or nothing decodes.
	QImage frameAt(double seconds, const QSize& box);
	//! The time of the frame frameAt() last decoded (-1 before the first).
	double shownSeconds() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	VideoProbe m_info;
};

} // namespace lmms

#endif // LMMS_VIDEO_DECODER_H
