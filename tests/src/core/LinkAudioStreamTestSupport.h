/*
 * LinkAudioStreamTestSupport.h - the Link-active scenario of LinkAudioStreamTest (Q050-024):
 *                                two Link peers stepped through tempo, start and stop, plus the
 *                                multicast-membership and ordering checks. Header-only, test use.
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


#ifndef LMMS_TEST_LINK_AUDIO_STREAM_TEST_SUPPORT_H
#define LMMS_TEST_LINK_AUDIO_STREAM_TEST_SUPPORT_H

#include <QString>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "AbletonLinkSession.h"
#include "LinkAudioStream.h"
#include "SampleFrame.h"

namespace lmms::test::linkaudio
{

using namespace lmms;
using namespace lmms::link;
constexpr sample_rate_t Rate = 48000;

AudioStreamConfig receiver(int latencyMs = 20)
{
	AudioStreamConfig c;
	c.enabled = true;
	c.receive = true;
	c.send = false;
	c.listenPort = 0;
	c.latencyMs = latencyMs;
	return c;
}

AudioStreamConfig senderTo(int port)
{
	AudioStreamConfig c;
	c.enabled = true;
	c.receive = false;
	c.send = true;
	c.peerAddress = "127.0.0.1";
	c.peerPort = port;
	return c;
}

//! A period whose left channel counts frames from \a first (exact in float up to 2^24) and right is its negative.
void fillRamp(std::vector<SampleFrame>& buffer, std::uint64_t first)
{
	for (std::size_t i = 0; i < buffer.size(); ++i)
	{
		const float v = static_cast<float>(first + i);
		buffer[i] = SampleFrame(v, -v);
	}
}


//! Which interfaces joined Link's multicast group 224.76.78.75 (Linux: /proc/net/igmp; empty elsewhere).
std::vector<std::string> interfacesInLinkGroup()
{
	std::vector<std::string> devices;
	std::ifstream igmp("/proc/net/igmp");
	std::string line, device;
	while (std::getline(igmp, line))
	{
		if (!line.empty() && line[0] >= '0' && line[0] <= '9')
		{
			std::istringstream head(line);
			std::string index;
			head >> index >> device;
		}
		else if (line.find("4B4E4CE0") != std::string::npos)   // 224.76.78.75, network order read little-endian
		{
			devices.push_back(device);
		}
	}
	return devices;
}

bool nearTempo(double a, double b) { return std::fabs(a - b) < 0.01; }

//! Two Link participants stepped through tempo, start and stop, one transition per advance().
class LinkScenario
{
public:
	static constexpr double Quantum = 4.0;
	LinkScenario() : m_a(120.0), m_b(90.0)
	{
		m_a.enable(true);
		m_b.enable(true);
		m_a.enableStartStopSync(true);
		m_b.enableStartStopSync(true);
	}
	bool done() const { return m_stage == 4; }
	//! Step the state machine; false when a stage has taken ~4.5 s.
	bool advance(int period)
	{
		if (moveOn()) { m_stageStart = period; }
		return period - m_stageStart < 3500;
	}
	QString describeStall(int) const
	{
		return QStringLiteral("Link stage %1 did not complete (A sees %2 peers, B %3)")
			.arg(m_stage).arg(m_a.numPeers()).arg(m_b.numPeers());
	}
	//! Whether Link's multicast group is joined on lo alone; nothing where the platform cannot be inspected (only Linux can).
	static std::optional<bool> confinedToLoopback()
	{
#ifdef __linux__
		const std::vector<std::string> devices = interfacesInLinkGroup();
		return !devices.empty() && std::all_of(devices.begin(), devices.end(), [](const std::string& d) { return d == "lo"; });
#else
		return std::nullopt;
#endif
	}
	double phaseError()
	{
		const std::int64_t at = m_a.clockMicros() + 50000;
		const LinkTimelinePoint a = m_a.audioPeriod(at, Quantum, LinkAudioChange{});
		const LinkTimelinePoint b = m_b.audioPeriod(at, Quantum, LinkAudioChange{});
		double error = a.phase - b.phase;
		return error - Quantum * std::floor(error / Quantum + 0.5);
	}
	bool temposAgree() const { return nearTempo(m_a.appCapture(Quantum).tempo, m_b.appCapture(Quantum).tempo); }

private:
	//! True when this call completed a stage and began the next.
	bool moveOn()
	{
		switch (m_stage)
		{
		case 0:
			if (m_a.numPeers() < 1 || m_b.numPeers() < 1) { return false; }
			m_a.appSetTempo(133.5);
			break;
		case 1:
			if (!nearTempo(m_b.appCapture(Quantum).tempo, 133.5)) { return false; }
			m_a.audioPeriod(m_a.clockMicros(), Quantum, change(1));
			break;
		case 2:
			if (!m_b.appCapture(Quantum).playing) { return false; }
			m_b.audioPeriod(m_b.clockMicros(), Quantum, change(0));
			break;
		default:
			if (m_a.appCapture(Quantum).playing) { return false; }
			break;
		}
		++m_stage;
		return true;
	}
	static LinkAudioChange change(int playing) { LinkAudioChange c; c.playing = playing; return c; }

	AbletonLinkSession m_a, m_b;
	int m_stage = 0;      // 0 join, 1 tempo, 2 start, 3 stop, 4 done
	int m_stageStart = 0;
};

//! Strictly increasing non-zero samples (the ramp), at least \a minimum of them.
bool isOrdered(const std::vector<float>& heard, std::size_t minimum)
{
	float last = 0.f;
	std::size_t audible = 0;
	for (float v : heard)
	{
		if (v == 0.f) { continue; }
		if (v <= last) { return false; }
		last = v;
		++audible;
	}
	return audible >= minimum;
}


} // namespace lmms::test::linkaudio

#endif // LMMS_TEST_LINK_AUDIO_STREAM_TEST_SUPPORT_H
