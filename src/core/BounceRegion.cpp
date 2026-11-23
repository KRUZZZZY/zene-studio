/*
 * BounceRegion.cpp - the render side of a range bounce that replaces only its region
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

#include "BounceRegion.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <sndfile.h>

#include "Clip.h"
#include "Track.h"

namespace lmms::bouncefix
{

namespace
{

//! Below this a sample is silence (about -80 dBFS): the floor under which a tail is not worth landing.
constexpr float SilenceFloor = 1.0e-4f;
constexpr int BlockFrames = 4096;

//! The clip elements of \a track that sit on the timeline: a position and a length, as every clip saves them.
std::vector<QDomElement> timelineClips(const QDomElement& track)
{
	std::vector<QDomElement> clips;
	for (QDomElement child = track.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		if (!child.hasAttribute(QStringLiteral("pos")) || !child.hasAttribute(QStringLiteral("len"))) { continue; }
		if (child.attribute(QStringLiteral("pos")).toInt() < 0 || child.attribute(QStringLiteral("len")).toInt() <= 0) { continue; }
		clips.push_back(child);
	}
	return clips;
}

bool clipIsMuted(const QDomElement& clip) { return clip.attribute(QStringLiteral("muted")).toInt() != 0; }

//! Cuts \a clip at \a edge when it crosses it, the way splitClipAlone does: the left piece is shortened and the
//! right piece starts at the edge with its start offset moved back by the left piece's length.
void splitElementAt(QDomElement clip, tick_t edge)
{
	const int position = clip.attribute(QStringLiteral("pos")).toInt();
	const int length = clip.attribute(QStringLiteral("len")).toInt();
	if (clipIsMuted(clip) || edge <= position || edge >= position + length) { return; }
	const int leftLength = edge - position;
	QDomElement right = clip.cloneNode(true).toElement();
	clip.setAttribute(QStringLiteral("len"), leftLength);
	right.setAttribute(QStringLiteral("pos"), static_cast<int>(edge));
	right.setAttribute(QStringLiteral("len"), length - leftLength);
	right.setAttribute(QStringLiteral("off"), clip.attribute(QStringLiteral("off")).toInt() - leftLength);
	clip.parentNode().insertAfter(right, clip);
}

//! True when the last audible frame of \a block (frames of \a channels) is found; \a at gets its index.
bool lastAudibleIn(const std::vector<float>& block, sf_count_t frames, int channels, sf_count_t* at)
{
	for (sf_count_t frame = frames; frame > 0; --frame)
	{
		for (int channel = 0; channel < channels; ++channel)
		{
			if (std::fabs(block[static_cast<size_t>((frame - 1) * channels + channel)]) > SilenceFloor)
			{
				*at = frame - 1;
				return true;
			}
		}
	}
	return false;
}

} // namespace

void isolateRegionInTrackElement(QDomElement track, tick_t start, tick_t end)
{
	for (const tick_t edge : {start, end})
	{
		for (QDomElement clip : timelineClips(track)) { splitElementAt(clip, edge); }
	}
	for (QDomElement clip : timelineClips(track))
	{
		const int position = clip.attribute(QStringLiteral("pos")).toInt();
		if (!clipIsMuted(clip) && (position < start || position >= end)) { clip.setAttribute(QStringLiteral("muted"), 1); }
	}
}

tick_t tailTicksPastRange(const std::vector<Track*>& tracks, const BounceInPlace::Range& range)
{
	tick_t room = MaxTailTicks;
	for (const Track* track : tracks)
	{
		if (track == nullptr || track->isFrozen()) { return 0; }
		if (static_cast<int>(track->type()) >= static_cast<int>(Track::Type::Automation)) { continue; }
		for (const Clip* clip : track->getClips())
		{
			if (clip == nullptr || clip->isMuted()) { continue; }
			const tick_t at = clip->startPosition().getTicks();
			if (at >= range.end) { room = std::min(room, at - range.end); }
		}
	}
	return room;
}

qint64 audibleFramesIn(const QString& wav, qint64 from, qint64 maxFrames)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(wav.toLocal8Bit().constData(), SFM_READ, &info);
	if (file == nullptr) { return 0; }
	qint64 audible = 0;
	if (info.channels > 0 && from >= 0 && sf_seek(file, from, SEEK_SET) >= 0)
	{
		std::vector<float> block(static_cast<size_t>(info.channels) * BlockFrames);
		qint64 position = 0;
		while (position < maxFrames)
		{
			const sf_count_t got = sf_readf_float(file, block.data(), std::min<qint64>(BlockFrames, maxFrames - position));
			if (got <= 0) { break; }
			sf_count_t last = 0;
			if (lastAudibleIn(block, got, info.channels, &last)) { audible = position + last + 1; }
			position += got;
		}
	}
	sf_close(file);
	return audible;
}

} // namespace lmms::bouncefix
