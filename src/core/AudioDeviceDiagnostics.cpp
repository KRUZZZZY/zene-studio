/*
 * AudioDeviceDiagnostics.cpp - the audio device's negotiated timing and xruns, as mixer.cpu_load reports them
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
 *
 */

#include "AudioDeviceDiagnostics.h"

#include <optional>

#include "AudioDevice.h"
#include "AudioEngine.h"

namespace lmms
{

namespace
{

QJsonObject reading(const std::optional<double>& value, const QString& unit)
{
	QJsonObject out{{QStringLiteral("available"), value.has_value()}, {QStringLiteral("unit"), unit}};
	if (value.has_value()) { out.insert(QStringLiteral("value"), *value); }
	return out;
}

template<typename T>
std::optional<double> asDouble(const std::optional<T>& value)
{
	return value.has_value() ? std::optional<double>(static_cast<double>(*value)) : std::nullopt;
}

//! A latency reading: frames, and milliseconds at @a rate, with what the figure is measured from.
QJsonObject latency(const std::optional<f_cnt_t>& frames, const QString& basis, double rate)
{
	QJsonObject out = reading(asDouble(frames), QStringLiteral("frames"));
	if (frames.has_value())
	{
		out.insert(QStringLiteral("ms"), rate > 0 ? 1000.0 * static_cast<double>(*frames) / rate : 0.0);
		out.insert(QStringLiteral("basis"), basis);
	}
	return out;
}

QJsonObject engineTiming(const AudioEngine* engine)
{
	return QJsonObject{{QStringLiteral("sample_rate_hz"), static_cast<double>(engine->baseSampleRate())},
		{QStringLiteral("output_sample_rate_hz"), static_cast<double>(engine->outputSampleRate())},
		{QStringLiteral("period_frames"), static_cast<double>(engine->framesPerPeriod())},
		{QStringLiteral("buffer_frames"), static_cast<double>(engine->framesPerAudioBuffer())}};
}

//! A reported rate in Hz, or the engine's output rate when the device does not report one.
double rateOf(const std::optional<sample_rate_t>& reported, const AudioEngine* engine)
{
	return static_cast<double>(reported.has_value() ? *reported : engine->outputSampleRate());
}

bool reportsTiming(const AudioDevice::DeviceTiming& timing)
{
	return timing.sampleRate.has_value() || timing.inputSampleRate.has_value() || timing.periodFrames.has_value() || timing.bufferFrames.has_value()
		|| timing.inputLatencyFrames.has_value() || timing.outputLatencyFrames.has_value();
}

//! The device's own xrun count, or nothing when its backend does not count them.
std::optional<double> xrunCount(const AudioDevice* device, const AudioDevice::DeviceTiming& timing)
{
	if (device == nullptr || !timing.countsXruns) { return std::nullopt; }
	return static_cast<double>(device->backendXruns());
}

} // namespace

QJsonObject audioDeviceReport(AudioEngine* engine)
{
	AudioDevice* device = engine->audioDev();
	const AudioDevice::DeviceTiming timing = device != nullptr ? device->deviceTiming() : AudioDevice::DeviceTiming{};
	const double rate = rateOf(timing.sampleRate, engine);
	const double inputRate = rateOf(timing.inputSampleRate, engine);
	return QJsonObject{
		{QStringLiteral("backend"), timing.backend.isEmpty() ? engine->audioDevName() : timing.backend},
		{QStringLiteral("running"), device != nullptr && device->isRunning()},
		{QStringLiteral("reports_timing"), reportsTiming(timing)},
		{QStringLiteral("engine"), engineTiming(engine)},
		{QStringLiteral("sample_rate"), reading(asDouble(timing.sampleRate), QStringLiteral("Hz"))},
		{QStringLiteral("input_sample_rate"), reading(asDouble(timing.inputSampleRate), QStringLiteral("Hz"))},
		{QStringLiteral("period_frames"), reading(asDouble(timing.periodFrames), QStringLiteral("frames"))},
		{QStringLiteral("buffer_frames"), reading(asDouble(timing.bufferFrames), QStringLiteral("frames"))},
		{QStringLiteral("input_latency"), latency(timing.inputLatencyFrames, timing.inputLatencyBasis, inputRate)},
		{QStringLiteral("output_latency"), latency(timing.outputLatencyFrames, timing.outputLatencyBasis, rate)},
		{QStringLiteral("xruns"), reading(xrunCount(device, timing), QStringLiteral("count"))}};
}

} // namespace lmms
