/*
 * VideoFfmpeg.cpp - one opened stream of a media file through FFmpeg, shared by the video decoder and the audio extractor
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

#include "VideoFfmpeg.h"

#ifdef ZENE_HAVE_FFMPEG

#include <algorithm>

#include <QFile>

namespace lmms::video
{

QString errorText(int code)
{
	char text[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, text, sizeof(text));
	return QString::fromUtf8(text);
}

bool MediaStream::open(const QString& path, AVMediaType type, QString* error)
{
	close();
	int code = avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr);
	if (code >= 0) { code = avformat_find_stream_info(format, nullptr); }
	const AVCodec* decoder = nullptr;
	if (code >= 0) { code = av_find_best_stream(format, type, -1, -1, &decoder, 0); }
	if (code >= 0)
	{
		index = code;
		codec = avcodec_alloc_context3(decoder);
		code = codec != nullptr ? avcodec_parameters_to_context(codec, stream()->codecpar) : AVERROR(ENOMEM);
	}
	if (code >= 0) { code = avcodec_open2(codec, decoder, nullptr); }
	if (code < 0)
	{
		if (error) { *error = errorText(code); }
		close();
		return false;
	}
	draining = false;
	return true;
}

void MediaStream::close()
{
	avcodec_free_context(&codec);
	avformat_close_input(&format);
	index = -1;
	draining = false;
}

int64_t MediaStream::startPts() const
{
	const int64_t start = stream()->start_time;
	return start == AV_NOPTS_VALUE ? 0 : start;
}

bool MediaStream::receive(AVFrame* frame, AVPacket* packet)
{
	for (;;)
	{
		const int got = avcodec_receive_frame(codec, frame);
		if (got == 0) { return true; }
		if (got != AVERROR(EAGAIN) || !feed(packet)) { return false; }
	}
}

bool MediaStream::feed(AVPacket* packet)
{
	if (draining) { return false; }
	while (av_read_frame(format, packet) >= 0)
	{
		const bool ours = packet->stream_index == index;
		if (ours) { avcodec_send_packet(codec, packet); }
		av_packet_unref(packet);
		if (ours) { return true; }
	}
	draining = true;
	avcodec_send_packet(codec, nullptr);  // the decoder hands out what it still holds, then reports the end
	return true;
}

void MediaStream::seek(double seconds)
{
	const auto target = startPts() + static_cast<int64_t>(std::max(0.0, seconds) / timeBase());
	av_seek_frame(format, index, target, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(codec);
	draining = false;
}

} // namespace lmms::video

#endif // ZENE_HAVE_FFMPEG
