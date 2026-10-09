/*
 * MetronomeOptions.cpp - the click's level, accent, sound and when it sounds
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
#include "MetronomeOptions.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

#include "ConfigManager.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SamplePlayHandle.h"

namespace lmms::metronomeoptions
{

namespace
{

//! The built-in sounds: the plain and the accented sample of each (factory samples, as before).
struct Sound
{
	const char* name;
	const char* plain;
	const char* accented;
};

constexpr Sound kSounds[] = {{"classic", "misc/metronome01.ogg", "misc/metronome02.ogg"},
	{"rim", "drums/rim01.ogg", "drums/snare_rim01.ogg"}, {"clave", "drums/clav01.ogg", "drums/clav02.ogg"},
	{"stick", "drums/sidestick01.ogg", "drums/nasty_rim01.ogg"}};
constexpr int kSoundCount = static_cast<int>(sizeof(kSounds) / sizeof(kSounds[0]));

struct State
{
	std::atomic<float> level{1.f};
	std::atomic<bool> accent{true};
	std::atomic<bool> recordOnly{false};
	std::atomic<int> sound{0};
	std::atomic<bool> loaded{false};
	//! Every sound's plain and accented sample, decoded once on the main thread so a click never reads a file
	//! on the audio thread; set before 'loaded', never changed after.
	std::array<std::shared_ptr<const SampleBuffer>, 2 * kSoundCount> buffers;
};

State& state()
{
	static State value;
	return value;
}

QString read(const char* key, const char* fallback)
{
	return ConfigManager::inst()->value(QStringLiteral("metronome"), QLatin1String(key), QLatin1String(fallback));
}

//! The level as stored: 0..2. std::clamp passes NaN through (a hand-edited "nan" in the config, which
//! toFloat() accepts), and it would ride on every click's amplification, so NaN is the default 1.
float boundedLevel(float level)
{
	return std::isnan(level) ? 1.f : std::clamp(level, 0.f, 2.f);
}

//! The stored options, read once (the first call is on the main thread: options() runs at registration).
void load()
{
	State& s = state();
	if (s.loaded.load(std::memory_order_acquire)) { return; }
	s.level.store(boundedLevel(read("level", "1").toFloat()));
	s.accent.store(read("accent", "1").toInt() != 0);
	s.recordOnly.store(read("recordonly", "0").toInt() != 0);
	s.sound.store(std::clamp(static_cast<int>(soundNames().indexOf(read("sound", "classic"))), 0, kSoundCount - 1));
	for (int i = 0; i < kSoundCount; ++i)
	{
		s.buffers[2 * i] = SampleBuffer::fromFile(QLatin1String(kSounds[i].plain));
		s.buffers[2 * i + 1] = SampleBuffer::fromFile(QLatin1String(kSounds[i].accented));
	}
	s.loaded.store(true, std::memory_order_release);
}

} // namespace

const QStringList& soundNames()
{
	static const QStringList names = [] {
		QStringList out;
		for (const Sound& sound : kSounds) { out << QLatin1String(sound.name); }
		return out;
	}();
	return names;
}

Options options()
{
	load();
	const State& s = state();
	return {s.level.load(), s.accent.load(), s.recordOnly.load(), s.sound.load()};
}

void setOptions(const Options& next)
{
	load();
	State& s = state();
	s.level.store(boundedLevel(next.level));
	s.accent.store(next.accent);
	s.recordOnly.store(next.recordOnly);
	s.sound.store(std::clamp(next.sound, 0, kSoundCount - 1));
	ConfigManager* config = ConfigManager::inst();
	config->setValue(QStringLiteral("metronome"), QStringLiteral("level"), QString::number(s.level.load()));
	config->setValue(QStringLiteral("metronome"), QStringLiteral("accent"), next.accent ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(QStringLiteral("metronome"), QStringLiteral("recordonly"), next.recordOnly ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(QStringLiteral("metronome"), QStringLiteral("sound"), soundNames().value(s.sound.load()));
}

QJsonObject toJson(const Options& options)
{
	return {{QStringLiteral("level"), static_cast<double>(options.level)}, {QStringLiteral("accent"), options.accent},
		{QStringLiteral("record_only"), options.recordOnly}, {QStringLiteral("sound"), soundNames().value(options.sound)}};
}

bool sounds(bool forced, bool recording)
{
	return forced || !state().recordOnly.load(std::memory_order_relaxed) || recording;
}

PlayHandle* click(bool downbeat, std::size_t offset)
{
	// Options registration decodes every built-in source on the control thread. If it has not run,
	// refuse this click instead of reaching the file loader from the render path.
	const State& s = state();
	AudioEngine* const audio = Engine::audioEngine();
	if (audio == nullptr) { return nullptr; }
	if (!s.loaded.load(std::memory_order_acquire))
	{
		audio->recordSampleVoiceOverflow();
		return nullptr;
	}
	const int index = std::clamp(s.sound.load(std::memory_order_relaxed), 0, kSoundCount - 1);
	const bool accented = downbeat && s.accent.load(std::memory_order_relaxed);
	const auto& buffer = s.buffers[2 * index + (accented ? 1 : 0)];
	if (buffer == nullptr || buffer->empty())
	{
		audio->recordSampleVoiceOverflow();
		return nullptr;
	}
	return audio->acquirePooledSampleVoice(buffer, 0, static_cast<int>(buffer->size()),
		s.level.load(std::memory_order_relaxed), static_cast<f_cnt_t>(offset), nullptr, true);
}

} // namespace lmms::metronomeoptions
