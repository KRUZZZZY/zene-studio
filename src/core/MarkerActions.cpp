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

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

//! The two counters as one look read them: every jump, and the loop wraps among them (Song::loopWraps).
struct Counts
{
	unsigned jumps = 0;
	std::uint64_t wraps = 0;
	bool operator!=(const Counts& other) const { return jumps != other.jumps || wraps != other.wraps; }
};

Counts& seen()
{
	static Counts value;
	return value;
}

Counts countsNow(const Song& song)
{
	return {jumps().load(std::memory_order_acquire), song.loopWraps()};
}

//! What moved the position between two looks.
enum class Moved { Not, Wrapped, Jumped, Racing };

//! Song counts a wrap before it announces the wrap's jump, so more wraps than jumps is a wrap caught
//! between the two (the next look sees it whole); a jump that is not a wrap is a seek.
Moved movedBetween(const Counts& before, const Counts& now)
{
	const unsigned jumped = now.jumps - before.jumps;
	const std::uint64_t wrapped = now.wraps - before.wraps;
	if (wrapped > jumped) { return Moved::Racing; }
	if (jumped == 0) { return Moved::Not; }
	return jumped == wrapped ? Moved::Wrapped : Moved::Jumped;
}

//! The move since the last look, taking the counts as seen - or Racing, and nothing taken, while a wrap's
//! jump is still to come. The same racing counts on a second look are a wrap whose jump was never
//! announced (the transport stopped and started again under it): a seek, then, so the watcher goes on.
Moved settle(const Counts& now)
{
	static Counts racing;
	const Moved moved = movedBetween(seen(), now);
	if (moved == Moved::Racing && now != std::exchange(racing, now)) { return Moved::Racing; }
	seen() = now;
	return moved == Moved::Racing ? Moved::Jumped : moved;
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

//! A look while the arrangement plays. A wrap runs what the pass crossed up to the loop end and then
//! what the new pass crossed from the loop begin; a seek only moves the mark (a forward seek must not run
//! the markers it skipped); a fresh start takes its mark from where playback started.
void follow(const Song& song, Moved moved, tick_t now, bool paused)
{
	if (moved == Moved::Wrapped)
	{
		const Timeline& timeline = song.getTimeline(Song::PlayMode::Song);
		advanceWrapped(now, timeline.loopBegin().getTicks(), timeline.loopEnd().getTicks());
		return;
	}
	if (moved == Moved::Jumped) { lastSeen() = now; }
	else if (lastSeen() == Unwatched && !paused) { lastSeen() = startMark(song, now); }
	advance(now, true);
}

//! One look of the timer. Only the arrangement's playback crosses markers (a pattern or a clip played
//! alone has its own ticks). A resume from pause is not a fresh start. Stopped, the counters are taken
//! as they stand, so a wrap whose jump went unannounced (the transport stopped under it) is forgotten;
//! paused, the current position stays as the mark from which resumed playback is checked.
void watch(Song& song)
{
	static bool paused = false;
	const Counts before = countsNow(song);
	const bool playing = song.isPlaying() && song.playMode() == Song::PlayMode::Song;
	const tick_t now = song.getPlayPos().getTicks();
	const Counts after = countsNow(song);
	if (after != before) { return; }  // raced a jump: the next look
	if (!playing)
	{
		paused = song.isPaused();
		seen() = after;
		if (paused) { lastSeen() = now; }
		else { advance(now, false); }
		return;
	}
	const Moved moved = settle(after);
	if (moved != Moved::Racing) { follow(song, moved, now, paused); }
}

//! Follows @a song's position jumps (once per song; the watcher may start before the song exists).
void followJumps(Song* song)
{
	static QPointer<Song> followed;  // a QPointer: a new song at a gone one's address is followed too
	if (song == followed) { return; }
	followed = song;
	QObject::connect(song, &Song::playbackPositionJumped, song,
		[] { jumps().fetch_add(1, std::memory_order_release); }, Qt::DirectConnection);
	// After the connection: a wrap from here on is counted on both sides.
	seen() = countsNow(*song);
}

//! Runs one crossed marker action and reports malformed arguments or command refusals.
void runMarkerAction(const Marker& marker)
{
	QJsonObject args;
	if (!marker.args.isEmpty())
	{
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(marker.args.toUtf8(), &parseError);
		if (parseError.error != QJsonParseError::NoError)
		{
			qWarning("marker action '%s' has invalid args JSON: %s",
				qPrintable(marker.action), qPrintable(parseError.errorString()));
			return;
		}
		if (!document.isObject())
		{
			qWarning("marker action '%s' args must be a JSON object", qPrintable(marker.action));
			return;
		}
		args = document.object();
	}

	const ControlResult result = ControlRegistry::instance()->invoke(marker.action, args);
	if (!result.ok)
	{
		qWarning("marker action '%s' failed: %s", qPrintable(marker.action), qPrintable(result.errorMessage));
	}
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
	// Copied first: an action may edit the markers themselves. ProjectMarkers keeps them in tick order.
	QVector<Marker> crossed;
	for (const Marker& marker : ProjectMarkers::instance().markers())
	{
		if (!marker.action.isEmpty() && marker.tick > last && marker.tick <= tick) { crossed.append(marker); }
	}
	last = tick;
	for (const Marker& marker : crossed)
	{
		runMarkerAction(marker);
	}
	return static_cast<int>(crossed.size());
}

int advanceWrapped(tick_t tick, tick_t loopBegin, tick_t loopEnd)
{
	tick_t& last = lastSeen();
	// The tick AT the loop end never plays (the wrap happens on reaching it), so the tail stops one short.
	const int tail = last != Unwatched && last < loopEnd ? advance(loopEnd - 1, true) : 0;
	last = loopBegin - 1;  // a marker on the loop begin plays at the start of every pass
	return tail + advance(tick, true);
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
