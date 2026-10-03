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
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <atomic>
#include <limits>
#include <utility>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectMarkers.h"
#include "Song.h"

namespace lmms::markeractions
{

namespace
{

//! The mark while the watcher is not following a pass (below every tick, so a start at 0 can sit before it).
constexpr tick_t Unwatched = std::numeric_limits<tick_t>::min();

tick_t& lastSeen()
{
	static tick_t value = Unwatched;
	return value;
}

//! Counts the playing song's position jumps (a seek, a loop wrap). Bumped on whichever thread moved the
//! timeline - the render thread for a loop - so it is only an atomic counter, nothing else.
std::atomic<unsigned>& jumps()
{
	static std::atomic<unsigned> value{0};
	return value;
}

//! The mark a fresh start takes: one tick before where the song's playback started, so a marker ON the
//! start tick runs too, when that is where it really played from up to @a now; Unwatched when not (an
//! enforced loop moved it, or a count-in began the pass before the recorded start).
tick_t startMark(const Song& song, tick_t now)
{
	const Timeline& timeline = song.getTimeline(Song::PlayMode::Song);
	const tick_t from = timeline.playStartPosition().getTicks();
	if (from < 0 || from > now) { return Unwatched; }
	const bool outsideLoop = from < timeline.loopBegin().getTicks() || from >= timeline.loopEnd().getTicks();
	return timeline.loopEnabled() && outsideLoop ? Unwatched : from - 1;
}

//! One look of the timer. Only the arrangement's playback crosses markers (a pattern or a clip played
//! alone has its own ticks). A fresh start takes its mark from where playback started, so a marker
//! crossed before this first look still runs; a resume from pause is not a fresh start. A position jump
//! (a seek, a loop wrap) only moves the mark: a forward seek must not run the markers it skipped.
void watch(Song& song)
{
	static bool paused = false;
	static unsigned seenJumps = 0;
	const unsigned jumpsBefore = jumps().load(std::memory_order_acquire);
	const bool playing = song.isPlaying() && song.playMode() == Song::PlayMode::Song;
	const tick_t now = song.getPlayPos().getTicks();
	if (jumps().load(std::memory_order_acquire) != jumpsBefore) { return; }  // raced a jump: the next look
	const bool jumped = std::exchange(seenJumps, jumpsBefore) != jumpsBefore;
	if (!playing) { paused = song.isPaused(); }
	else if (jumped) { lastSeen() = now; }
	else if (lastSeen() == Unwatched && !paused) { lastSeen() = startMark(song, now); }
	advance(now, playing);
}

//! Follows @a song's position jumps (once per song; the watcher may start before the song exists).
void followJumps(Song* song)
{
	static QPointer<Song> followed;  // a QPointer: a new song at a gone one's address is followed too
	if (song == followed) { return; }
	followed = song;
	QObject::connect(song, &Song::playbackPositionJumped, song,
		[] { jumps().fetch_add(1, std::memory_order_release); }, Qt::DirectConnection);
}

} // namespace

int advance(tick_t tick, bool playing)
{
	tick_t& last = lastSeen();
	if (!playing || last == Unwatched || tick < last)
	{
		last = playing ? tick : Unwatched;
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
		if (song == nullptr) { return; }
		followJumps(song);
		watch(*song);
	});
	timer->start();
}

} // namespace lmms::markeractions
