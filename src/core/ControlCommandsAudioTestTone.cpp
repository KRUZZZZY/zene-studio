/*
 * ControlCommandsAudioTestTone.cpp - audio.test_tone and audio.test_tone_stop: a short sine through the
 *                                    running output, to check that sound comes out
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
 */

/*! Q050-183.1: audio.test_tone / audio.test_tone_stop. The tone is built here, on the calling thread, as one
 *  finished buffer and handed to the engine as a play handle, the same way browser.preview hands its buffer
 *  over. It plays to master whether or not the song plays. Stopping and replacing are generation numbers the
 *  handle reads (atomics), so the audio thread never waits, allocates or frees for them. Session audio only:
 *  the song, the transport, the undo journal and the config are not touched. */

#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "AudioTestTone.h"
#include "ConfigManager.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Sample.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"

namespace lmms
{

namespace
{

constexpr double TonePi = 3.14159265358979323846;
constexpr double ToneFrequencyLowest = 20.0;
constexpr double ToneFrequencyHighest = 20000.0;
constexpr double ToneSecondsLowest = 0.1;
constexpr double ToneSecondsHighest = 10.0;
constexpr double ToneLevelLowest = -60.0;
constexpr double ToneLevelHighest = -6.0;
constexpr double ToneDefaultFrequency = 440.0;
constexpr double ToneDefaultSeconds = 1.0;
constexpr double ToneDefaultLevelDb = -18.0;
constexpr double ToneFadeSeconds = 0.010;

//! Bumped by every test tone and by a stop: a tone handle plays while its generation is current.
std::atomic<int> s_toneGeneration{0};
//! The generation of the last tone handle the engine destroyed; the tone is over once it equals the current one.
std::atomic<int> s_toneEndedGeneration{0};
//! Set by audio.test_tone_stop, cleared when a tone starts.
std::atomic<bool> s_toneStopped{false};

class TestTonePlayHandle : public SamplePlayHandle
{
public:
	TestTonePlayHandle(Sample* sample, int generation) :
		SamplePlayHandle(sample),
		m_generation(generation)
	{
	}

	~TestTonePlayHandle()
	{
		// The engine may destroy an older, superseded handle after a newer one ended; the marker must never move backwards.
		int seen = s_toneEndedGeneration.load(std::memory_order_relaxed);
		while (seen < m_generation && !s_toneEndedGeneration.compare_exchange_weak(seen, m_generation, std::memory_order_relaxed))
		{
		}
	}

	bool isFinished() const override
	{
		return s_toneGeneration.load(std::memory_order_relaxed) != m_generation || SamplePlayHandle::isFinished();
	}

private:
	const int m_generation;
};

//! The tone as stereo frames at @a rate: a sine at @a frequency and @a levelDb with a 10 ms linear fade in and out.
std::vector<SampleFrame> sineTone(double frequency, double seconds, double levelDb, int rate)
{
	const std::size_t count = static_cast<std::size_t>(std::lround(seconds * rate));
	std::size_t fade = static_cast<std::size_t>(ToneFadeSeconds * rate);
	if (fade * 2 > count) { fade = count / 2; }
	const double amplitude = std::pow(10.0, levelDb / 20.0);
	const double step = 2.0 * TonePi * frequency / rate;

	std::vector<SampleFrame> frames;
	frames.reserve(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		double envelope = 1.0;
		if (fade > 0 && i < fade) { envelope = static_cast<double>(i) / static_cast<double>(fade); }
		const std::size_t fromEnd = count - 1 - i;
		if (fade > 0 && fromEnd < fade) { envelope *= static_cast<double>(fromEnd) / static_cast<double>(fade); }
		const sample_t value = static_cast<sample_t>(amplitude * envelope * std::sin(step * static_cast<double>(i)));
		frames.push_back(SampleFrame(value, value));
	}
	return frames;
}

//! Reads the optional number @a key into @a out: @a fallback when absent, the value when it is a number in
//! [@a low, @a high], else an InvalidArgs refusal in @a error.
bool readToneNumber(const QJsonObject& args, const QString& key, double low, double high, double fallback,
	double* out, ControlResult* error)
{
	*out = fallback;
	if (!args.contains(key)) { return true; }
	const QJsonValue value = args.value(key);
	const double number = value.toDouble();
	if (!value.isDouble() || number < low || number > high)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("%1 must be a number from %2 to %3")
			.arg(key).arg(low).arg(high));
		return false;
	}
	*out = number;
	return true;
}

