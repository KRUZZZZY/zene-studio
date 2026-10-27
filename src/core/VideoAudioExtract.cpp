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

#include <QObject>
#include <QSaveFile>

#include "VideoDecoder.h"
#include "VideoFfmpeg.h"

#ifdef ZENE_HAVE_FFMPEG

#include <algorithm>
#include <vector>

#include <sndfile.h>

extern "C" {
#include <libavutil/error.h>
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
	QSaveFile output;
	std::vector<float> buffer;
	long long frames = 0;
	bool writeFailed = false;
	bool convertFailed = false;
	QString error;

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
		output.setFileName(wav);
		output.setDirectWriteFallback(false);
		if (!output.open(QIODevice::WriteOnly))
		{
			error = output.errorString();
			return false;
		}
		SF_INFO info{};
		info.samplerate = sampleRate;
		info.channels = 2;
		info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
		file = sf_open_fd(output.handle(), SFM_WRITE, &info, false);
		if (file == nullptr) { error = QString::fromUtf8(sf_strerror(nullptr)); }
		return file != nullptr;
	}

	//! Close the WAV and publish it only after the full extraction succeeds.
	bool commit()
	{
		if (sf_close(file) != 0)
		{
			file = nullptr;
			error = QString::fromUtf8(sf_strerror(nullptr));
			return false;
		}
		file = nullptr;
		if (!output.commit())
		{
			error = output.errorString();
			return false;
		}
		return true;
	}

	//! Record a resampler error @a code: the decode stops and the WAV is never published.
	bool failConversion(int code)
	{
		char text[AV_ERROR_MAX_STRING_SIZE] = {};
		av_strerror(code, text, sizeof text);
		convertFailed = true;
		error = QString::fromUtf8(text);
		return false;
	}

	//! Convert @a count input frames (none flushes what the resampler holds) and write them; false once a conversion or write fails.
	bool write(const uint8_t** input, int count)
	{
		if (writeFailed || convertFailed) { return false; }
		const int room = swr_get_out_samples(resampler, count);
		if (room < 0) { return failConversion(room); }
		if (room == 0) { return true; }
		buffer.resize(static_cast<std::size_t>(room) * 2);
		auto* out = reinterpret_cast<uint8_t*>(buffer.data());
		const int got = swr_convert(resampler, &out, room, input, count);
		if (got < 0) { return failConversion(got); }
		if (got == 0) { return true; }
		const sf_count_t written = sf_writef_float(file, buffer.data(), static_cast<sf_count_t>(got));
		if (written != got)
		{
			writeFailed = true;
			error = QObject::tr("only %1 of %2 frames were written").arg(written).arg(got);
			return false;
		}
		frames += written;
		return true;
	}
};

//! The file's length in seconds, or 0 when it does not say.
double lengthOf(const video::MediaStream& media)
{
	return media.format->duration > 0 ? static_cast<double>(media.format->duration) / AV_TIME_BASE : 0.0;
}

//! Tell @a control how far @a frame is into the file; false when it asks the decode to stop.
bool reportFrame(const video::MediaStream& media, const AVFrame* frame, double length, VideoExtractControl* control)
{
	if (control == nullptr) { return true; }
	if (length > 0.0 && frame->pts != AV_NOPTS_VALUE)
	{
		control->progress.store(static_cast<float>(std::clamp(media.secondsOf(frame->pts) / length, 0.0, 1.0)));
	}
	return !control->cancel.load();
}

//! Decode every frame of @a media into @a sink; false on cancellation or a failed WAV write.
bool decodeAll(video::MediaStream& media, WavSink& sink, VideoExtractControl* control)
{
	AVFrame* frame = av_frame_alloc();
	AVPacket* packet = av_packet_alloc();
	const double length = lengthOf(media);
	bool whole = true;
	while (media.receive(frame, packet))
	{
		if (!sink.write(const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples)) { whole = false; break; }
		if (!reportFrame(media, frame, length, control)) { whole = false; break; }
	}
	if (!sink.write(nullptr, 0)) { whole = false; }
	av_frame_free(&frame);
	av_packet_free(&packet);
	return whole;
}

//! Write @a video's audio to @a wav; the error text, or empty on success.
QString extract(const QString& video, const QString& wav, VideoAudio* out, VideoExtractControl* control)
{
	video::MediaStream media;
	QString why;
	if (!media.open(video, AVMEDIA_TYPE_AUDIO, &why)) { return QObject::tr("cannot read the video's audio (%1)").arg(why); }
	if (media.codec->sample_rate <= 0) { return QObject::tr("the video's audio has no sample rate"); }
	WavSink sink;
	if (!sink.openResampler(media.codec)) { return QObject::tr("the video's audio cannot be converted to stereo"); }
	if (!sink.openFile(wav, media.codec->sample_rate)) { return QObject::tr("cannot write '%1': %2").arg(wav, sink.error); }
	if (!decodeAll(media, sink, control))
	{
		if (sink.convertFailed) { return QObject::tr("the video's audio could not be converted (%1)").arg(sink.error); }
		if (sink.writeFailed) { return QObject::tr("cannot write '%1': %2").arg(wav, sink.error); }
		return QObject::tr("the extraction was cancelled");
	}
	if (sink.frames <= 0)
	{
		return QObject::tr("no audio decoded from the video");
	}
	if (!sink.commit()) { return QObject::tr("cannot write '%1': %2").arg(wav, sink.error); }
	*out = VideoAudio{wav, media.codec->sample_rate, sink.frames};
	return QString();
}

} // namespace

bool extractVideoAudio(const QString& video, const QString& wav, VideoAudio* out, QString* error, VideoExtractControl* control)
{
	*error = extract(video, wav, out, control);
	return error->isEmpty();
}

#else // no FFmpeg

bool extractVideoAudio(const QString&, const QString&, VideoAudio*, QString* error, VideoExtractControl*)
{
	*error = VideoDecoder::unavailableReason();
	return false;
}

#endif // ZENE_HAVE_FFMPEG

bool extractVideoAudio(const QString& video, const QString& wav, VideoAudio* out, QString* error)
{
	return extractVideoAudio(video, wav, out, error, nullptr);
}

} // namespace lmms
