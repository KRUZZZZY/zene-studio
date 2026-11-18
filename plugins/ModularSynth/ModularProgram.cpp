/*
 * ModularProgram.cpp - Q050-059: compiling a modular graph, and the DSP of its starter modules
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "ModularProgram.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>

#include "AutomatableModel.h"
#include "SampleFrame.h"

namespace lmms::modular
{

namespace
{

std::atomic<std::uint64_t> s_nextSerial{1};

// Per-module voice state slots.
constexpr std::size_t Phase = 0;			// oscillator, LFO: 0..1
constexpr std::size_t EnvLevel = 0;
constexpr std::size_t EnvStage = 1;
constexpr std::size_t EnvReleaseFrom = 2;
constexpr std::size_t FilterIc1 = 0;
constexpr std::size_t FilterIc2 = 1;

enum EnvStageValue : int { Attack, Decay, Sustain, Release };

//! The polynomial band-limited step correction (PolyBLEP) for a discontinuity at phase 0.
float polyBlep(float t, float dt) noexcept
{
	if (t < dt)
	{
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt)
	{
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

float wrap(float phase) noexcept
{
	return phase - std::floor(phase);
}

//! Oscillator shapes: 0 sine, 1 saw, 2 square, 3 triangle; saw and square band-limited.
float oscillatorSample(int wave, float t, float dt) noexcept
{
	switch (wave)
	{
		case 0: return std::sin(2.f * std::numbers::pi_v<float> * t);
		case 1: return 2.f * t - 1.f - polyBlep(t, dt);
		case 2: return (t < 0.5f ? 1.f : -1.f) + polyBlep(t, dt) - polyBlep(wrap(t + 0.5f), dt);
		default: return 1.f - 4.f * std::abs(t - 0.5f);
	}
}

//! LFO shapes: 0 sine, 1 triangle, 2 saw, 3 square.
float lfoSample(int wave, float t) noexcept
{
	switch (wave)
	{
		case 0: return std::sin(2.f * std::numbers::pi_v<float> * t);
		case 1: return 1.f - 4.f * std::abs(t - 0.5f);
		case 2: return 2.f * t - 1.f;
		default: return t < 0.5f ? 1.f : -1.f;
	}
}

int whole(float value) noexcept
{
	return static_cast<int>(std::lround(value));
}

using Params = std::array<float, MaxParams>;
using State = std::array<float, 4>;
using Values = std::array<float, MaxModules>;

//! What every module of a block shares: the parameters (read once per block, so automation is followed at block
//! rate), the note's frequency and the sample rate.
struct Block
{
	std::array<Params, MaxModules> params;
	float frequency;
	float sampleRate;
	float framesPerMs;
};

std::array<Params, MaxModules> readParameters(const Program& program) noexcept
{
	std::array<Params, MaxModules> p{};
	for (std::size_t i = 0; i < static_cast<std::size_t>(program.count); ++i)
	{
		const ProgramModule& m = program.modules[i];
		for (std::size_t k = 0; k < static_cast<std::size_t>(MaxParams); ++k)
		{
			if (m.params[k] != nullptr) { p[i][k] = m.params[k]->value(); }
		}
	}
	return p;
}

//! The sum of what is wired into @a port, or @a unwired when nothing is.
float input(const ProgramModule& m, std::size_t port, const Values& value, float unwired) noexcept
{
	if (m.sourceCount[port] == 0) { return unwired; }
	float sum = 0.f;
	for (std::int8_t k = 0; k < m.sourceCount[port]; ++k)
	{
		sum += value[static_cast<std::size_t>(m.sources[port][static_cast<std::size_t>(k)])];
	}
	return sum;
}

float stepOscillator(const Params& par, State& s, const Block& block, float pitch, float amp) noexcept
{
	const float semitones = par[1] + par[2] / 100.f;
	const float hz = block.frequency * std::exp2(semitones / 12.f + pitch);
	const float dt = std::clamp(hz / block.sampleRate, 0.f, 0.49f);
	const float sample = oscillatorSample(whole(par[0]), s[Phase], dt) * par[3] * amp;
	s[Phase] = wrap(s[Phase] + dt);
	return sample;
}

//! A linear segment from @a level towards @a target over @a frames; true once it is there.
bool ramp(float& level, float target, float frames, float from) noexcept
{
	if (frames < 1.f)
	{
		level = target;
		return true;
	}
	const float stepSize = std::abs(target - from) / frames;
	level = target > level ? std::min(target, level + stepSize) : std::max(target, level - stepSize);
	return level == target;
}

float stepEnvelope(const Params& par, State& s, const Block& block, bool released) noexcept
{
	float& level = s[EnvLevel];
	int stage = static_cast<int>(s[EnvStage]);
	if (released && stage != Release)
	{
		stage = Release;
		s[EnvStage] = static_cast<float>(Release);
		s[EnvReleaseFrom] = level;
	}
	const float sustain = par[2];
	switch (stage)
	{
		case Attack:
			if (ramp(level, 1.f, par[0] * block.framesPerMs, 0.f)) { s[EnvStage] = static_cast<float>(Decay); }
			break;
		case Decay:
			if (ramp(level, sustain, par[1] * block.framesPerMs, 1.f)) { s[EnvStage] = static_cast<float>(Sustain); }
			break;
		case Sustain:
			level = sustain;
			break;
		default:
			ramp(level, 0.f, par[3] * block.framesPerMs, s[EnvReleaseFrom]);
			break;
	}
	return level;
}

float stepFilter(const Params& par, State& s, const Block& block, float x, float modulation) noexcept
{
	const float cutoff = std::clamp(par[1] * std::exp2(par[3] * modulation), 20.f, 0.45f * block.sampleRate);
	const float g = std::tan(std::numbers::pi_v<float> * cutoff / block.sampleRate);
	const float k = std::max(0.1f, 2.f * (1.f - par[2]));
	// Zavalishin's topology-preserving state-variable filter.
	const float hp = (x - (g + k) * s[FilterIc1] - s[FilterIc2]) / (1.f + g * (g + k));
	const float bp = g * hp + s[FilterIc1];
	s[FilterIc1] = g * hp + bp;
	const float lp = g * bp + s[FilterIc2];
	s[FilterIc2] = g * bp + lp;
	const int mode = whole(par[0]);
	return mode == 0 ? lp : mode == 1 ? hp : bp;
}

float stepLfo(const Params& par, State& s, const Block& block) noexcept
{
	const float sample = lfoSample(whole(par[0]), s[Phase]) * par[2];
	s[Phase] = wrap(s[Phase] + par[1] / block.sampleRate);
	return sample;
}

//! One module's next value; every module wired into it has already produced its own in @a value.
float step(const ProgramModule& m, State& s, const Params& par, const Block& block, const Values& value,
	bool released) noexcept
{
	switch (m.kind)
	{
		case ModuleKind::Oscillator:
			return stepOscillator(par, s, block, input(m, 0, value, 0.f), input(m, 1, value, 1.f));
		case ModuleKind::Envelope: return stepEnvelope(par, s, block, released);
		case ModuleKind::Filter: return stepFilter(par, s, block, input(m, 0, value, 0.f), input(m, 1, value, 0.f));
		case ModuleKind::Lfo: return stepLfo(par, s, block);
		case ModuleKind::Output: return input(m, 0, value, 0.f) * input(m, 1, value, 1.f) * par[0];
	}
	return 0.f;
}

} // namespace


std::unique_ptr<Program> compile(const Graph& graph,
	const std::array<std::array<const FloatModel*, MaxParams>, MaxModules>& params)
{
	auto program = std::make_unique<Program>();
	const std::vector<int> order = graph.processingOrder();
	std::array<int, MaxModules> position{};	// graph index -> processing-order index
	for (std::size_t i = 0; i < order.size(); ++i) { position[static_cast<std::size_t>(order[i])] = static_cast<int>(i); }

	program->count = static_cast<int>(order.size());
	for (std::size_t i = 0; i < order.size(); ++i)
	{
		const auto index = static_cast<std::size_t>(order[i]);
		ProgramModule& m = program->modules[i];
		m.kind = graph.modules[index].kind;
		m.params = params[index];
		if (m.kind == ModuleKind::Output) { program->output = static_cast<int>(i); }
	}
	for (const Wire& wire : graph.wires)
	{
		ProgramModule& target = program->modules[static_cast<std::size_t>(position[static_cast<std::size_t>(graph.indexOf(wire.to))])];
		const auto port = static_cast<std::size_t>(portIndex(target.kind, wire.port));
		const int source = position[static_cast<std::size_t>(graph.indexOf(wire.from))];
		target.sources[port][static_cast<std::size_t>(target.sourceCount[port]++)] = static_cast<std::int8_t>(source);
		if (target.kind == ModuleKind::Output && wire.port == QLatin1String("amp")
			&& program->modules[static_cast<std::size_t>(source)].kind == ModuleKind::Envelope)
		{
			program->outputEnveloped = true;
		}
	}
	program->serial = s_nextSerial.fetch_add(1, std::memory_order_relaxed);
	return program;
}

float longestReleaseMs(const Program& program)
{
	float longest = 0.f;
	for (int i = 0; i < program.count; ++i)
	{
		const ProgramModule& m = program.modules[static_cast<std::size_t>(i)];
		if (m.kind == ModuleKind::Envelope && m.params[3] != nullptr) { longest = std::max(longest, m.params[3]->value()); }
	}
	return longest;
}

void render(const Program& program, Voice& voice, float frequency, float sampleRate,
	SampleFrame* out, f_cnt_t from, f_cnt_t to, f_cnt_t releaseAt) noexcept
{
	if (voice.serial != program.serial)
	{
		// A new note, or the graph was replaced under a sounding one: start every module afresh.
		voice.state = {};
		voice.serial = program.serial;
	}

	const Block block{readParameters(program), frequency, sampleRate, sampleRate / 1000.f};
	std::array<float, MaxModules> value{};
	for (f_cnt_t f = from; f < to; ++f)
	{
		const bool released = f >= releaseAt;
		for (std::size_t i = 0; i < static_cast<std::size_t>(program.count); ++i)
		{
			value[i] = step(program.modules[i], voice.state[i], block.params[i], block, value, released);
		}
		const float sample = program.output >= 0 ? value[static_cast<std::size_t>(program.output)] : 0.f;
		out[f][0] = sample;
		out[f][1] = sample;
	}
}

} // namespace lmms::modular
