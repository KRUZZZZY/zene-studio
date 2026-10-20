/*
 * HardwareIo.cpp - a mixer channel's hardware side: its direct out, its cue sends, its hardware insert
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

#include "HardwareIo.h"

#include <algorithm>
#include <cmath>

#include <QDomDocument>
#include <QDomElement>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerListen.h"
#include "MixerSurround.h"
#include "Song.h"

namespace lmms
{

namespace
{

std::array<hardwareio::CueMaster, hardwareio::MaxCues> s_cueMasters{};
bool s_listenToCue = false;

//! The render thread's scratch: one channel's period, and one cue mix's sum (the engine's period is at
//! most DEFAULT_BUFFER_SIZE frames - AudioEngine's constructor caps it there).
std::array<SampleFrame, DEFAULT_BUFFER_SIZE> s_channelScratch{};
std::array<SampleFrame, DEFAULT_BUFFER_SIZE> s_cueScratch{};

//! An export runs no device: the hardware inserts are bypassed and report no latency meanwhile.
bool exporting()
{
	const Song* song = Engine::getSong();
	return song != nullptr && song->isExporting();
}

audioio::Destination readDestination(const QDomElement& element, const QString& name)
{
	audioio::Destination out;
	return audioio::parseDestination(element.attribute(name), audioio::Side{}, &out, nullptr) ? out : audioio::Destination{};
}

//! A saved 0/1 flag. A malformed value ("abc") does not convert, so it keeps @a fallback rather than reading as 0.
bool savedFlag(const QDomElement& element, const QString& name, bool fallback)
{
	bool valid = false;
	const int value = element.attribute(name).toInt(&valid);
	return valid ? value != 0 : fallback;
}

void saveCueSends(QDomDocument& doc, QDomElement& element, const ChannelHardwareIo& io)
{
	for (int k = 0; k < hardwareio::MaxCues; ++k)
	{
		const hardwareio::CueSend& send = io.cues[static_cast<std::size_t>(k)];
		if (send == hardwareio::CueSend{}) { continue; }
		QDomElement cue = doc.createElement(QStringLiteral("cue"));
		cue.setAttribute(QStringLiteral("index"), k);
		cue.setAttribute(QStringLiteral("level"), static_cast<double>(send.level));
		cue.setAttribute(QStringLiteral("pan"), static_cast<double>(send.pan));
		cue.setAttribute(QStringLiteral("prefader"), send.preFader ? 1 : 0);
		element.appendChild(cue);
	}
}

void loadCueSends(const QDomElement& element, ChannelHardwareIo& io)
{
	for (QDomElement cue = element.firstChildElement(QStringLiteral("cue")); !cue.isNull(); cue = cue.nextSiblingElement(QStringLiteral("cue")))
	{
		bool validIndex = false;
		const int k = cue.attribute(QStringLiteral("index")).toInt(&validIndex);
		if (!validIndex || k < 0 || k >= hardwareio::MaxCues) { continue; }
		hardwareio::CueSend& send = io.cues[static_cast<std::size_t>(k)];
		// A hand-edited project can carry level="nan": std::clamp lets NaN through, so a non-finite
		// figure falls back to the attribute's own default (no send, centred).
		const float sendLevel = cue.attribute(QStringLiteral("level")).toFloat();
		send.level = std::isfinite(sendLevel) ? std::clamp(sendLevel, 0.f, 2.f) : 0.f;
		const float sendPan = cue.attribute(QStringLiteral("pan")).toFloat();
		send.pan = std::isfinite(sendPan) ? std::clamp(sendPan, -1.f, 1.f) : 0.f;
		send.preFader = savedFlag(cue, QStringLiteral("prefader"), true);
	}
}

//! Channel @a channel's direct out and hardware insert send, onto @a bus.
void addChannelOutputs(HardwareOutputBus& bus, MixerChannel& channel, f_cnt_t frames)
{
	const SampleFrame* send = channel.m_hardwareIo.insert.takeSend();
	if (channel.m_muted.load(std::memory_order_relaxed)) { return; }
	if (send != nullptr)
	{
		const audioio::Destination to = channel.m_hardwareIo.insert.settings().send;
		bus.add(to.start, to.width, send, frames);
	}
	const audioio::Destination out = channel.m_hardwareIo.effectiveOutput(false);
	if (channel.index() == 0 || out.none()) { return; }
	std::fill_n(s_channelScratch.begin(), frames, SampleFrame{});
	mixerlisten::addChannelSignal(s_channelScratch.data(), channel, true, frames);
	// 0.5.0 F7 (MX-060): a surround channel's other speakers after the pair, or folded into it (MixerSurround.h).
	surround::directOut(bus, channel, out.start, out.width, s_channelScratch.data(), frames);
	bus.add(out.start, out.width, s_channelScratch.data(), frames);
}

//! Cue mix @a cue's sum into s_cueScratch: the listened channels while it follows a listen, else every
//! unmuted channel's send to it (the master's is its main mix after the fader, @a master).
void sumCue(Mixer& mixer, int cue, const SampleFrame* master, f_cnt_t frames)
{
	std::fill_n(s_cueScratch.begin(), frames, SampleFrame{});
	if (s_cueMasters[static_cast<std::size_t>(cue)].followListen && mixerlisten::anyListening())
	{
		mixerlisten::replaceOutput(s_cueScratch.data(), mixer, frames);
		return;
	}
	for (int i = 0; i < mixer.numChannels(); ++i)
	{
		MixerChannel* channel = mixer.mixerChannel(i);
		const hardwareio::CueSend& send = channel->m_hardwareIo.cues[static_cast<std::size_t>(cue)];
		if (send.level <= 0.f || channel->m_muted.load(std::memory_order_relaxed)) { continue; }
		if (i > 0)
		{
			mixerlisten::addChannelSignal(s_cueScratch.data(), *channel, !send.preFader, frames, send.level, send.pan);
			continue;
		}
		const float left = send.level * std::min(1.f, 1.f - send.pan);
		const float right = send.level * std::min(1.f, 1.f + send.pan);
		for (f_cnt_t f = 0; f < frames; ++f) { s_cueScratch[f] += SampleFrame(master[f][0] * left, master[f][1] * right); }
	}
}

} // namespace

audioio::Destination ChannelHardwareIo::effectiveOutput(bool master) const
{
	if (outputSet) { return output; }
	return master ? audioio::Destination{0, 2} : audioio::Destination{};
}

bool ChannelHardwareIo::usesCues() const
{
	return std::any_of(cues.begin(), cues.end(), [](const hardwareio::CueSend& send) { return !(send == hardwareio::CueSend{}); });
}

void ChannelHardwareIo::save(QDomDocument& doc, QDomElement& channelElement) const
{
	if (!outputSet && !usesCues() && insert.settings() == HardwareInsert::Settings{}) { return; }
	QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
	if (outputSet) { element.setAttribute(QStringLiteral("output"), audioio::channelLabel(output)); }
	saveCueSends(doc, element, *this);
	insert.save(doc, element);
	channelElement.appendChild(element);
}

void ChannelHardwareIo::load(const QDomElement& channelElement, f_cnt_t frames)
{
	clear();
	const QDomElement element = channelElement.firstChildElement(QStringLiteral("hardwareio"));
	if (element.hasAttribute(QStringLiteral("output")))
	{
		outputSet = true;
		output = readDestination(element, QStringLiteral("output"));
	}
	loadCueSends(element, *this);
	insert.load(element, frames);
}

void ChannelHardwareIo::clear()
{
	outputSet = false;
	output = audioio::Destination{};
	cues.fill(hardwareio::CueSend{});
	insert.setSettings(HardwareInsert::Settings{}, 0);
}

namespace hardwareio
{

const CueMaster& cueMaster(int cue)
{
	return s_cueMasters[static_cast<std::size_t>(std::clamp(cue, 0, MaxCues - 1))];
}

void setCueMaster(int cue, const CueMaster& master)
{
	if (cue >= 0 && cue < MaxCues) { s_cueMasters[static_cast<std::size_t>(cue)] = master; }
}

QString cueName(int cue)
{
	const int index = std::clamp(cue, 0, MaxCues - 1);
	const CueMaster& master = s_cueMasters[static_cast<std::size_t>(index)];
	return master.name.isEmpty() ? QStringLiteral("Cue %1").arg(QChar('A' + index)) : master.name;
}

bool listenToCue() { return s_listenToCue; }

void setListenToCue(bool toCue) { s_listenToCue = toCue; }

bool listenSparesMain()
{
	return s_listenToCue && std::any_of(s_cueMasters.begin(), s_cueMasters.end(),
		[](const CueMaster& cue) { return cue.followListen && cue.level > 0.f && !cue.output.none(); });
}

void processInsert(MixerChannel& channel, f_cnt_t frames)
{
	HardwareInsert& insert = channel.m_hardwareIo.insert;
	// An export runs no device, so no outboard gear: the insert is bypassed (the dry signal passes).
	if (!insert.enabled() || exporting()) { return; }
	insert.process(channel.m_buffer, frames, *Engine::audioEngine());
	channel.m_bus.quietChannels().reset(0);
	channel.m_bus.quietChannels().reset(1);
	channel.m_stillRunning = true;
}

int insertLatency(const MixerChannel& channel)
{
	return exporting() ? 0 : std::max(0, channel.m_hardwareIo.insert.latencyFrames());
}

void mixOutputs(Mixer& mixer, const SampleFrame* master, f_cnt_t frames)
{
	HardwareOutputBus& bus = Engine::audioEngine()->hardwareOutputs();
	if (frames > bus.frames() || frames > static_cast<f_cnt_t>(s_channelScratch.size())) { return; }
	const audioio::Destination main = mixer.mixerChannel(0)->m_hardwareIo.effectiveOutput(true);
	bus.setMasterPlacement({main.start, main.width});
	for (int i = 0; i < mixer.numChannels(); ++i) { addChannelOutputs(bus, *mixer.mixerChannel(i), frames); }
	for (int cue = 0; cue < MaxCues; ++cue)
	{
		const CueMaster& target = s_cueMasters[static_cast<std::size_t>(cue)];
		if (target.output.none() || target.level <= 0.f) { continue; }
		sumCue(mixer, cue, master, frames);
		bus.add(target.output.start, target.output.width, s_cueScratch.data(), frames, target.level);
	}
}

void listenOnMain(SampleFrame* master, Mixer& mixer, f_cnt_t frames)
{
	if (!listenSparesMain()) { mixerlisten::replaceOutput(master, mixer, frames); }
}

void saveMixer(QDomDocument& doc, QDomElement& mixerElement)
{
	for (int k = 0; k < MaxCues; ++k)
	{
		const CueMaster& cue = s_cueMasters[static_cast<std::size_t>(k)];
		if (cue == CueMaster{}) { continue; }
		QDomElement element = doc.createElement(QStringLiteral("cuemix"));
		element.setAttribute(QStringLiteral("index"), k);
		if (!cue.name.isEmpty()) { element.setAttribute(QStringLiteral("name"), cue.name); }
		element.setAttribute(QStringLiteral("level"), static_cast<double>(cue.level));
		element.setAttribute(QStringLiteral("output"), audioio::channelLabel(cue.output));
		element.setAttribute(QStringLiteral("listen"), cue.followListen ? 1 : 0);
		mixerElement.appendChild(element);
	}
	if (s_listenToCue)
	{
		QDomElement element = doc.createElement(QStringLiteral("cuelisten"));
		element.setAttribute(QStringLiteral("target"), QStringLiteral("cue"));
		mixerElement.appendChild(element);
	}
}

void loadMixer(const QDomElement& mixerElement)
{
	resetMixer();
	for (QDomElement element = mixerElement.firstChildElement(QStringLiteral("cuemix")); !element.isNull();
		element = element.nextSiblingElement(QStringLiteral("cuemix")))
	{
		bool validIndex = false;
		const int k = element.attribute(QStringLiteral("index")).toInt(&validIndex);
		if (!validIndex || k < 0 || k >= MaxCues) { continue; }
		CueMaster& cue = s_cueMasters[static_cast<std::size_t>(k)];
		cue.name = element.attribute(QStringLiteral("name"));
		const float cueLevel = element.attribute(QStringLiteral("level"), QStringLiteral("1")).toFloat();
		cue.level = std::isfinite(cueLevel) ? std::clamp(cueLevel, 0.f, 2.f) : 1.f;  // non-finite: the cuemix's default
		cue.output = readDestination(element, QStringLiteral("output"));
		cue.followListen = savedFlag(element, QStringLiteral("listen"), true);
	}
	s_listenToCue = mixerElement.firstChildElement(QStringLiteral("cuelisten")).attribute(QStringLiteral("target")) == QStringLiteral("cue");
}

void resetMixer()
{
	s_cueMasters.fill(CueMaster{});
	s_listenToCue = false;
}

} // namespace hardwareio

} // namespace lmms
