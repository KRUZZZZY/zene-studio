/*
 * VideoTestClip.h - a two-second test video made at test time: one second red, one second blue, a 440 Hz tone
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

#ifndef LMMS_VIDEO_TEST_CLIP_H
#define LMMS_VIDEO_TEST_CLIP_H

#include "lmmsconfig.h"

namespace videotest
{

//! The clip's shape, which the test compares against (known in every build; the clip is made only with FFmpeg).
constexpr int Width = 160;
constexpr int Height = 90;
constexpr int Fps = 25;
constexpr int Frames = 2 * Fps;
constexpr int Rate = 44100;
constexpr int AudioChunk = 1024;

} // namespace videotest

#ifdef ZENE_HAVE_FFMPEG

#include <cmath>
#include <cstring>

#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

/*! The clip VideoTrackTest reads (no binary video is committed): 160x90 at 25 fps for two seconds - the first second
 *  pure red, the second pure blue, so a frame read back says which second it came from - and a stereo 44.1 kHz
 *  440 Hz tone. Made with the ffmpeg program when it is installed, otherwise encoded here with libavcodec (MPEG-4
 *  video, PCM audio, Matroska), so a machine with the development files and no ffmpeg program still runs the test. */
namespace videotest
{


struct Encoder
{
	AVCodecContext* codec = nullptr;
	AVStream* stream = nullptr;
	AVFrame* frame = nullptr;
	~Encoder()
	{
		av_frame_free(&frame);
		avcodec_free_context(&codec);
	}
};

inline void setupVideo(AVCodecContext* c)
{
	c->width = Width;
	c->height = Height;
	c->pix_fmt = AV_PIX_FMT_YUV420P;
	c->time_base = AVRational{1, Fps};
	c->framerate = AVRational{Fps, 1};
	c->gop_size = 10;
	c->bit_rate = 400000;
}

inline void setupAudio(AVCodecContext* c)
{
	c->sample_fmt = AV_SAMPLE_FMT_S16;
	c->sample_rate = Rate;
	c->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	c->time_base = AVRational{1, Rate};
}

inline bool allocateFrame(Encoder& e)
{
	e.frame = av_frame_alloc();
	if (e.frame == nullptr) { return false; }
	e.frame->format = e.codec->codec_type == AVMEDIA_TYPE_VIDEO ? static_cast<int>(e.codec->pix_fmt) : static_cast<int>(e.codec->sample_fmt);
	e.frame->width = e.codec->width;
	e.frame->height = e.codec->height;
	e.frame->sample_rate = e.codec->sample_rate;
	e.frame->nb_samples = AudioChunk;
	av_channel_layout_copy(&e.frame->ch_layout, &e.codec->ch_layout);
	return av_frame_get_buffer(e.frame, 0) >= 0;
}

inline bool addStream(AVFormatContext* out, Encoder& e, AVCodecID id, void (*setup)(AVCodecContext*))
{
	const AVCodec* codec = avcodec_find_encoder(id);
	if (codec == nullptr) { return false; }
	e.stream = avformat_new_stream(out, nullptr);
	e.codec = avcodec_alloc_context3(codec);
	if (e.stream == nullptr || e.codec == nullptr) { return false; }
	setup(e.codec);
	if (out->oformat->flags & AVFMT_GLOBALHEADER) { e.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; }
	if (avcodec_open2(e.codec, codec, nullptr) < 0) { return false; }
	avcodec_parameters_from_context(e.stream->codecpar, e.codec);
	e.stream->time_base = e.codec->time_base;
	return allocateFrame(e);
}

//! Frame @a index: red in the first second, blue in the second (BT.601 limited-range YUV).
inline void fillVideo(AVFrame* f, int index)
{
	av_frame_make_writable(f);
	const bool red = index < Fps;
	const int values[3] = {red ? 81 : 41, red ? 90 : 240, red ? 240 : 110};
	for (int plane = 0; plane < 3; ++plane)
	{
		const int rows = plane == 0 ? Height : Height / 2;
		for (int y = 0; y < rows; ++y) { std::memset(f->data[plane] + y * f->linesize[plane], values[plane], f->linesize[plane]); }
	}
	f->pts = index;
}

inline void fillAudio(AVFrame* f, int64_t start)
{
	av_frame_make_writable(f);
	auto* samples = reinterpret_cast<int16_t*>(f->data[0]);
	for (int i = 0; i < AudioChunk; ++i)
	{
		const auto v = static_cast<int16_t>(8000.0 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(start + i) / Rate));
		samples[2 * i] = v;
		samples[2 * i + 1] = v;
	}
	f->pts = start;
}

