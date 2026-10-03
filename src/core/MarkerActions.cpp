/*
 * MarkerActions.cpp - commands run when playback crosses a marker
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
#include "MarkerActions.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVector>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectMarkers.h"
#include "Song.h"

namespace lmms::markeractions
{

namespace
{

tick_t& lastSeen()
{
	static tick_t value = -1;
	return value;
}

//! Where the song's playback started, when that is where it really played from up to @a now; -1 when
//! not (an enforced loop moved it, or a count-in began the pass before the recorded start).
tick_t playedFrom(const Song& song, tick_t now)
{
	const Timeline& timeline = song.getTimeline(Song::PlayMode::Song);
	const tick_t from = timeline.playStartPosition().getTicks();
	if (from < 0 || from > now) { return -1; }
	const bool outsideLoop = from < timeline.loopBegin().getTicks() || from >= timeline.loopEnd().getTicks();
	return timeline.loopEnabled() && outsideLoop ? -1 : from;
}

//! One look of the timer. Only the arrangement's playback crosses markers (a pattern or a clip played
//! alone has its own ticks). A fresh start takes its mark from where playback started, so a marker
//! crossed before this first look still runs; a resume from pause is not a fresh start.
void watch(Song& song)
{
	static bool paused = false;
	const bool playing = song.isPlaying() && song.playMode() == Song::PlayMode::Song;
	const tick_t now = song.getPlayPos().getTicks();
	if (!playing) { paused = song.isPaused(); }
	else if (lastSeen() < 0 && !paused) { lastSeen() = playedFrom(song, now); }
	advance(now, playing);
}

} // namespace

int advance(tick_t tick, bool playing)
{
	tick_t& last = lastSeen();
	if (!playing || last < 0 || tick < last)
	{
		last = playing ? tick : -1;
		return 0;
	}
	// Copied first: an action may edit the markers themselves.
	QVector<Marker> crossed;
	for (const Marker& marker : ProjectMarkers::instance().markers())
	{
		if (!marker.action.isEmpty() && marker.tick > last && marker.tick <= tick) { crossed.append(marker); }
	}
	last = tick;
	for (const Marker& marker : crossed)
	{
		ControlRegistry::instance()->invoke(marker.action, QJsonDocument::fromJson(marker.args.toUtf8()).object());
	}
	return static_cast<int>(crossed.size());
}

void start()
{
	static QTimer* timer = nullptr;
	if (timer != nullptr) { return; }
	timer = new QTimer();
	timer->setInterval(15);
	QObject::connect(timer, &QTimer::timeout, [] {
		Song* song = Engine::getSong();
		if (song != nullptr) { watch(*song); }
	});
	timer->start();
}

} // namespace lmms::markeractions
