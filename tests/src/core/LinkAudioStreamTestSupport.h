/*
 * LinkAudioStreamTestSupport.h - the shared helpers of LinkAudioStreamTest (Q050-024): the
 *                                Link-active scenario (two Link peers stepped through tempo,
 *                                start and stop, plus the multicast-membership and ordering
 *                                checks) and the loopback helpers the stream's own regressions
 *                                use (send a packet, wait for a counted packet, check the rate
 *                                in the packets a peer received). Header-only, test use.
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

#include <QElapsedTimer>
#include <QHostAddress>
#include <QString>
#include <QUdpSocket>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
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

//! One packet of the stream, sent to \a port over a real loopback socket: \a frames, all of
//! them \a value, stamped with \a sequence and \a rate.
bool sendAudioPacket(QUdpSocket& socket, quint16 port, std::uint32_t sequence, std::uint32_t rate,
	std::uint16_t frames, float value)
{
	AudioPacketHeader header;
	header.sequence = sequence;
	header.sampleRate = rate;
	header.frames = frames;
	std::vector<float> samples(static_cast<std::size_t>(frames) * AudioChannels, value);
	std::vector<std::uint8_t> wire(AudioMaxPacketBytes);
	const std::size_t bytes = encodeAudioPacket(header, samples.data(), wire.data(), wire.size());
	if (bytes == 0) { return false; }
	return socket.writeDatagram(reinterpret_cast<const char*>(wire.data()), bytes, QHostAddress::LocalHost, port)
		== static_cast<qint64>(bytes);
}

//! Wait until \a stream has counted \a count packets: the wait is bounded and it waits for a
//! counter, not for a moment in time.
bool waitForPackets(const LinkAudioStream& stream, std::uint64_t count, int timeoutMs)
{
	QElapsedTimer clock;
	clock.start();
	while (stream.report().packetsReceived < count && clock.elapsed() < timeoutMs) { QTest::qWait(1); }
	return stream.report().packetsReceived >= count;
}

//! The rate a packet may carry and the value the frames of that rate hold.
using RateValues = std::vector<std::pair<std::uint32_t, float>>;

//! What the peer received: how many packets, how many of them carried frames of a rate that
//! was not the rate in their own header, and whether a packet of the new rate arrived.
struct PeerRateCheck
{
	int packets = 0;
	int mismatched = 0;
	bool sawNewRate = false;
};

//! The value the frames of \a rate must have, or -1 for a rate that may not come.
float expectedValue(const RateValues& values, std::uint32_t rate)
{
	for (const auto& value : values) { if (value.first == rate) { return value.second; } }
	return -1.f;
}

//! Read the datagrams waiting at \a peer and check each of them: the samples in a packet must
//! be the frames of the rate in its own header. A packet of the new rate carrying old-rate
//! samples is a mismatch - the peer would play them at the wrong speed.
void drainPeerPackets(QUdpSocket& peer, PeerRateCheck& check, const RateValues& values, std::uint32_t newRate)
{
	while (peer.hasPendingDatagrams())
	{
		QByteArray datagram;
		datagram.resize(static_cast<int>(peer.pendingDatagramSize()));
		const qint64 read = peer.readDatagram(datagram.data(), datagram.size());
		if (read <= 0) { continue; }
		AudioPacketHeader header;
		std::vector<float> samples(AudioMaxPacketFrames * AudioChannels, 0.f);
		if (!decodeAudioPacket(reinterpret_cast<const std::uint8_t*>(datagram.constData()),
				static_cast<std::size_t>(read), header, samples.data(), samples.size()))
		{
			continue;
		}
		++check.packets;
		const float expected = expectedValue(values, header.sampleRate);
		if (expected < 0.f) { ++check.mismatched; continue; }
		for (float v : samples) { if (v != expected) { ++check.mismatched; break; } }
		if (header.sampleRate == newRate) { check.sawNewRate = true; }
	}
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
