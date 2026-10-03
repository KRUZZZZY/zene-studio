/*
 * RecordOptions.cpp - count-in, pre-roll and the record mode
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
#include "RecordOptions.h"

#include <algorithm>
#include <atomic>
#include <limits>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Song.h"
#include "Timeline.h"

namespace lmms::recordoptions
{

namespace
{

const QString kSection = QStringLiteral("recording");

//! The count-in of the running pass: [from, until) in ticks; until < 0 when there is none.
std::atomic<tick_t> s_countInFrom{0};
std::atomic<tick_t> s_countInUntil{-1};
bool s_gateOpened = false;  //!< UI thread: this pass opened the punch gate itself

int configInt(const char* key, int fallback)
{
	return ConfigManager::inst()->value(kSection, QLatin1String(key), QString::number(fallback)).toInt();
}

} // namespace

const QStringList& modeNames()
{
	static const QStringList names{QStringLiteral("overdub"), QStringLiteral("replace")};
	return names;
}

Options options()
{
	Options out;
	out.countInBars = std::clamp(configInt("countin", 0), 0, 4);
	out.preRollBars = std::clamp(configInt("preroll", 0), 0, 8);
	out.mode = configInt("mode", 0) == 1 ? Mode::Replace : Mode::Overdub;
	return out;
}

void setOptions(const Options& next)
{
	ConfigManager::inst()->setValue(kSection, QStringLiteral("countin"), QString::number(std::clamp(next.countInBars, 0, 4)));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("preroll"), QString::number(std::clamp(next.preRollBars, 0, 8)));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("mode"), QString::number(static_cast<int>(next.mode)));
}

QJsonObject toJson(const Options& o)
{
	return {{QStringLiteral("count_in_bars"), o.countInBars}, {QStringLiteral("pre_roll_bars"), o.preRollBars},
		{QStringLiteral("mode"), modeNames().at(static_cast<int>(o.mode))}};
}

void beginPass(Song* song)
{
	const Options o = options();
	const int bars = o.countInBars + o.preRollBars;
	if (bars == 0) { return; }
	const tick_t start = song->getPlayPos(Song::PlayMode::Song).getTicks();
	const tick_t back = std::max<tick_t>(0, start - static_cast<tick_t>(bars) * song->ticksPerBar());
	// Captured from the play position on: the punch gate opens there, unless a region is armed.
	if (!song->getTimeline(Song::PlayMode::Song).punchArmed())
	{
		Engine::audioEngine()->setPunchWindow(true, start, std::numeric_limits<tick_t>::max() / 2);
		s_gateOpened = true;
	}
	s_countInFrom.store(back, std::memory_order_relaxed);
	s_countInUntil.store(o.countInBars > 0 ? std::min(start, back + static_cast<tick_t>(o.countInBars) * song->ticksPerBar()) : -1,
		std::memory_order_relaxed);
	song->setPlayPos(back, Song::PlayMode::Song);
}

void endPass(Song* song)
{
	s_countInUntil.store(-1, std::memory_order_relaxed);
	if (!s_gateOpened) { return; }
	s_gateOpened = false;
	const Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	Engine::audioEngine()->setPunchWindow(timeline.punchArmed(), timeline.punchBegin(), timeline.punchEnd());
}

bool countingIn(tick_t tick)
{
	const tick_t until = s_countInUntil.load(std::memory_order_relaxed);
	return until >= 0 && tick >= s_countInFrom.load(std::memory_order_relaxed) && tick < until;
}

} // namespace lmms::recordoptions
