/*
 * VideoFfmpeg.h - one opened stream of a media file through FFmpeg, shared by the video decoder and the audio extractor
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

#ifndef LMMS_VIDEO_FFMPEG_H
#define LMMS_VIDEO_FFMPEG_H

#include "lmmsconfig.h"

#ifdef ZENE_HAVE_FFMPEG

#include <cstdint>

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace lmms::video
{

//! FFmpeg's text for the error @a code.
QString errorText(int code);

/*! One stream of a media file, opened with its decoder (ED-054). Owned by one thread at a time: VideoDecoder's on
 *  the frame pump's worker, the audio extractor's on the thread running video.extract_audio. Closes on destruction. */
struct MediaStream
{
	AVFormatContext* format = nullptr;
	AVCodecContext* codec = nullptr;
	int index = -1;
	bool draining = false;  //!< the file is read to its end and the decoder is being flushed

	MediaStream() = default;
	MediaStream(const MediaStream&) = delete;
	MediaStream& operator=(const MediaStream&) = delete;
	~MediaStream() { close(); }

	//! Open @a path's best stream of @a type with its decoder; false, @a error set, when the file or stream cannot be.
	bool open(const QString& path, AVMediaType type, QString* error);
	void close();
	bool isOpen() const { return codec != nullptr; }

	AVStream* stream() const { return format->streams[index]; }
	double timeBase() const { return av_q2d(stream()->time_base); }
	//! The stream's first timestamp (0 when the file does not say).
	int64_t startPts() const;
	//! @a pts in seconds from the stream's start.
	double secondsOf(int64_t pts) const { return static_cast<double>(pts - startPts()) * timeBase(); }

	//! The next decoded frame into @a frame, reading packets through @a packet as needed; false at the end or on an error.
	bool receive(AVFrame* frame, AVPacket* packet);
	//! Seek to the key frame at or before @a seconds and empty the decoder.
	void seek(double seconds);

private:
	//! Send the stream's next packet to the decoder, or start flushing it at the end; false when nothing is left.
	bool feed(AVPacket* packet);
};

} // namespace lmms::video

#endif // ZENE_HAVE_FFMPEG

#endif // LMMS_VIDEO_FFMPEG_H
