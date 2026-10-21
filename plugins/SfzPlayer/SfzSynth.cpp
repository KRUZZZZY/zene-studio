/*
 * SfzSynth.cpp - one sfizz synthesizer, configured for the engine, and the MIDI events it plays
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "SfzSynth.h"

#include <algorithm>
#include <atomic>

#include "sfizz.hpp"

namespace lmms
{

namespace
{
std::uint64_t nextSynthId() noexcept
{
	static std::atomic<std::uint64_t> counter{1};
	return counter.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

SfzSynth::SfzSynth(const Settings& settings)
	: m_id{nextSynthId()}
	, m_synth{std::make_unique<sfz::Sfizz>()}
{
	m_planes = {m_left.data(), m_right.data()};
	m_synth->setSampleRate(static_cast<float>(settings.sampleRate));
	m_synth->setSamplesPerBlock(static_cast<int>(ChunkFrames));
	m_synth->setNumVoices(settings.polyphony);
	m_synth->setOversamplingFactor(settings.oversampling);
	m_synth->setSampleQuality(sfz::Sfizz::ProcessLive, settings.quality);
}

SfzSynth::~SfzSynth() = default;

bool SfzSynth::load(const std::string& path)
{
	return m_synth->loadSfzFile(path);
}

int SfzSynth::regionCount() const
{
	return m_synth->getNumRegions();
}

int SfzSynth::groupCount() const
{
	return m_synth->getNumGroups();
}

void SfzSynth::apply(const SfzEvent& event, int delay) noexcept
{
	const int number = event.number;
	const int value = event.value;
	switch (event.kind)
	{
		case SfzEvent::Kind::NoteOn: m_synth->noteOn(delay, number, value); break;
		case SfzEvent::Kind::NoteOff: m_synth->noteOff(delay, number, value); break;
		case SfzEvent::Kind::Controller: m_synth->cc(delay, number, value); break;
		case SfzEvent::Kind::PitchBend: m_synth->pitchWheel(delay, event.bend); break;
		case SfzEvent::Kind::ChannelPressure: m_synth->channelAftertouch(delay, value); break;
		case SfzEvent::Kind::KeyPressure: m_synth->polyAftertouch(delay, number, value); break;
		case SfzEvent::Kind::ProgramChange: m_synth->programChange(delay, number); break;
	}
}

void SfzSynth::render(SampleFrame* out, f_cnt_t frames) noexcept
{
	const std::size_t count = std::min<std::size_t>(frames, ChunkFrames);
	m_synth->renderBlock(m_planes.data(), count, 1);
	for (std::size_t i = 0; i < count; ++i)
	{
		out[i] = SampleFrame{m_left[i], m_right[i]};
	}
}

void SfzSynth::setVolume(float decibels) noexcept
{
	m_synth->setVolume(decibels);
}

void SfzSynth::setSampleQuality(int quality) noexcept
{
	m_synth->setSampleQuality(sfz::Sfizz::ProcessLive, quality);
}

void SfzSynth::setFreewheeling(bool on) noexcept
{
	if (on) { m_synth->enableFreeWheeling(); }
	else { m_synth->disableFreeWheeling(); }
}

} // namespace lmms
