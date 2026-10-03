/*
 * ControlRoom.cpp - the monitor section: speakers, dim, mono and cut on what you hear, never on a render
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
#include "ControlRoom.h"

#include <atomic>
#include <cmath>
#include <algorithm>

#include <QStringList>

#include "ConfigManager.h"
#include "SampleFrame.h"

namespace lmms::controlroom
{

namespace
{

std::atomic<float> g_gain{1.f};  // the speaker's trim, dim and cut as one linear gain (audio thread reads)
std::atomic<bool> g_mono{false};
std::atomic<bool> g_exporting{false};
State g_state;  // the interface thread's copy; the audio thread never reads it

float linear(float db) { return std::pow(10.f, db / 20.f); }

//! A trim as the config file holds it, kept to the range monitor.control_room takes (-24..12 dB): an edited or
//! unreadable value cannot reach the live output as an infinite or absurd gain.
float storedTrim(const QString& text)
{
	bool ok = false;
	const float db = text.toFloat(&ok);
	return ok && std::isfinite(db) ? std::clamp(db, -24.f, 12.f) : 0.f;
}

void publish(const State& state)
{
	const float gain = state.cut ? 0.f : linear(state.trimsDb[state.speaker] + (state.dim ? state.dimDb : 0.f));
	g_gain.store(gain, std::memory_order_relaxed);
	g_mono.store(state.mono, std::memory_order_relaxed);
}

} // namespace

State state() { return g_state; }

void setState(const State& state)
{
	g_state = state;
	publish(g_state);
	QStringList trims;
	for (const float db : g_state.trimsDb) { trims << QString::number(db); }
	ConfigManager::inst()->setValue(QStringLiteral("controlroom"), QStringLiteral("trims"), trims.join(QLatin1Char(';')));
	ConfigManager::inst()->setValue(QStringLiteral("controlroom"), QStringLiteral("speaker"), QString::number(g_state.speaker));
}

void loadPreferences()
{
	const QStringList trims = ConfigManager::inst()->value(QStringLiteral("controlroom"), QStringLiteral("trims")).split(QLatin1Char(';'));
	for (int i = 0; i < 3 && i < trims.size(); ++i) { g_state.trimsDb[i] = storedTrim(trims[i]); }
	g_state.speaker = std::clamp(ConfigManager::inst()->value(QStringLiteral("controlroom"), QStringLiteral("speaker")).toInt(), 0, 2);
	publish(g_state);
}

void setExporting(bool exporting) { g_exporting.store(exporting, std::memory_order_relaxed); }

void apply(SampleFrame* buffer, f_cnt_t frames)
{
	const float gain = g_gain.load(std::memory_order_relaxed);
	const bool mono = g_mono.load(std::memory_order_relaxed);
	if ((gain == 1.f && !mono) || g_exporting.load(std::memory_order_relaxed)) { return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		if (mono) { buffer[f][0] = buffer[f][1] = 0.5f * (buffer[f][0] + buffer[f][1]); }
		buffer[f][0] *= gain;
		buffer[f][1] *= gain;
	}
}

} // namespace lmms::controlroom
