/*
 * ControlCommandsVideoRender.cpp - video.render_frames: the video track's picture at each song tick of a range
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

/*! Q050-180.1: a range render of the project's video track. The range [from, to) is sampled at 'fps' frames per
 *  second of SONG time - so the sample ticks follow the project's tempo map - and each sample is the frame of the clip
 *  active at that tick (VideoReference::clipAt), at that clip's own time through its song-tick offset. Before the
 *  first clip, after the last one and in a gap the image is black: a frame of another clip is never carried over.
 *  Each image is written as 'frame-<n>.png' into 'out'; the reply lists every sample (tick, song seconds, clip id,
 *  clip seconds, file). Runs on the interface thread, decoding with its own VideoDecoder per file, never on the
 *  audio thread. No compositing and no encoded video: images only. */

#include <map>
#include <memory>

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QPainter>

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "VideoCommands.h"
#include "VideoDecoder.h"
#include "VideoReference.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxFrames = 10000;  //!< a render of more samples is refused (about 6.7 minutes at 25 fps)
constexpr double MaxFps = 240.0;
constexpr int MaxSide = 4096;

struct RenderRequest
{
	tick_t from = 0;
	tick_t to = 0;
	double fps = 25.0;
	QSize box{320, 180};
	QString out;
};

ControlResult invalid(const QString& why)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs, why);
}

ControlResult readRange(const QJsonObject& args, RenderRequest* request)
{
	request->from = static_cast<tick_t>(args.value(QStringLiteral("from")).toInt(-1));
	request->to = static_cast<tick_t>(args.value(QStringLiteral("to")).toInt(-1));
	if (request->from < 0 || request->to <= request->from)
	{
		return invalid(QStringLiteral("'from' and 'to' are song ticks with 0 <= from < to"));
	}
	request->fps = args.value(QStringLiteral("fps")).toDouble(25.0);
	if (!(request->fps > 0.0 && request->fps <= MaxFps)) { return invalid(QStringLiteral("'fps' is in (0, 240]")); }
	return ControlResult::success({});
}

ControlResult readRequest(const QJsonObject& args, RenderRequest* request)
{
	const ControlResult range = readRange(args, request);
	if (!range.ok) { return range; }
	request->box = QSize(args.value(QStringLiteral("width")).toInt(320), args.value(QStringLiteral("height")).toInt(180));
	const auto fits = [](int side) { return side >= 1 && side <= MaxSide; };
	if (!fits(request->box.width()) || !fits(request->box.height()))
	{
		return invalid(QStringLiteral("'width' and 'height' are 1..4096 pixels"));
	}
	request->out = args.value(QStringLiteral("out")).toString();
	if (request->out.isEmpty()) { return invalid(QStringLiteral("'out' names the folder the frames are written to")); }
	if (!QDir().mkpath(request->out))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot create the folder '%1'").arg(request->out));
	}
	return ControlResult::success({});
}

//! The song ticks sampled: one per 1/fps of song time from 'from', while still before 'to'.
QVector<tick_t> sampleTicks(const RenderRequest& request, bool* tooMany)
{
	Song* song = Engine::getSong();
	const double start = song->secondsAtTick(request.from);
	const double end = song->secondsAtTick(request.to);
	QVector<tick_t> ticks;
	*tooMany = (end - start) * request.fps > MaxFrames;
	if (*tooMany) { return ticks; }
	for (int index = 0;; ++index)
	{
		const double seconds = start + index / request.fps;
		if (seconds >= end) { break; }
		const tick_t tick = std::max(request.from,
			song->tempoMap().map().tickAtSeconds(seconds, song->tempoModel().value()));
		if (tick >= request.to) { break; }
		ticks.append(tick);
	}
	return ticks;
}