ControlResult testTone(const QJsonObject& args)
{
	double frequency = ToneDefaultFrequency;
	double seconds = ToneDefaultSeconds;
	double levelDb = ToneDefaultLevelDb;
	ControlResult error;
	if (!readToneNumber(args, QStringLiteral("frequency"), ToneFrequencyLowest, ToneFrequencyHighest,
			ToneDefaultFrequency, &frequency, &error)) { return error; }
	if (!readToneNumber(args, QStringLiteral("seconds"), ToneSecondsLowest, ToneSecondsHighest,
			ToneDefaultSeconds, &seconds, &error)) { return error; }
	if (!readToneNumber(args, QStringLiteral("level_db"), ToneLevelLowest, ToneLevelHighest,
			ToneDefaultLevelDb, &levelDb, &error)) { return error; }

	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("audio.test_tone needs the audio engine running"));
	}
	const int rate = static_cast<int>(engine->outputSampleRate());
	std::vector<SampleFrame> frames = sineTone(frequency, seconds, levelDb, rate);

	s_toneStopped.store(false, std::memory_order_relaxed);
	const int generation = s_toneGeneration.fetch_add(1) + 1;
	const bool playing = engine->addPlayHandle(new TestTonePlayHandle(new Sample(frames.data(), frames.size(), rate), generation));

	const QString output = engine->audioDevName();
	const QString configured = ConfigManager::inst()->value(QStringLiteral("audioengine"), QStringLiteral("audiodev"));
	const bool audible = !engine->audioDevStartFailed() && output != AudioDummy::name();
	return ControlResult::success({{QStringLiteral("playing"), playing}, {QStringLiteral("output"), output},
		{QStringLiteral("configured"), configured}, {QStringLiteral("audible"), audible},
		{QStringLiteral("frequency"), frequency}, {QStringLiteral("seconds"), seconds},
		{QStringLiteral("level_db"), levelDb}});
}

ControlResult testToneStop(const QJsonObject&)
{
	const bool wasPlaying = audioTestToneActive();
	s_toneStopped.store(true, std::memory_order_relaxed);
	s_toneGeneration.fetch_add(1);
	return ControlResult::success({{QStringLiteral("stopped"), true}, {QStringLiteral("was_playing"), wasPlaying}});
}

} // namespace

bool audioTestToneActive()
{
	const int generation = s_toneGeneration.load(std::memory_order_relaxed);
	return generation > 0 && s_toneEndedGeneration.load(std::memory_order_relaxed) != generation
		&& !s_toneStopped.load(std::memory_order_relaxed);
}

void registerAudioTestToneCommands(ControlRegistry& registry)
{
	control::defineCommandWithSchema(registry, QStringLiteral("audio.test_tone"),
		QStringLiteral("Play a short sine tone through the running audio output, to check that sound comes out. "
			"'frequency' Hz (20..20000, 440 when left out), 'seconds' (0.1..10, 1.0), 'level_db' (-60..-6, -18), with a "
			"10 ms fade in and out. A new tone replaces the last. Result: 'playing', 'output' (the running backend), "
			"'configured' (the backend the config selects) and 'audible' (false when the output is the dummy device or "
			"the configured one failed to start). Session audio only: it writes no project state."),
		control::objectSchema({{QStringLiteral("frequency"), control::numberProperty()},
			{QStringLiteral("seconds"), control::numberProperty()},
			{QStringLiteral("level_db"), control::numberProperty()}}),
		false, testTone);
	control::defineCommandWithSchema(registry, QStringLiteral("audio.test_tone_stop"),
		QStringLiteral("Stop the test tone started by audio.test_tone. 'was_playing' says whether one was sounding. "
			"Session audio only: it writes no project state."),
		control::objectSchema({}), false, testToneStop);
}

} // namespace lmms