inline void send(AVFormatContext* out, Encoder& e, AVFrame* frame)
{
	if (avcodec_send_frame(e.codec, frame) < 0) { return; }
	AVPacket* packet = av_packet_alloc();
	while (avcodec_receive_packet(e.codec, packet) == 0)
	{
		av_packet_rescale_ts(packet, e.codec->time_base, e.stream->time_base);
		packet->stream_index = e.stream->index;
		av_interleaved_write_frame(out, packet);
	}
	av_packet_free(&packet);
}

inline void writeAll(AVFormatContext* out, Encoder& video, Encoder& audio)
{
	int64_t audioAt = 0;
	for (int i = 0; i < Frames; ++i)
	{
		fillVideo(video.frame, i);
		send(out, video, video.frame);
		for (; audioAt * Fps < static_cast<int64_t>(i + 1) * Rate; audioAt += AudioChunk)
		{
			fillAudio(audio.frame, audioAt);
			send(out, audio, audio.frame);
		}
	}
	send(out, video, nullptr);
	send(out, audio, nullptr);
}

inline bool encodeWithLibavcodec(const QString& path)
{
	const QByteArray name = QFile::encodeName(path);
	AVFormatContext* out = nullptr;
	if (avformat_alloc_output_context2(&out, nullptr, nullptr, name.constData()) < 0) { return false; }
	bool ok = false;
	{
		Encoder video;
		Encoder audio;
		ok = addStream(out, video, AV_CODEC_ID_MPEG4, setupVideo) && addStream(out, audio, AV_CODEC_ID_PCM_S16LE, setupAudio)
			&& avio_open(&out->pb, name.constData(), AVIO_FLAG_WRITE) >= 0 && avformat_write_header(out, nullptr) >= 0;
		if (ok)
		{
			writeAll(out, video, audio);
			ok = av_write_trailer(out) >= 0;
		}
	}
	avio_closep(&out->pb);
	avformat_free_context(out);
	return ok;
}

inline bool encodeWithProgram(const QString& program, const QString& path)
{
	const QString seconds = QStringLiteral(":s=%1x%2:r=%3:d=1").arg(Width).arg(Height).arg(Fps);
	const QStringList args{QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
		QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=red") + seconds,
		QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=blue") + seconds,
		QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:sample_rate=%1:duration=2").arg(Rate),
		QStringLiteral("-filter_complex"), QStringLiteral("[0:v][1:v]concat=n=2:v=1:a=0[v]"), QStringLiteral("-map"), QStringLiteral("[v]"),
		QStringLiteral("-map"), QStringLiteral("2:a"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"), QStringLiteral("10"),
		QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), QStringLiteral("-ac"), QStringLiteral("2"), path};
	return QProcess::execute(program, args) == 0 && QFile::exists(path);
}

//! Write the clip to @a path (a .mkv): through the ffmpeg program when there is one, else with libavcodec.
inline bool makeClip(const QString& path)
{
	const QString program = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
	if (!program.isEmpty() && encodeWithProgram(program, path)) { return true; }
	return encodeWithLibavcodec(path);
}

} // namespace videotest

#endif // ZENE_HAVE_FFMPEG

#endif // LMMS_VIDEO_TEST_CLIP_H
