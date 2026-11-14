/*
 * Earcons.cpp - short monitoring tones for transport and edit events
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include "Earcons.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace lmms::earcons
{

namespace
{

constexpr int kSoundCount = static_cast<int>(Sound::Count);
constexpr int kCategoryCount = static_cast<int>(Category::Count);

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
	"the pending mask must be lock-free on the audio thread");
static_assert(std::atomic<float>::is_always_lock_free,
	"the level must be lock-free on the audio thread");
static_assert(std::atomic<int>::is_always_lock_free,
	"the active bank index must be lock-free on the audio thread");

//! Longest sound is 120 ms; 48 kHz is the highest rate prepared here.
constexpr int kMaxRate = 48000;
constexpr int kMaxFrames = kMaxRate * 120 / 1000 + 2;

//! Peak of every tone. Level scales it; the default level is 0.5.
constexpr float kPeak = 0.5f;
//! 5 ms linear attack and release, at kMaxRate.
constexpr int kRampFrames = kMaxRate * 5 / 1000;

struct Bank
{
	std::array<std::array<float, kMaxFrames>, kSoundCount> samples{};
	std::array<int, kSoundCount> lengths{};
};

//! Two banks; prepare() fills the inactive one and then publishes it.
std::array<Bank, 2> s_banks;
std::atomic<int> s_activeBank{0};

//! pending bit per Sound, published by play() and consumed by beginBuffer().
std::atomic<std::uint32_t> s_pending{0};
std::array<std::atomic<bool>, kCategoryCount> s_enabled{};
std::array<std::atomic<float>, kCategoryCount> s_level{};

//! Every category starts disabled at level 0.5.
struct Defaults
{
	Defaults()
	{
		for (auto& on : s_enabled) { on.store(false, std::memory_order_relaxed); }
		for (auto& value : s_level) { value.store(0.5f, std::memory_order_relaxed); }
	}
};
Defaults s_defaults;

//! Audio-thread voice state. Single consumer: only beginBuffer()/nextSample() touch it.
struct Voice
{
	const float* samples = nullptr;
	int length = 0;
	int position = 0;
	Category category = Category::Selection;
};

std::array<Voice, kSoundCount> s_voices;
int s_activeVoices = 0;
sample_rate_t s_preparedRate = 0;

void renderTone(Bank& bank, Sound sound, const float* frequencies, int frequencyCount,
	float seconds)
{
	const auto index = static_cast<std::size_t>(sound);
	const auto length = static_cast<int>(static_cast<float>(s_preparedRate) * seconds);
	bank.lengths[index] = length;
	auto& out = bank.samples[index];
	if (length <= 0) { return; }

	const int segment = length / frequencyCount;
	const int ramp = std::min(kRampFrames, std::max(1, segment / 4));
	constexpr float twoPi = 6.283185307179586f;
	for (int frame = 0; frame < length; ++frame)
	{
		const int which = std::min(frequencyCount - 1, frame / segment);
		float gain = 1.f;
		if (frame < ramp) { gain = static_cast<float>(frame) / static_cast<float>(ramp); }
		const int remaining = length - 1 - frame;
		if (remaining < ramp) { gain = std::min(gain, static_cast<float>(remaining) / static_cast<float>(ramp)); }
		out[static_cast<std::size_t>(frame)] =
			kPeak * gain * std::sin(twoPi * frequencies[which] * static_cast<float>(frame) /
				static_cast<float>(s_preparedRate));
	}
}

} // namespace

Category categoryOf(Sound sound)
{
	switch (sound)
	{
	case Sound::SelectionBoundary: return Category::Selection;
	case Sound::EditApplied: return Category::Edit;
	case Sound::Refused: return Category::Refused;
	case Sound::RecordStart:
	case Sound::RecordStop: return Category::Record;
	case Sound::LoopWrap: return Category::Loop;
	case Sound::Count: break;
	}
	return Category::Selection;
}

const QStringList& categoryNames()
{
	static const QStringList names{
		QStringLiteral("selection"), QStringLiteral("edit"), QStringLiteral("refused"),
		QStringLiteral("record"), QStringLiteral("loop")};
	return names;
}

void prepare(sample_rate_t rate)
{
	if (rate == 0 || rate > static_cast<sample_rate_t>(kMaxRate)) { return; }
	if (rate == s_preparedRate) { return; }

	const int active = s_activeBank.load(std::memory_order_relaxed);
	Bank& bank = s_banks[active == 0 ? 1 : 0];
	for (auto& tone : bank.samples) { tone.fill(0.f); }
	bank.lengths.fill(0);

	s_preparedRate = rate;

	// A short, distinct tone per sound: one sine, or two notes in sequence.
	constexpr float boundary[] = {1760.f};
	constexpr float edit[] = {1320.f};
	constexpr float refused[] = {330.f, 220.f};
	constexpr float recordStart[] = {660.f, 990.f};
	constexpr float recordStop[] = {990.f, 660.f};
	constexpr float loop[] = {880.f};

	renderTone(bank, Sound::SelectionBoundary, boundary, 1, 0.040f);
	renderTone(bank, Sound::EditApplied, edit, 1, 0.050f);
	renderTone(bank, Sound::Refused, refused, 2, 0.120f);
	renderTone(bank, Sound::RecordStart, recordStart, 2, 0.100f);
	renderTone(bank, Sound::RecordStop, recordStop, 2, 0.100f);
	renderTone(bank, Sound::LoopWrap, loop, 1, 0.030f);

	s_activeBank.store(active == 0 ? 1 : 0, std::memory_order_release);
}

void play(Sound sound) noexcept
{
	if (sound < Sound::SelectionBoundary || sound >= Sound::Count) { return; }
	if (!enabled(categoryOf(sound))) { return; }
	s_pending.fetch_or(1u << static_cast<unsigned>(sound), std::memory_order_release);
}

void setEnabled(Category category, bool on)
{
	const auto index = static_cast<std::size_t>(category);
	if (index >= s_enabled.size()) { return; }
	s_enabled[index].store(on, std::memory_order_relaxed);
}

bool enabled(Category category)
{
	const auto index = static_cast<std::size_t>(category);
	if (index >= s_enabled.size()) { return false; }
	return s_enabled[index].load(std::memory_order_relaxed);
}

bool setLevel(Category category, float value)
{
	if (!std::isfinite(value) || value < 0.f || value > 1.f) { return false; }
	const auto index = static_cast<std::size_t>(category);
	if (index >= s_level.size()) { return false; }
	s_level[index].store(value, std::memory_order_relaxed);
	return true;
}

float level(Category category)
{
	const auto index = static_cast<std::size_t>(category);
	if (index >= s_level.size()) { return 0.f; }
	return s_level[index].load(std::memory_order_relaxed);
}

void beginBuffer() noexcept
{
	const std::uint32_t pending = s_pending.exchange(0, std::memory_order_acquire);
	if (pending == 0) { return; }

	const Bank& bank = s_banks[s_activeBank.load(std::memory_order_acquire)];
	s_activeVoices = 0;
	for (int sound = 0; sound < kSoundCount; ++sound)
	{
		if ((pending & (1u << sound)) == 0) { continue; }
		const int length = bank.lengths[static_cast<std::size_t>(sound)];
		if (length <= 0) { continue; }
		s_voices[s_activeVoices] = Voice{bank.samples[static_cast<std::size_t>(sound)].data(), length, 0,
			categoryOf(static_cast<Sound>(sound))};
		++s_activeVoices;
	}
}

float nextSample() noexcept
{
	if (s_activeVoices == 0) { return 0.f; }
	float sum = 0.f;
	for (int i = 0; i < s_activeVoices;)
	{
		Voice& voice = s_voices[static_cast<std::size_t>(i)];
		sum += voice.samples[voice.position] * level(voice.category);
		++voice.position;
		if (voice.position >= voice.length)
		{
			s_voices[static_cast<std::size_t>(i)] = s_voices[static_cast<std::size_t>(s_activeVoices - 1)];
			--s_activeVoices;
			continue;
		}
		++i;
	}
	return sum;
}

} // namespace lmms::earcons
