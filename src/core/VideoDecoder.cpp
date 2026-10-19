/*
 * VideoDecoder.cpp - a video file's frames, sought and scaled through FFmpeg, for the video window and lane
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

#include "VideoDecoder.h"

#include <algorithm>
#include <utility>

#include <QObject>

#include "VideoFfmpeg.h"

#ifdef ZENE_HAVE_FFMPEG
extern "C" {
#include <libswscale/swscale.h>
}
#endif

namespace lmms
{

#ifdef ZENE_HAVE_FFMPEG

namespace
{

constexpr double MaxDecodeAhead = 2.0;  //!< seconds: further ahead than this, a seek is cheaper than decoding forward
constexpr double Epsilon = 1e-4;

VideoProbe probeOf(const video::MediaStream& media)
{
	VideoProbe out;
	AVStream* stream = media.stream();
	out.fps = av_q2d(av_guess_frame_rate(media.format, stream, nullptr));
	out.width = media.codec->width;
	out.height = media.codec->height;
	if (media.format->duration > 0) { out.duration = static_cast<double>(media.format->duration) / AV_TIME_BASE; }
	else if (stream->duration > 0) { out.duration = static_cast<double>(stream->duration) * media.timeBase(); }
	out.hasAudio = av_find_best_stream(media.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
	return out;
}

} // namespace

struct VideoDecoder::Impl
{
	video::MediaStream media;
	AVFrame* shown = av_frame_alloc();
	AVFrame* pending = av_frame_alloc();  //!< decoded past the time asked for: the next call's first candidate
	AVPacket* packet = av_packet_alloc();
	SwsContext* scaler = nullptr;
	bool hasShown = false;
	bool hasPending = false;
	bool seekFailed = false;
	double shownAt = -1.0;
	double pendingAt = -1.0;
	double frameLength = 1.0 / 25.0;

	~Impl()
	{
		sws_freeContext(scaler);
		av_frame_free(&shown);
		av_frame_free(&pending);
		av_packet_free(&packet);
	}

	double timeOf(const AVFrame* frame, double fallback) const
	{
		const int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
		return ts == AV_NOPTS_VALUE ? fallback : media.secondsOf(ts);
	}

	void reset()
	{
		hasShown = false;
		hasPending = false;
		shownAt = -1.0;
	}

	//! Seek the stream to @a seconds; false when the demuxer refused it, so the caller must not decode on.
	bool seek(double seconds)
	{
		const bool sought = media.seek(seconds);
		reset();
		seekFailed = !sought;
		return sought;
	}

	//! Decode forward until the shown frame is the last one at or before @a seconds (or the stream ends).
	void advanceTo(double seconds)
	{
		for (;;)
		{
			if (!hasPending)
			{
				if (!media.receive(pending, packet)) { return; }
				pendingAt = timeOf(pending, shownAt + frameLength);
				hasPending = true;
			}
			if (hasShown && pendingAt > seconds + Epsilon) { return; }
			std::swap(shown, pending);  // the frame passed over is unreferenced by the next receive
			shownAt = pendingAt;
			hasShown = true;
			hasPending = false;
		}
	}

	//! The shown frame's size on screen: its pixels stretched by their aspect ratio.
	QSize displaySize()
	{
		const AVRational aspect = av_guess_sample_aspect_ratio(media.format, media.stream(), shown);
		const int width = aspect.num > 0 && aspect.den > 0 ? shown->width * aspect.num / aspect.den : shown->width;
		return QSize(std::max(1, width), std::max(1, shown->height));
	}

	QImage scaled(const QSize& box)
	{
		const QSize size = box.isEmpty() ? displaySize() : displaySize().scaled(box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
		scaler = sws_getCachedContext(scaler, shown->width, shown->height, static_cast<AVPixelFormat>(shown->format),
			size.width(), size.height(), AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (scaler == nullptr) { return QImage(); }
		QImage image(size, QImage::Format_RGB32);  // AV_PIX_FMT_RGB32 is the native-endian 0xAARRGGBB QImage stores
		uint8_t* planes[4] = {image.bits(), nullptr, nullptr, nullptr};
		const int strides[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
		sws_scale(scaler, shown->data, shown->linesize, 0, shown->height, planes, strides);
		return image;
	}
};

bool VideoDecoder::available()
{
	return true;
}

bool VideoDecoder::open(const QString& path, QString* error)
{
	close();
	auto made = std::make_unique<Impl>();
	if (!made->media.open(path, AVMEDIA_TYPE_VIDEO, error)) { return false; }
	m_info = probeOf(made->media);
	made->frameLength = m_info.fps > 0.0 ? 1.0 / m_info.fps : made->frameLength;
	m_impl = std::move(made);
	return true;
}

QImage VideoDecoder::frameAt(double seconds, const QSize& box)
{
	if (!isOpen()) { return QImage(); }
	Impl& d = *m_impl;
	const double last = m_info.duration > 0.0 ? m_info.duration - d.frameLength / 2 : seconds;  // unknown length: no clamp
	const double at = std::clamp(seconds, 0.0, std::max(0.0, last));
	const bool behind = d.seekFailed || (!d.hasShown ? at > Epsilon : at < d.shownAt - Epsilon);
	const bool farAhead = d.hasShown && at > d.shownAt + MaxDecodeAhead;
	if ((behind || farAhead) && !d.seek(at)) { return QImage(); }  // the demuxer refused the seek: no frame is the right one
	d.advanceTo(at);
	return d.hasShown ? d.scaled(box) : QImage();
}

double VideoDecoder::shownSeconds() const
{
	return isOpen() && m_impl->hasShown ? m_impl->shownAt : -1.0;
}

#else // no FFmpeg: every call fails, and says why

struct VideoDecoder::Impl
{
};

bool VideoDecoder::available()
{
	return false;
}

bool VideoDecoder::open(const QString&, QString* error)
{
	close();
	*error = unavailableReason();
	return false;
}

QImage VideoDecoder::frameAt(double, const QSize&)
{
	return QImage();
}

double VideoDecoder::shownSeconds() const
{
	return -1.0;
}

#endif // ZENE_HAVE_FFMPEG

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() = default;

QString VideoDecoder::unavailableReason()
{
	return QObject::tr("video is unavailable: this build has no FFmpeg (configure with ZENE_WANT_FFMPEG and the FFmpeg "
		"development files)");
}

bool VideoDecoder::probe(const QString& path, VideoProbe* out, QString* error)
{
	VideoDecoder decoder;
	if (!decoder.open(path, error)) { return false; }
	*out = decoder.info();
	return true;
}

void VideoDecoder::close()
{
	m_impl.reset();
	m_info = VideoProbe();
}

bool VideoDecoder::isOpen() const
{
	return m_impl != nullptr;
}

const VideoProbe& VideoDecoder::info() const
{
	return m_info;
}

} // namespace lmms
