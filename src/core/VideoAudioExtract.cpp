/*
 * VideoAudioExtract.cpp - a video's sound decoded to a stereo WAV through FFmpeg (libswresample)
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

#include "VideoAudioExtract.h"

#include <QFile>
#include <QObject>

#include "VideoDecoder.h"
#include "VideoFfmpeg.h"

#ifdef ZENE_HAVE_FFMPEG

#include <vector>

#include <sndfile.h>

extern "C" {
#include <libswresample/swresample.h>
}

#endif

namespace lmms
{

#ifdef ZENE_HAVE_FFMPEG

namespace
{

//! The decoder's resampler and the WAV it writes to, closed on destruction.
struct WavSink
{
	SwrContext* resampler = nullptr;
	SNDFILE* file = nullptr;
	std::vector<float> buffer;
	long long frames = 0;

	WavSink() = default;
	WavSink(const WavSink&) = delete;
	WavSink& operator=(const WavSink&) = delete;
	~WavSink()
	{
		swr_free(&resampler);
		if (file != nullptr) { sf_close(file); }
	}

	bool openResampler(const AVCodecContext* codec)
	{
		AVChannelLayout in{};
		if (codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) { av_channel_layout_default(&in, codec->ch_layout.nb_channels); }
		else { av_channel_layout_copy(&in, &codec->ch_layout); }
		const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		const int made = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_FLT, codec->sample_rate, &in, codec->sample_fmt,
			codec->sample_rate, 0, nullptr);
		av_channel_layout_uninit(&in);
		return made >= 0 && swr_init(resampler) >= 0;
	}

	bool openFile(const QString& wav, int sampleRate)
	{
		SF_INFO info{};
		info.samplerate = sampleRate;
		info.channels = 2;
		info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
		file = sf_open(wav.toLocal8Bit().constData(), SFM_WRITE, &info);
		return file != nullptr;
	}

	//! Close the WAV this sink wrote and delete it (called only after openFile succeeded).
	void discard(const QString& wav)
	{
		sf_close(file);
		file = nullptr;
		QFile::remove(wav);
	}

	//! Convert @a count input frames (none flushes what the resampler holds) and write them.
	void write(const uint8_t** input, int count)
	{
		const int room = swr_get_out_samples(resampler, count);
		if (room <= 0) { return; }
		buffer.resize(static_cast<std::size_t>(room) * 2);
		auto* out = reinterpret_cast<uint8_t*>(buffer.data());
		const int got = swr_convert(resampler, &out, room, input, count);
		if (got > 0) { frames += sf_writef_float(file, buffer.data(), got); }
	}
};

//! Decode every frame of @a media into @a sink.
void decodeAll(video::MediaStream& media, WavSink& sink)
{
	AVFrame* frame = av_frame_alloc();
	AVPacket* packet = av_packet_alloc();
	while (media.receive(frame, packet))
	{
		sink.write(const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
	}
	sink.write(nullptr, 0);
	av_frame_free(&frame);
	av_packet_free(&packet);
}

//! Write @a video's audio to @a wav; the error text, or empty on success.
QString extract(const QString& video, const QString& wav, VideoAudio* out)
{
	video::MediaStream media;
	QString why;
	if (!media.open(video, AVMEDIA_TYPE_AUDIO, &why)) { return QObject::tr("cannot read the video's audio (%1)").arg(why); }
	if (media.codec->sample_rate <= 0) { return QObject::tr("the video's audio has no sample rate"); }
	WavSink sink;
	if (!sink.openResampler(media.codec)) { return QObject::tr("the video's audio cannot be converted to stereo"); }
	if (!sink.openFile(wav, media.codec->sample_rate)) { return QObject::tr("cannot write '%1': %2").arg(wav, QString::fromUtf8(sf_strerror(nullptr))); }
	decodeAll(media, sink);
	if (sink.frames <= 0)
	{
		sink.discard(wav);
		return QObject::tr("no audio decoded from the video");
	}
	*out = VideoAudio{wav, media.codec->sample_rate, sink.frames};
	return QString();
}

} // namespace

bool extractVideoAudio(const QString& video, const QString& wav, VideoAudio* out, QString* error)
{
	*error = extract(video, wav, out);
	return error->isEmpty();
}

#else // no FFmpeg

bool extractVideoAudio(const QString&, const QString&, VideoAudio*, QString* error)
{
	*error = VideoDecoder::unavailableReason();
	return false;
}

#endif // ZENE_HAVE_FFMPEG

} // namespace lmms