//! The frame of @a clip at @a seconds of it, letterboxed onto a black @a box; false when nothing decoded.
bool drawClipFrame(std::map<QString, std::unique_ptr<VideoDecoder>>& decoders, const VideoAttachment& clip,
	double seconds, QImage* canvas)
{
	std::unique_ptr<VideoDecoder>& decoder = decoders[clip.path];
	if (decoder == nullptr)
	{
		decoder = std::make_unique<VideoDecoder>();
		QString why;
		if (!decoder->open(clip.path, &why)) { return false; }
	}
	if (!decoder->isOpen()) { return false; }
	const QImage frame = decoder->frameAt(seconds, canvas->size());
	if (frame.isNull()) { return false; }
	QPainter painter(canvas);
	const QSize size = frame.size().scaled(canvas->size(), Qt::KeepAspectRatio);
	painter.drawImage(QRect(canvas->rect().center() - QPoint(size.width() / 2, size.height() / 2), size), frame);
	return true;
}

ControlResult renderFrames(const QJsonObject& args)
{
	if (!VideoDecoder::available())
	{
		return ControlResult::failure(ControlErrorKind::Requires, VideoDecoder::unavailableReason());
	}
	RenderRequest request;
	const ControlResult read = readRequest(args, &request);
	if (!read.ok) { return read; }
	bool tooMany = false;
	const QVector<tick_t> ticks = sampleTicks(request, &tooMany);
	if (tooMany)
	{
		return invalid(QStringLiteral("the range at that fps is more than %1 frames; render it in parts").arg(MaxFrames));
	}
	const VideoReference& video = VideoReference::instance();
	const QDir dir(request.out);
	std::map<QString, std::unique_ptr<VideoDecoder>> decoders;
	QJsonArray frames;
	for (int index = 0; index < ticks.size(); ++index)
	{
		const tick_t tick = ticks[index];
		QImage canvas(request.box, QImage::Format_RGB32);
		canvas.fill(Qt::black);
		QJsonObject item{{QStringLiteral("index"), index}, {QStringLiteral("tick"), static_cast<qint64>(tick)},
			{QStringLiteral("seconds"), Engine::getSong()->secondsAtTick(tick)}, {QStringLiteral("clip"), QString()}};
		if (const VideoAttachment* clip = video.clipAt(tick))
		{
			const double seconds = video.secondsAt(tick, *clip);
			item.insert(QStringLiteral("clip"), clip->id);
			item.insert(QStringLiteral("clip_seconds"), seconds);
			item.insert(QStringLiteral("decoded"), drawClipFrame(decoders, *clip, seconds, &canvas));
		}
		const QString file = dir.filePath(QStringLiteral("frame-%1.png").arg(index, 5, 10, QLatin1Char('0')));
		if (!canvas.save(file, "PNG"))
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot write '%1'").arg(file));
		}
		item.insert(QStringLiteral("file"), file);
		frames.append(item);
	}
	return ControlResult::success({{QStringLiteral("out"), dir.absolutePath()}, {QStringLiteral("count"), frames.size()},
		{QStringLiteral("fps"), request.fps}, {QStringLiteral("width"), request.box.width()},
		{QStringLiteral("height"), request.box.height()}, {QStringLiteral("frames"), frames}});
}

} // namespace

void registerVideoRenderCommand(ControlRegistry& registry)
{
	const QJsonObject properties{{QStringLiteral("from"), tickProperty()}, {QStringLiteral("to"), tickProperty()},
		{QStringLiteral("out"), stringProperty()}, {QStringLiteral("fps"), numberProperty()},
		{QStringLiteral("width"), integerProperty()}, {QStringLiteral("height"), integerProperty()}};
	defineCommandWithSchema(registry, QStringLiteral("video.render_frames"), QStringLiteral("Render the project's video "
		"track over the song range 'from'..'to' (ticks, 'to' excluded) as PNG images 'frame-<n>.png' in the folder 'out': "
		"one image per 1/'fps' second of song time (25 when left out), so the sampled ticks follow the tempo map. Each image "
		"is the frame of the clip active at that tick, at that clip's own time from its offset, letterboxed into "
		"'width' x 'height' (320 x 180); before, after and between clips it is black - never another clip's frame. The "
		"reply lists every frame: 'tick', song 'seconds', 'clip' id (empty outside every clip), 'clip_seconds', whether it "
		"'decoded', and its 'file'. At most 10000 frames. Writes files only; the project is not changed."),
		objectSchema(properties, QJsonArray{QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("out")}), false,
		renderFrames);
}

} // namespace lmms
