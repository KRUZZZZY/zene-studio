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

void VideoReference::set(const VideoAttachment& video)
{
	if (video == m_video) { return; }
	m_video = video;
	emit changed();
}

double VideoReference::secondsAt(tick_t tick) const
{
	const Song* song = Engine::getSong();
	return song->secondsAtTick(tick) - song->secondsAtTick(m_video.offset);
}

tick_t VideoReference::tickAt(double seconds) const
{
	Song* song = Engine::getSong();
	const double songSeconds = std::max(0.0, song->secondsAtTick(m_video.offset) + seconds);
	return song->tempoMap().map().tickAtSeconds(songSeconds, song->tempoModel().value());
}

QString VideoReference::timecode(double seconds, double fps)
{
	const double rate = fps > 0.0 ? fps : 25.0;
	const double magnitude = std::abs(seconds);
	const auto whole = static_cast<long long>(std::floor(magnitude + 1e-9));
	const int frame = std::clamp(static_cast<int>(std::floor((magnitude - static_cast<double>(whole)) * rate + 1e-6)), 0,
		std::max(0, static_cast<int>(std::ceil(rate)) - 1));
	return QStringLiteral("%1%2:%3:%4:%5")
		.arg(seconds < 0.0 && (whole > 0 || frame > 0) ? QStringLiteral("-") : QString())
		.arg(whole / 3600, 2, 10, QLatin1Char('0'))
		.arg(whole / 60 % 60, 2, 10, QLatin1Char('0'))
		.arg(whole % 60, 2, 10, QLatin1Char('0'))
		.arg(frame, 2, 10, QLatin1Char('0'));
}

QString VideoReference::elementName() const
{
	return QStringLiteral("video");
}

void VideoReference::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	element.setAttribute(QStringLiteral("path"), m_video.path);
	element.setAttribute(QStringLiteral("offset"), m_video.offset);
	element.setAttribute(QStringLiteral("fps"), m_video.fps);
	element.setAttribute(QStringLiteral("duration"), m_video.duration);
	element.setAttribute(QStringLiteral("width"), m_video.width);
	element.setAttribute(QStringLiteral("height"), m_video.height);
	element.setAttribute(QStringLiteral("audio"), m_video.hasAudio ? 1 : 0);
	parent.appendChild(element);
}

void VideoReference::load(const QDomElement& element)
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
	set(video);
}

void VideoReference::clear()
{
	set(VideoAttachment());
}

} // namespace lmms
