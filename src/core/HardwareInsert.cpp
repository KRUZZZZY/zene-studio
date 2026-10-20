/*
 * HardwareInsert.cpp - an outboard device in a channel: send to an output pair, return from an input pair
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

#include "HardwareInsert.h"

#include <algorithm>
#include <cmath>

#include <QDomDocument>
#include <QDomElement>

#include "AudioEngine.h"

namespace lmms
{

namespace
{

constexpr auto Relaxed = std::memory_order_relaxed;

//! Channel @a channel of a frame of @a width interleaved channels, 0 when the block does not carry it.
float wideSample(const float* frame, int width, int channel)
{
	return channel < width ? frame[channel] : 0.f;
}

audioio::Destination destinationAttribute(const QDomElement& element, const char* name, audioio::Destination fallback)
{
	audioio::Destination out;
	// As in HardwareIo.cpp: a project's insert sends and returns are channel numbers, read over the
	// whole range so a project saved on another machine's interface still loads.
	audioio::Side routes;
	routes.channels = audioio::MaxChannels;
	if (!element.hasAttribute(name) || !audioio::parseDestination(element.attribute(name), routes, &out, nullptr))
	{
		return fallback;
	}
	return out;
}

} // namespace

void HardwareInsert::prepare(f_cnt_t frames)
{
	const auto size = static_cast<std::size_t>(std::max<f_cnt_t>(frames, 1));
	if (m_send.size() >= size) { return; }
	m_send.assign(size, SampleFrame{});
	m_return.assign(size, SampleFrame{});
	m_ring.assign(static_cast<std::size_t>(AudioEngine::InputStageCapacityFrames) + size, SampleFrame{});
	m_ringRead = 0;
	m_ringFill = 0;
	m_dry.init(static_cast<f_cnt_t>(size));
}

HardwareInsert::Settings HardwareInsert::settings() const
{
	Settings out = m_settings;
	out.measured = m_measured.load(Relaxed);
	return out;
}

void HardwareInsert::setSettings(const Settings& settings, f_cnt_t frames)
{
	if (settings.enabled) { prepare(frames); }
	// Switched on: the return starts from an empty ring, so what it held when it went off cannot shift
	// the round trip the ping measured (the render is held off: the caller has the change lock).
	if (settings.enabled && (!m_settings.enabled || settings.ret != m_settings.ret))
	{
		m_ringRead = 0;
		m_ringFill = 0;
	}
	m_settings = settings;
	if (!settings.enabled) { m_ping.store(static_cast<int>(PingState::Idle), std::memory_order_release); }
	m_measured.store(settings.measured, Relaxed);
}

int HardwareInsert::latencyFrames() const
{
	if (!m_settings.enabled) { return 0; }
	return m_settings.latencyOverride >= 0 ? m_settings.latencyOverride : std::max(0, m_measured.load(Relaxed));
}

void HardwareInsert::requestPing()
{
	m_ping.store(static_cast<int>(PingState::Requested), std::memory_order_release);
}

void HardwareInsert::process(SampleFrame* buffer, f_cnt_t frames, AudioEngine& engine) noexcept
{
	if (!m_settings.enabled || m_send.size() < static_cast<std::size_t>(frames)) { return; }
	const auto state = static_cast<PingState>(m_ping.load(std::memory_order_acquire));
	if (state == PingState::Requested)
	{
		// The click, alone on the send: the music stays off it until the click is heard back.
		std::fill_n(m_send.begin(), frames, SampleFrame{});
		m_send[0] = SampleFrame(PingLevel, PingLevel);
		m_pingFrames = 0;
		m_ping.store(static_cast<int>(PingState::Waiting), std::memory_order_release);
	}
	else if (state == PingState::Waiting) { std::fill_n(m_send.begin(), frames, SampleFrame{}); }
	else { std::copy_n(buffer, frames, m_send.begin()); }
	m_sent = true;
	pullReturn(engine, frames);
	listenForPing(frames, engine.outputSampleRate());
	writeOutput(buffer, frames, state == PingState::Requested || state == PingState::Waiting);
}

void HardwareInsert::pullReturn(AudioEngine& engine, f_cnt_t frames) noexcept
{
	pushCaptured(engine);
	// A whole period, or silence while the ring is still filling (the return keeps its alignment).
	const auto need = static_cast<std::size_t>(frames);
	if (m_ringFill < need)
	{
		std::fill_n(m_return.begin(), frames, SampleFrame{});
		return;
	}
	const std::size_t size = m_ring.size();
	for (std::size_t f = 0; f < need; ++f) { m_return[f] = m_ring[(m_ringRead + f) % size]; }
	m_ringRead = (m_ringRead + need) % size;
	m_ringFill -= need;
}

void HardwareInsert::pushCaptured(AudioEngine& engine) noexcept
{
	// The N-channel capture block when the backend delivers one, else the stereo bus (inputs 1/2).
	const float* wide = engine.inputWideBuffer();
	const bool useWide = wide != nullptr && engine.inputWideChannels() > 0 && engine.inputWideFrames() > 0;
	const int width = useWide ? engine.inputWideChannels() : DEFAULT_CHANNELS;
	const f_cnt_t captured = useWide ? engine.inputWideFrames() : engine.inputBufferFrames();
	const SampleFrame* stereo = engine.inputBuffer();
	const int left = m_settings.ret.start;
	const int right = m_settings.ret.width == 1 ? left : left + 1;
	const std::size_t size = m_ring.size();
	for (f_cnt_t f = 0; f < captured && m_ringFill < size && left >= 0; ++f)
	{
		const float* frame = useWide ? wide + static_cast<std::size_t>(f) * width : stereo[f].data();
		m_ring[(m_ringRead + m_ringFill) % size] = SampleFrame(wideSample(frame, width, left), wideSample(frame, width, right));
		++m_ringFill;
	}
}

void HardwareInsert::listenForPing(f_cnt_t frames, sample_rate_t rate) noexcept
{
	if (m_ping.load(std::memory_order_acquire) != static_cast<int>(PingState::Waiting)) { return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		if (std::abs(m_return[f][0]) > PingThreshold || std::abs(m_return[f][1]) > PingThreshold)
		{
			m_measured.store(m_pingFrames + static_cast<int>(f), Relaxed);
			m_ping.store(static_cast<int>(PingState::Done), std::memory_order_release);
			return;
		}
	}
	m_pingFrames += static_cast<int>(frames);
	if (m_pingFrames > static_cast<int>(rate) * PingTimeoutSeconds)
	{
		m_ping.store(static_cast<int>(PingState::Failed), std::memory_order_release);
	}
}

void HardwareInsert::writeOutput(SampleFrame* buffer, f_cnt_t frames, bool measuring) noexcept
{
	// The dry signal waits as long as the round trip, so the two halves of the mix line up.
	m_dry.setDelayFrames(latencyFrames());
	const SampleFrame* dry = m_dry.process(buffer, frames);
	const float wet = measuring ? 0.f : std::clamp(m_settings.mix, 0.f, 1.f);
	const float dryGain = 1.f - wet;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buffer[f] = SampleFrame(dry[f][0] * dryGain + m_return[f][0] * wet, dry[f][1] * dryGain + m_return[f][1] * wet);
	}
}

const SampleFrame* HardwareInsert::takeSend() noexcept
{
	if (!m_sent) { return nullptr; }
	m_sent = false;
	return m_send.data();
}

void HardwareInsert::save(QDomDocument& doc, QDomElement& channelElement) const
{
	const Settings current = settings();
	if (current == Settings{}) { return; }
	QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
	element.setAttribute(QStringLiteral("on"), current.enabled ? 1 : 0);
	element.setAttribute(QStringLiteral("send"), audioio::channelLabel(current.send));
	element.setAttribute(QStringLiteral("return"), audioio::channelLabel(current.ret));
	element.setAttribute(QStringLiteral("mix"), static_cast<double>(current.mix));
	element.setAttribute(QStringLiteral("latency"), current.latencyOverride);
	element.setAttribute(QStringLiteral("measured"), current.measured);
	channelElement.appendChild(element);
}

void HardwareInsert::load(const QDomElement& channelElement, f_cnt_t frames)
{
	const QDomElement element = channelElement.firstChildElement(QStringLiteral("hardwareinsert"));
	Settings loaded;
	if (!element.isNull())
	{
		loaded.enabled = element.attribute(QStringLiteral("on")).toInt() != 0;
		loaded.send = destinationAttribute(element, "send", loaded.send);
		loaded.ret = destinationAttribute(element, "return", loaded.ret);
		const float mix = element.attribute(QStringLiteral("mix"), QStringLiteral("1")).toFloat();
		loaded.mix = std::isfinite(mix) ? std::clamp(mix, 0.f, 1.f) : Settings{}.mix;
		bool latencyOk = false;
		const int latency = element.attribute(QStringLiteral("latency"), QStringLiteral("-1")).toInt(&latencyOk);
		if (latencyOk) { loaded.latencyOverride = latency; }
		bool measuredOk = false;
		const int measured = element.attribute(QStringLiteral("measured"), QStringLiteral("-1")).toInt(&measuredOk);
		if (measuredOk) { loaded.measured = measured; }
	}
	setSettings(loaded, frames);
	m_ping.store(static_cast<int>(PingState::Idle), Relaxed);
}

} // namespace lmms
