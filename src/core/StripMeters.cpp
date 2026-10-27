/*
 * StripMeters.cpp - what one mixer strip's meter reads: its own mode, a VU meter and a loudness meter
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

#include "StripMeters.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

StripLoudness::StripLoudness(sample_rate_t sampleRate) :
	m_meter(sampleRate, DEFAULT_CHANNELS)
{
}

void StripLoudness::resetIfAsked() noexcept
{
	if (!m_resetPending.exchange(false, std::memory_order_acquire)) { return; }
	m_meter.reset();  // in place: the meter's fixed arrays zeroed, nothing allocated
}

void StripLoudness::feed(const SampleFrame* buffer, float gain, f_cnt_t frames) noexcept
{
	resetIfAsked();
	for (f_cnt_t done = 0; done < frames; done += ScratchFrames)
	{
		const f_cnt_t count = std::min<f_cnt_t>(ScratchFrames, frames - done);
		for (f_cnt_t f = 0; f < count; ++f) { m_scratch[static_cast<std::size_t>(f)] = buffer[done + f] * gain; }
		m_meter.processBlock(m_scratch.data(), count);
	}
	publish();
}

void StripLoudness::feedSilence(f_cnt_t frames) noexcept
{
	resetIfAsked();
	std::fill(m_scratch.begin(), m_scratch.end(), SampleFrame());
	for (f_cnt_t done = 0; done < frames; done += ScratchFrames)
	{
		m_meter.processBlock(m_scratch.data(), std::min<f_cnt_t>(ScratchFrames, frames - done));
	}
	publish();
}

void StripLoudness::publish() noexcept
{
	m_momentary.store(m_meter.momentaryLufs(), std::memory_order_relaxed);
	m_shortTerm.store(m_meter.shortTermLufs(), std::memory_order_relaxed);
	m_truePeak.store(m_meter.truePeakDbtp(), std::memory_order_relaxed);
}

namespace stripmeters
{

namespace
{

//! Two equal one-pole stages are critically damped; their step response reaches 99 % at t = 6.6384 tau.
constexpr double kVuTauSeconds = VuIntegrationMs / 1000.0 / 6.6384;
//! A rectified sine averages 2/pi of its peak: the needle is scaled so a steady sine reads its peak.
constexpr float kSinePeakPerMean = 1.5707963f;

//! The VU needle over @a frames samples of @a sample(f, side): two one-pole stages on the rectified signal.
template<typename Sample>
void runVu(MixerChannel& channel, f_cnt_t frames, Sample sample)
{
	if (!channel.m_vuRunning)
	{
		channel.m_vuState.fill(0.f);  // entering the mode: the needle starts at rest
		channel.m_vuRunning = true;
	}
	const double rate = Engine::audioEngine()->outputSampleRate();
	const auto a = static_cast<float>(1.0 - std::exp(-1.0 / (kVuTauSeconds * rate)));
	for (int side = 0; side < 2; ++side)
	{
		float first = channel.m_vuState[static_cast<std::size_t>(side * 2)];
		float second = channel.m_vuState[static_cast<std::size_t>(side * 2 + 1)];
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			first += a * (std::abs(sample(f, side)) - first);
			second += a * (first - second);
		}
		channel.m_vuState[static_cast<std::size_t>(side * 2)] = first;
		channel.m_vuState[static_cast<std::size_t>(side * 2 + 1)] = second;
	}
	channel.m_vuLeft.set(channel.m_vuState[1] * kSinePeakPerMean);
	channel.m_vuRight.set(channel.m_vuState[3] * kSinePeakPerMean);
}

} // namespace

const QStringList& modeNames()
{
	static const QStringList names{QStringLiteral("default"), QStringLiteral("peak"), QStringLiteral("rms"),
		QStringLiteral("pre_fader"), QStringLiteral("vu"), QStringLiteral("lufs")};
	return names;
}

int mode(const MixerChannel& channel)
{
	return channel.m_meterMode.load(std::memory_order_relaxed);
}

void setMode(MixerChannel& channel, int next)
{
	next = std::clamp(next, 0, static_cast<int>(Mode::Lufs));
	if (next == mode(channel)) { return; }
	if (next == static_cast<int>(Mode::Lufs))
	{
		if (channel.m_loudnessOwner == nullptr)
		{
			// Built here, on the control thread, once; the worker only ever sees a finished object.
			channel.m_loudnessOwner = std::make_unique<StripLoudness>(Engine::audioEngine()->outputSampleRate());
			channel.m_loudness.store(channel.m_loudnessOwner.get(), std::memory_order_release);
		}
		channel.m_loudnessOwner->requestReset();
	}
	channel.m_meterMode.store(next, std::memory_order_relaxed);
}

void measure(MixerChannel& channel, const SampleFrame* buffer, float gain, f_cnt_t frames)
{
	const auto current = static_cast<Mode>(mode(channel));
	if (current == Mode::Vu)
	{
		runVu(channel, frames, [buffer, gain](f_cnt_t f, int side) { return buffer[f][side] * gain; });
		return;
	}
	channel.m_vuRunning = false;
	if (current != Mode::Lufs) { return; }
	if (StripLoudness* meter = channel.m_loudness.load(std::memory_order_acquire)) { meter->feed(buffer, gain, frames); }
}

void measureSilence(MixerChannel& channel, f_cnt_t frames)
{
	const auto current = static_cast<Mode>(mode(channel));
	if (current == Mode::Vu)
	{
		runVu(channel, frames, [](f_cnt_t, int) { return 0.f; });
		return;
	}
	channel.m_vuRunning = false;
	if (current != Mode::Lufs) { return; }
	if (StripLoudness* meter = channel.m_loudness.load(std::memory_order_acquire)) { meter->feedSilence(frames); }
}

float vu(const MixerChannel& channel, int side, double referenceDbfs)
{
	const float level = side == 0 ? channel.m_vuLeft.get() : channel.m_vuRight.get();
	if (level <= 1e-9f) { return -std::numeric_limits<float>::infinity(); }
	return static_cast<float>(20.0 * std::log10(static_cast<double>(level)) - referenceDbfs);
}

const StripLoudness* loudness(const MixerChannel& channel)
{
	return channel.m_loudness.load(std::memory_order_acquire);
}

} // namespace stripmeters

} // namespace lmms
