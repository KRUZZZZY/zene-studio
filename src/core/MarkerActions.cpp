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
		if (song == nullptr) { return; }
		advance(song->getPlayPos().getTicks(), song->isPlaying());
	});
	timer->start();
}

} // namespace lmms::markeractions
