/*
 * MidiPlayback.cpp - global swing, note chasing, the MIDI playback offset and the loop pass
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
#include "MidiPlayback.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QDomElement>

#include "ConfigManager.h"

namespace lmms
{

MidiPlayback& MidiPlayback::instance()
{
	static MidiPlayback* playback = [] {
		auto* made = new MidiPlayback();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *playback;
}

MidiPlayback::MidiPlayback()
{
	ConfigManager* config = ConfigManager::inst();
	m_chase.store(config->value(QStringLiteral("midi"), QStringLiteral("chase")).toInt() != 0);
	m_offsetMs.store(std::clamp(config->value(QStringLiteral("midi"), QStringLiteral("playbackoffset")).toInt(), 0, MaxOffsetMs));
}

void MidiPlayback::setSwing(float swing, int grid)
{
	// std::clamp leaves a NaN untouched - every comparison against it is false -
	// so a project file carrying swing="nan" (or a caller passing one) would store
	// a non-finite amount that then reaches delayFrames(), save() and
	// shouldPersist(). Load non-finite as the default, off, exactly like an
	// unreadable attribute.
	const float amount = std::isfinite(swing) ? swing : 0.f;
	m_swing.store(std::clamp(amount, 0.f, MaxSwing), std::memory_order_relaxed);
	m_swingGrid.store(grid == 2 * DefaultSwingGrid ? grid : DefaultSwingGrid, std::memory_order_relaxed);
}

void MidiPlayback::setPreferences(bool chase, int offsetMs)
{
	m_chase.store(chase, std::memory_order_relaxed);
	m_offsetMs.store(std::clamp(offsetMs, 0, MaxOffsetMs), std::memory_order_relaxed);
	ConfigManager::inst()->setValue(QStringLiteral("midi"), QStringLiteral("chase"), chase ? QStringLiteral("1") : QStringLiteral("0"));
	ConfigManager::inst()->setValue(QStringLiteral("midi"), QStringLiteral("playbackoffset"), QString::number(this->offsetMs()));  // as clamped
}

void MidiPlayback::tickPlayed(tick_t tick, std::uint64_t loopWraps)
{
	const bool restarted = m_restart.exchange(false, std::memory_order_relaxed);
	if (restarted)
	{
		m_lastTick = -2;  // no pass to count back from
		m_lastWraps = loopWraps;  // a wrap before the start (or the start's own loop correction) is no pass
		m_pass.store(0, std::memory_order_relaxed);
	}
	m_chasingNow = restarted || tick != m_lastTick + 1;  // the first tick is a start, whatever tick it is (-1 too)
	// Only a loop wrap is a pass: a seek back moves the position the same way but is not one (0.5.0 W2).
	const std::uint64_t wraps = loopWraps - m_lastWraps;  // modular: a counter that wrapped round still subtracts right
	if (wraps != 0) { m_pass.fetch_add(static_cast<int>(wraps), std::memory_order_relaxed); }
	m_lastWraps = loopWraps;
	m_lastTick = tick;
}

void MidiPlayback::stopped()
{
	m_restart.store(true, std::memory_order_relaxed);
}

f_cnt_t MidiPlayback::delayFrames(tick_t songTick, float framesPerTick, sample_rate_t sampleRate) const
{
	double frames = offsetMs() * static_cast<double>(sampleRate) / 1000.0;
	const float amount = swing();
	const int grid = swingGrid();
	// A tick length that is not a positive finite number gives the swing nothing to delay by: a negative
	// one would make the delay negative, and a negative delay converts to a huge unsigned frame count.
	if (amount > 0.f && songTick % (2 * grid) == grid && std::isfinite(framesPerTick) && framesPerTick > 0.f)
	{
		frames += amount * grid * framesPerTick;
	}
	// The delay is a count of frames: never below zero (NaN included), and clamped to the largest count
	// the type holds before the conversion, which is only defined inside the type's range.
	if (!(frames > 0.0)) { return 0; }
	if (frames >= static_cast<double>(std::numeric_limits<f_cnt_t>::max())) { return std::numeric_limits<f_cnt_t>::max(); }
	return static_cast<f_cnt_t>(std::round(frames));
}

QString MidiPlayback::elementName() const
{
	return QStringLiteral("midi-playback");
}

void MidiPlayback::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	element.setAttribute(QStringLiteral("swing"), QString::number(swing(), 'g', 6));
	if (swingGrid() != DefaultSwingGrid) { element.setAttribute(QStringLiteral("grid"), swingGrid()); }
	parent.appendChild(element);
}

void MidiPlayback::load(const QDomElement& element)
{
	setSwing(element.attribute(QStringLiteral("swing"), QStringLiteral("0")).toFloat(),
		element.attribute(QStringLiteral("grid"), QString::number(DefaultSwingGrid)).toInt());
}

} // namespace lmms
