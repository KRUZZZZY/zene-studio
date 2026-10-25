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
#include "Engine.h"

namespace lmms
{

namespace
{

//! The ping's state machine lives in one word: the audio thread must never take a lock for it.
static_assert(std::atomic<std::int64_t>::is_always_lock_free,
	"the ping's state machine must be a lock-free atomic");
static_assert(std::atomic<sample_rate_t>::is_always_lock_free,
	"the measured sample rate must be a lock-free atomic");

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

bool hasMeasurementContext(const HardwareInsert::Settings& settings, sample_rate_t rate)
{
	return settings.measured >= 0 && settings.measuredRate == rate
		&& settings.measuredSend == settings.send && settings.measuredReturn == settings.ret;
}

void normalizeMeasurementContext(HardwareInsert::Settings& settings, sample_rate_t rate)
{
	if (settings.measured < 0)
	{
		settings.measuredRate = 0;
		return;
	}
	if (settings.measuredRate == 0 && settings.measuredSend == settings.send && settings.measuredReturn == settings.ret)
	{
		settings.measuredRate = rate;
	}
	if (!hasMeasurementContext(settings, rate))
	{
		settings.measured = -1;
		settings.measuredRate = 0;
	}
}

void loadMeasurement(const QDomElement& element, sample_rate_t rate, HardwareInsert::Settings& loaded)
{
	bool measuredOk = false;
	const int measured = element.attribute(QStringLiteral("measured"), QStringLiteral("-1")).toInt(&measuredOk);
	if (!measuredOk) { return; }
	loaded.measured = measured;
	if (loaded.measured < 0) { return; }
	bool measuredRateOk = !element.hasAttribute(QStringLiteral("measured_rate"));
	sample_rate_t measuredRate = rate;  // older projects have no rate metadata; keep their prior interpretation
	if (element.hasAttribute(QStringLiteral("measured_rate")))
	{
		const uint parsedRate = element.attribute(QStringLiteral("measured_rate")).toUInt(&measuredRateOk);
		if (measuredRateOk) { measuredRate = static_cast<sample_rate_t>(parsedRate); }
	}
	loaded.measuredRate = measuredRate;
	if (!measuredRateOk || measuredRate != rate) { loaded.measured = -1; }
}

sample_rate_t currentOutputSampleRate()
{
	const AudioEngine* engine = Engine::audioEngine();
	return engine == nullptr ? 0 : engine->outputSampleRate();
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
	out.measured = measuredFrames();
	out.measuredRate = out.measured >= 0 ? m_measurementRate.load(std::memory_order_acquire) : 0;
	if (out.measured >= 0 && out.measuredRate != currentOutputSampleRate())
	{
		out.measured = -1;
		out.measuredRate = 0;
	}
	out.measuredSend = out.send;
	out.measuredReturn = out.ret;
	return out;
}

void HardwareInsert::setSettings(const Settings& settings, f_cnt_t frames)
{
	const sample_rate_t rate = currentOutputSampleRate();
	if (settings.enabled) { prepare(frames); }
	// Switched on: the return starts from an empty ring, so what it held when it went off cannot shift
	// the round trip the ping measured (the render is held off: the caller has the change lock).
	if (settings.enabled && (!m_settings.enabled || settings.ret != m_settings.ret))
	{
		m_ringRead = 0;
		m_ringFill = 0;
	}
	const bool routeChanged = settings.send != m_settings.send || settings.ret != m_settings.ret;
	const bool waitingAtAnotherRate = pingState() == PingState::Waiting && m_pingSampleRate != rate;
	if (routeChanged || !settings.enabled || waitingAtAnotherRate) { movePingTo(PingState::Idle); }
	Settings applied = settings;
	normalizeMeasurementContext(applied, rate);
	applied.measuredSend = applied.send;
	applied.measuredReturn = applied.ret;
	m_settings = applied;
	m_measurementRate.store(applied.measuredRate, std::memory_order_release);
	setMeasured(applied.measured);
}

int HardwareInsert::latencyFrames() const
{
	if (!m_settings.enabled) { return 0; }
	if (m_settings.latencyOverride >= 0) { return m_settings.latencyOverride; }
	const int measured = measuredFrames();
	return measured >= 0 && m_measurementRate.load(std::memory_order_acquire) == currentOutputSampleRate()
		? measured : 0;
}

void HardwareInsert::requestPing()
{
	movePingTo(PingState::Requested);
}

void HardwareInsert::cancelPing()
{
	movePingTo(PingState::Idle);
}

void HardwareInsert::movePingTo(PingState state) noexcept
{
	// A compare-and-swap, so this move wins against a period that is already on its way: a
	// period that had read the request cannot put the ping back behind the cancel, and a ping
	// that is called off keeps the figure it had. The audio thread moves the ping on twice a
	// period at most, so the bounded loop cannot spin.
	PingWord word = m_ping.load(std::memory_order_acquire);
	for (int attempt = 0; attempt < 8; ++attempt)
	{
		PingWord expected = word;
		if (m_ping.compare_exchange_strong(expected, packPing(measuredOf(word), state),
				std::memory_order_acq_rel, std::memory_order_acquire))
		{ return; }
		word = expected;
	}
}

void HardwareInsert::setMeasured(int measured) noexcept
{
	// The figure is part of the state machine's word: the state is kept, the figure is
	// replaced. A period that publishes its own figure at the same time wins - the next
	// setSettings carries it on.
	PingWord word = m_ping.load(std::memory_order_acquire);
	for (int attempt = 0; attempt < 8; ++attempt)
	{
		PingWord expected = word;
		if (m_ping.compare_exchange_strong(expected, packPing(measured, pingStateOf(word)),
				std::memory_order_acq_rel, std::memory_order_acquire))
		{ return; }
		word = expected;
	}
}

void HardwareInsert::process(SampleFrame* buffer, f_cnt_t frames, AudioEngine& engine) noexcept
{
	if (!m_settings.enabled || m_send.size() < static_cast<std::size_t>(frames)) { return; }
	const sample_rate_t rate = engine.outputSampleRate();
	invalidateMeasurementForRate(rate);
	PingWord word = pingWord();
	auto state = pingStateOf(word);
	if (state == PingState::Waiting && m_pingSampleRate != rate)
	{
		cancelPing();
		word = pingWord();
		state = pingStateOf(word);
	}
	bool measuring = false;
	if (state == PingState::Requested && claimPing(word, rate))
	{
		// The click, alone on the send: the music stays off it until the click is heard back.
		std::fill_n(m_send.begin(), frames, SampleFrame{});
		m_send[0] = SampleFrame(PingLevel, PingLevel);
		measuring = true;
	}
	else if (state == PingState::Waiting)
	{
		std::fill_n(m_send.begin(), frames, SampleFrame{});
		measuring = true;
	}
	else { std::copy_n(buffer, frames, m_send.begin()); }
	m_sent = true;
	pullReturn(engine, frames);
	listenForPing(frames, rate);
	writeOutput(buffer, frames, measuring);
}

void HardwareInsert::invalidateMeasurementForRate(sample_rate_t rate) noexcept
{
	if (measuredFrames() < 0 || m_measurementRate.load(std::memory_order_acquire) == rate) { return; }
	m_measurementRate.store(0, std::memory_order_release);
	setMeasured(-1);
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
	const PingWord word = pingWord();
	if (pingStateOf(word) != PingState::Waiting) { return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		if (std::abs(m_return[f][0]) > PingThreshold || std::abs(m_return[f][1]) > PingThreshold)
		{
			m_measurementRate.store(rate, std::memory_order_relaxed);
			publishPingResult(word, m_pingFrames + static_cast<int>(f));
			return;
		}
	}
	m_pingFrames += static_cast<int>(frames);
	if (m_pingFrames > static_cast<int>(rate) * PingTimeoutSeconds) { failPing(word); }
}

bool HardwareInsert::claimPing(PingWord expected) noexcept
{
	return claimPing(expected, currentOutputSampleRate());
}

bool HardwareInsert::claimPing(PingWord expected, sample_rate_t rate) noexcept
{
	// The claim goes through only when the word is still the one the period read: a ping that
	// was called off (or replaced) in the meantime stays as it is, and no click goes out.
	PingWord word = expected;
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		if (pingStateOf(word) != PingState::Requested) { return false; }
		PingWord current = word;
		if (m_ping.compare_exchange_strong(current, packPing(measuredOf(word), PingState::Waiting),
				std::memory_order_acq_rel, std::memory_order_acquire))
		{
			m_pingFrames = 0;
			m_pingSampleRate = rate;
			return true;
		}
		word = current;  // the failed swap wrote the word as it is now back
	}
	return false;
}

bool HardwareInsert::publishPingResult(PingWord expected, int frames) noexcept
{
	// The figure and the state are published together, so a ping that was called off in the
	// meantime keeps its answer to itself: the undo's project keeps the figure it had.
	PingWord word = expected;
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		if (pingStateOf(word) != PingState::Waiting) { return false; }
		PingWord current = word;
		if (m_ping.compare_exchange_strong(current, packPing(frames, PingState::Done),
				std::memory_order_acq_rel, std::memory_order_acquire))
		{ return true; }
		word = current;
	}
	return false;
}

