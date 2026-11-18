/*
 * VideoReference.cpp - the video a project is scored to: its file, where it starts, its rate and length
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

#include "VideoReference.h"

#include <algorithm>
#include <cmath>

#include <QDomDocument>
#include <QDomElement>

#include "Engine.h"
#include "Song.h"
#include "VideoDecoder.h"

namespace lmms
{

VideoReference& VideoReference::instance()
{
	static VideoReference* reference = [] {
		auto* made = new VideoReference();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *reference;
}

namespace
{

//! The highest n among ids "video<n>" in @a clips (0 when none).
int highestClipNumber(const QVector<VideoAttachment>& clips)
{
	int highest = 0;
	for (const VideoAttachment& clip : clips)
	{
		if (!clip.id.startsWith(QStringLiteral("video"))) { continue; }
		bool ok = false;
		const int number = clip.id.mid(5).toInt(&ok);
		if (ok) { highest = std::max(highest, number); }
	}
	return highest;
}

} // namespace

const VideoAttachment& VideoReference::attachment() const
{
	static const VideoAttachment empty;
	return m_clips.isEmpty() ? empty : m_clips.first();
}

void VideoReference::set(const VideoAttachment& video)
{
	setClips(video.attached() ? QVector<VideoAttachment>{video} : QVector<VideoAttachment>{});
}

void VideoReference::setClips(const QVector<VideoAttachment>& clips)
{
	// Clips without a file are dropped; a missing or repeated id gets the next free "video<n>".
	QVector<VideoAttachment> normalized;
	QStringList used;
	int highest = highestClipNumber(clips);
	for (VideoAttachment clip : clips)
	{
		if (!clip.attached()) { continue; }
		if (clip.id.isEmpty() || used.contains(clip.id)) { clip.id = QStringLiteral("video%1").arg(++highest); }
		used.append(clip.id);
		normalized.append(clip);
	}
	if (normalized == m_clips) { return; }
	m_clips = normalized;
	emit changed();
}

const VideoAttachment* VideoReference::clip(const QString& id) const
{
	for (const VideoAttachment& clip : m_clips)
	{
		if (clip.id == id) { return &clip; }
	}
	return nullptr;
}

const VideoAttachment* VideoReference::clipAt(tick_t tick) const
{
	for (const VideoAttachment& clip : m_clips)
	{
		if (clip.attached() && tick >= clip.offset && tick < endTick(clip)) { return &clip; }
	}
	return nullptr;
}

double VideoReference::secondsAt(tick_t tick, const VideoAttachment& clip) const
{
	const Song* song = Engine::getSong();
	return song->secondsAtTick(tick) - song->secondsAtTick(clip.offset);
}

tick_t VideoReference::tickAt(double seconds, const VideoAttachment& clip) const
{
	Song* song = Engine::getSong();
	const double songSeconds = std::max(0.0, song->secondsAtTick(clip.offset) + seconds);
	return song->tempoMap().map().tickAtSeconds(songSeconds, song->tempoModel().value());
}

double VideoReference::secondsAt(tick_t tick) const
{
	const VideoAttachment* active = clipAt(tick);
	return secondsAt(tick, active != nullptr ? *active : attachment());
}

tick_t VideoReference::tickAt(double seconds) const
{
	return tickAt(seconds, attachment());
}

QString VideoReference::timecode(double seconds, double fps)
{
	const double rate = fps > 0.0 ? fps : 25.0;
	const double magnitude = std::abs(seconds);
	const auto whole = static_cast<long long>(std::floor(magnitude + 1e-9));
	const int frame = std::clamp(static_cast<int>(std::floor((magnitude - static_cast<double>(whole)) * rate + 1e-6)), 0,
		std::max(0, static_cast<int>(std::ceil(rate)) - 1));
	return QStringLiteral("%1%2:%3:%4:%5")
		.arg(seconds < 0.0 ? QStringLiteral("-") : QString())
		.arg(whole / 3600, 2, 10, QLatin1Char('0'))
		.arg(whole / 60 % 60, 2, 10, QLatin1Char('0'))
		.arg(whole % 60, 2, 10, QLatin1Char('0'))
		.arg(frame, 2, 10, QLatin1Char('0'));
}

QString VideoReference::elementName() const
{
	return QStringLiteral("video");
}

namespace
{

void writeAttachmentAttributes(QDomElement& element, const VideoAttachment& video)
{
	element.setAttribute(QStringLiteral("path"), video.path);
	element.setAttribute(QStringLiteral("offset"), video.offset);
	element.setAttribute(QStringLiteral("fps"), video.fps);
	element.setAttribute(QStringLiteral("duration"), video.duration);
	element.setAttribute(QStringLiteral("width"), video.width);
	element.setAttribute(QStringLiteral("height"), video.height);
	element.setAttribute(QStringLiteral("audio"), video.hasAudio ? 1 : 0);
}

VideoAttachment readAttachmentAttributes(const QDomElement& element)
{
	VideoAttachment video;
	video.path = element.attribute(QStringLiteral("path"));
	video.offset = std::max(0, element.attribute(QStringLiteral("offset")).toInt());
	video.fps = element.attribute(QStringLiteral("fps")).toDouble();
	bool durationOk = false;
	const double duration = element.attribute(QStringLiteral("duration")).toDouble(&durationOk);
	if (durationOk && std::isfinite(duration) && duration >= 0.0)
	{
		video.duration = duration;
	}
	else
	{
		VideoProbe probe;
		QString why;
		video.duration = VideoDecoder::probe(video.path, &probe, &why) && std::isfinite(probe.duration)
			? std::max(0.0, probe.duration) : 0.0;
	}
	video.width = element.attribute(QStringLiteral("width")).toInt();
	video.height = element.attribute(QStringLiteral("height")).toInt();
	video.hasAudio = element.attribute(QStringLiteral("audio")).toInt() != 0;
	video.id = element.attribute(QStringLiteral("id"));
	return video;
}

//! Every <clip> child of @a element; the legacy <video> attributes as one clip when it has none.
QVector<VideoAttachment> readClips(const QDomElement& element)
{
	QVector<VideoAttachment> clips;
	for (QDomElement child = element.firstChildElement(QStringLiteral("clip")); !child.isNull();
		child = child.nextSiblingElement(QStringLiteral("clip")))
	{
		clips.append(readAttachmentAttributes(child));
	}
	if (clips.isEmpty()) { clips.append(readAttachmentAttributes(element)); }
	return clips;  // setClips() drops clips without a file and gives each a unique id
}

} // namespace

void VideoReference::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	// The legacy view: the first clip, so a build that predates multiple clips
	// opens the project with the first one instead of no video at all.
	writeAttachmentAttributes(element, attachment());
	for (const VideoAttachment& clip : m_clips)
	{
		QDomElement child = doc.createElement(QStringLiteral("clip"));
		writeAttachmentAttributes(child, clip);
		child.setAttribute(QStringLiteral("id"), clip.id);
		element.appendChild(child);
	}
	parent.appendChild(element);
}

void VideoReference::load(const QDomElement& element)
{
	setClips(readClips(element));
}

void VideoReference::clear()
{
	setClips({});
}

} // namespace lmms