void HardwareInsert::failPing(PingWord expected) noexcept
{
	// The ping heard nothing in time: it failed, the figure it had is untouched.
	PingWord word = expected;
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		if (pingStateOf(word) != PingState::Waiting) { return; }
		PingWord current = word;
		if (m_ping.compare_exchange_strong(current, packPing(measuredOf(word), PingState::Failed),
				std::memory_order_acq_rel, std::memory_order_acquire))
		{ return; }
		word = current;
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
	if (current.measured >= 0) { element.setAttribute(QStringLiteral("measured_rate"), current.measuredRate); }
	channelElement.appendChild(element);
}

void HardwareInsert::load(const QDomElement& channelElement, f_cnt_t frames)
{
	const sample_rate_t rate = currentOutputSampleRate();
	const QDomElement element = channelElement.firstChildElement(QStringLiteral("hardwareinsert"));
	Settings loaded;
	if (!element.isNull())
	{
		loaded.enabled = element.attribute(QStringLiteral("on")).toInt() != 0;
		loaded.send = destinationAttribute(element, "send", loaded.send);
		loaded.ret = destinationAttribute(element, "return", loaded.ret);
		bool mixOk = false;
		const float mix = element.attribute(QStringLiteral("mix"), QStringLiteral("1")).toFloat(&mixOk);
		loaded.mix = mixOk && std::isfinite(mix) ? std::clamp(mix, 0.f, 1.f) : Settings{}.mix;  // unparsable or non-finite: the default
		bool latencyOk = false;
		const int latency = element.attribute(QStringLiteral("latency"), QStringLiteral("-1")).toInt(&latencyOk);
		if (latencyOk) { loaded.latencyOverride = latency; }
		loadMeasurement(element, rate, loaded);
		loaded.measuredSend = loaded.send;
		loaded.measuredReturn = loaded.ret;
	}
	setSettings(loaded, frames);
	movePingTo(PingState::Idle);
}

} // namespace lmms
