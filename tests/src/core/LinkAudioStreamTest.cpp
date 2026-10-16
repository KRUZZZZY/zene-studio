/*
 * LinkAudioStreamTest.cpp - the PCM stream between instances (Q050-024,
 *                           include/LinkAudioStream.h, docs/LINK-AUDIO.md).
 *
 * HERMETIC: every socket here is bound to 127.0.0.1 and every peer is 127.0.0.1. The one test that
 * runs Ableton Link confines its discovery to the loopback interface first (see pcmStreamsWhileLinkSyncIsActive).
 * Nothing is multicast or broadcast beyond the loopback interface (the Link test confines Link's discovery
 * to it), there is no name lookup, UPnP or internet.
 *
 * Proof of the acceptance criteria: the codec, the ring and the refusals pin the
 * building blocks; loopbackExchangeIsSampleExact moves a counting ramp from one
 * stream to another over a real UDP socket and compares every sample;
 * audioThreadAllocatesNothing counts operator new on the calling thread through
 * the shared AllocationProbe; realtimeStressReportsBoundedUnderrunsAndLatency runs
 * two threads on a 1 ms cadence (a 48 kHz, 48-frame period each) for 3 s and
 * bounds underruns, buffered latency and ordering; the command tests show the
 * registered surface, its undo, and that link.* (tempo, quantum) is untouched.
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

#include <QtTest>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <QJsonObject>

#include <fstream>
#include <optional>
#include <sstream>

#include "AbletonLinkSession.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "LinkAudioStream.h"
#include "SampleFrame.h"
// Naming the probe is what links it (tests/CMakeLists.txt); its operator new is the whole program's.
#include "AllocationProbe.h"
#include "LinkRealtimeProbe.h"

using namespace lmms;
using namespace lmms::link;

namespace
{

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

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
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

} // namespace

class LinkAudioStreamTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		std::string e;
		LinkAudioStream::instance()->configure(AudioStreamConfig{}, &e);
	}

	void packetCodecRoundTripsAndRejectsGarbage()
	{
		std::vector<float> in(AudioMaxPacketFrames * 2);
		for (std::size_t i = 0; i < in.size(); ++i) { in[i] = static_cast<float>(i) * 0.25f - 7.f; }
		AudioPacketHeader h;
		h.sequence = 0xFFFFFFFEu;
		h.sampleRate = 44100;
		h.frames = static_cast<std::uint16_t>(AudioMaxPacketFrames);
		std::vector<std::uint8_t> wire(AudioMaxPacketBytes);
		const std::size_t bytes = encodeAudioPacket(h, in.data(), wire.data(), wire.size());
		QCOMPARE(bytes, AudioMaxPacketBytes);
		QVERIFY(bytes < 1472);  // one LAN datagram

		AudioPacketHeader out;
		std::vector<float> samples(in.size());
		QVERIFY(decodeAudioPacket(wire.data(), bytes, out, samples.data(), samples.size()));
		QCOMPARE(out.sequence, h.sequence);
		QCOMPARE(out.sampleRate, h.sampleRate);
		QCOMPARE(out.frames, h.frames);
		QCOMPARE(samples, in);

		QVERIFY(!decodeAudioPacket(wire.data(), bytes - 1, out, samples.data(), samples.size()));  // truncated
		auto bad = wire;
		bad[0] = 'X';
		QVERIFY(!decodeAudioPacket(bad.data(), bytes, out, samples.data(), samples.size()));        // magic
		bad = wire;
		bad[14] = 6;
		QVERIFY(!decodeAudioPacket(bad.data(), bytes, out, samples.data(), samples.size()));        // channels
		QVERIFY(!decodeAudioPacket(wire.data(), 3, out, samples.data(), samples.size()));
		QCOMPARE(encodeAudioPacket(h, in.data(), wire.data(), 20), std::size_t{0});                 // does not fit
	}

	void ipv4ParserAcceptsLiteralsOnly()
	{
		std::uint32_t a = 0;
		QVERIFY(parseIpv4("127.0.0.1", a));
		QCOMPARE(a, 0x7F000001u);
		for (const char* bad : {"localhost", "example.com", "1.2.3", "1.2.3.4.5", "256.0.0.1", "1..2.3", "", "1.2.3.4 ", "::1"})
		{
			QVERIFY2(!parseIpv4(bad, a), bad);
		}
	}

	void ringKeepsOrderAcrossWrapAndRefusesOverflow()
	{
		AudioRing ring(8);
		QCOMPARE(ring.capacity(), std::size_t{8});
		std::vector<float> in(24), out(24);
		for (std::size_t i = 0; i < in.size(); ++i) { in[i] = static_cast<float>(i); }
		QCOMPARE(ring.write(in.data(), 6), std::size_t{6});
		QCOMPARE(ring.read(out.data(), 4), std::size_t{4});
		QCOMPARE(ring.write(in.data() + 12, 6), std::size_t{6});   // wraps
		QCOMPARE(ring.fill(), std::size_t{8});
		QCOMPARE(ring.write(in.data(), 1), std::size_t{0});        // full: nothing written, nothing lost
		QCOMPARE(ring.skip(2), std::size_t{2});
		QCOMPARE(ring.read(out.data(), 99), std::size_t{6});
		// frames 0-5 written, 0-3 read; 6-11 (floats 12..23) written; skip 2 drops frames 4,5; the rest is 6..11.
		for (int i = 0; i < 6; ++i)
		{
			QCOMPARE(out[i * 2], in[12 + i * 2]);
			QCOMPARE(out[i * 2 + 1], in[12 + i * 2 + 1]);
		}
		QCOMPARE(ring.fill(), std::size_t{0});
	}

	void refusalsChangeNothing()
	{
		LinkAudioStream stream;
		std::string why;
		AudioStreamConfig c = receiver();
		c.latencyMs = 1;
		QVERIFY(!stream.configure(c, &why));
		c = senderTo(9);
		c.peerAddress = "localhost";                 // a name: never resolved
		QVERIFY(!stream.configure(c, &why));
		QVERIFY2(why.find("IPv4") != std::string::npos, why.c_str());
		c = senderTo(9);
		c.peerAddress.clear();                       // send with nowhere to send
		QVERIFY(!stream.configure(c, &why));
		c = receiver();
		c.receive = false;                           // neither direction
		QVERIFY(!stream.configure(c, &why));
		c = receiver();
		c.bindAddress = "not-an-address";
		QVERIFY(!stream.configure(c, &why));
		QVERIFY(!stream.config().enabled);
		QVERIFY(!stream.report().running);
		QCOMPARE(AudioStreamConfig{}.bindAddress, std::string("127.0.0.1"));  // loopback unless the user opts out
	}

	void loopbackExchangeIsSampleExact()
	{
		LinkAudioStream rx, tx;
		std::string why;
		QVERIFY2(rx.configure(receiver(10), &why), why.c_str());
		const int port = rx.report().boundPort;
		QVERIFY(port > 0);
		QVERIFY2(tx.configure(senderTo(port), &why), why.c_str());

		constexpr std::size_t Period = 64;
		std::vector<SampleFrame> out(Period), mix(Period);
		std::uint64_t produced = 0;
		std::vector<float> heard;   // left channel of everything rx added to silence
		for (int i = 0; i < 1500; ++i)
		{
			fillRamp(out, produced + 1);              // starts at 1: 0 means silence
			produced += Period;
			tx.process(out.data(), Period, Rate);
			std::fill(mix.begin(), mix.end(), SampleFrame(0.f, 0.f));
			rx.process(mix.data(), Period, Rate);
			for (const SampleFrame& f : mix) { heard.push_back(f.left()); QCOMPARE(f.right(), -f.left()); }
			std::this_thread::sleep_for(std::chrono::microseconds(1300));  // ~real time at 48 kHz
		}
		const AudioStreamReport r = rx.report();
		QVERIFY2(r.packetsReceived > 100, "the peer's packets arrived");
		QCOMPARE(tx.report().sendErrors, std::uint64_t{0});
		QCOMPARE(r.malformedPackets, std::uint64_t{0});
		// Everything audible is the ramp, in order, with no repeats; after the jitter buffer
		// primes there are no holes except where a counted underrun/loss put them.
		float last = 0.f;
		std::size_t audible = 0;
		for (float v : heard)
		{
			if (v == 0.f) { continue; }
			QVERIFY2(v == last + 1.f || last == 0.f || r.underruns + r.lostPackets + r.trims > 0, "gap with nothing counted");
			QVERIFY2(v > last, "ramp must stay in order");
			last = v;
			++audible;
		}
		QVERIFY2(audible > 40000, qPrintable(QString::number(audible)));
	}

	void audioThreadAllocatesNothing()
	{
		LinkAudioStream both;
		std::string why;
		AudioStreamConfig c = receiver(10);
		c.send = true;
		c.peerAddress = "127.0.0.1";
		c.peerPort = 9;   // discard port on loopback; nothing is meant to answer
		QVERIFY2(both.configure(c, &why), why.c_str());
		std::vector<SampleFrame> buffer(256);
		fillRamp(buffer, 1);
		both.process(buffer.data(), buffer.size(), Rate);  // first period (rate becomes known)
		const std::uint64_t allocations = test::link::allocationsDuring([&] {
			for (int i = 0; i < 200; ++i) { both.process(buffer.data(), buffer.size(), Rate); }
		});
		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(both.report().periods, std::uint64_t{201});
	}

	void realtimeStressReportsBoundedUnderrunsAndLatency()
	{
		LinkAudioStream rx, tx;
		std::string why;
		QVERIFY2(rx.configure(receiver(30), &why), why.c_str());
		QVERIFY2(tx.configure(senderTo(rx.report().boundPort), &why), why.c_str());

		constexpr std::size_t Period = 48;      // 1 ms at 48 kHz
		constexpr int Periods = 3000;
		std::atomic<bool> bad{false};
		std::vector<float> heard;
		heard.reserve(Periods * Period);
		const auto start = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
		std::thread sender([&] {
			std::vector<SampleFrame> buf(Period);
			for (int i = 0; i < Periods; ++i)
			{
				std::this_thread::sleep_until(start + std::chrono::milliseconds(i));
				fillRamp(buf, static_cast<std::uint64_t>(i) * Period + 1);
				tx.process(buf.data(), Period, Rate);
			}
		});
		std::thread player([&] {
			std::vector<SampleFrame> buf(Period);
			for (int i = 0; i < Periods; ++i)
			{
				std::this_thread::sleep_until(start + std::chrono::milliseconds(i) + std::chrono::microseconds(300));
				std::fill(buf.begin(), buf.end(), SampleFrame(0.f, 0.f));
				rx.process(buf.data(), Period, Rate);
				for (const SampleFrame& f : buf) { heard.push_back(f.left()); }
			}
		});
		sender.join();
		player.join();

		float last = 0.f;
		std::size_t audible = 0;
		for (float v : heard)
		{
			if (v == 0.f) { continue; }
			if (v <= last) { bad = true; }   // reordering or repetition
			last = v;
			++audible;
		}
		const AudioStreamReport r = rx.report();
		qInfo("stress: periods=%llu packets=%llu underruns=%llu trims=%llu lost=%llu reordered=%llu "
			"rx_overruns=%llu buffered_ms=%.2f max_ms=%.2f audible_frames=%zu tx: sent=%llu send_errors=%llu tx_overruns=%llu malformed=%llu rate_mismatch=%llu",
			static_cast<unsigned long long>(r.periods), static_cast<unsigned long long>(r.packetsReceived),
			static_cast<unsigned long long>(r.underruns), static_cast<unsigned long long>(r.trims),
			static_cast<unsigned long long>(r.lostPackets), static_cast<unsigned long long>(r.reorderedPackets),
			static_cast<unsigned long long>(r.rxOverruns), r.latencyMs, r.latencyMaxMs, audible,
			static_cast<unsigned long long>(tx.report().packetsSent), static_cast<unsigned long long>(tx.report().sendErrors),
			static_cast<unsigned long long>(tx.report().txOverruns), static_cast<unsigned long long>(r.malformedPackets),
			static_cast<unsigned long long>(r.rateMismatches));
		QVERIFY2(!bad.load(), "samples came out of order");
		QVERIFY2(r.underruns <= 30, "underruns must stay bounded (1% of periods)");
		QVERIFY2(r.latencyMaxMs <= 30.0 * 2 + 5.0 + 3.0, "buffered latency must stay under the high-water mark");
		QCOMPARE(r.rxOverruns, std::uint64_t{0});
		QVERIFY2(audible > static_cast<std::size_t>(Periods) * Period * 8 / 10, "most of the stream was heard");
		QCOMPARE(tx.report().txOverruns, std::uint64_t{0});
	}

	void packetsFromAStrangerOrAtAnotherRateAreCounted()
	{
		LinkAudioStream rx;
		std::string why;
		AudioStreamConfig c = receiver(10);
		c.peerAddress = "127.0.0.2";    // only this source is accepted; the test sends from 127.0.0.1
		QVERIFY2(rx.configure(c, &why), why.c_str());
		LinkAudioStream tx;
		QVERIFY2(tx.configure(senderTo(rx.report().boundPort), &why), why.c_str());
		std::vector<SampleFrame> buf(64);
		for (int i = 0; i < 50; ++i)
		{
			fillRamp(buf, 1);
			tx.process(buf.data(), 64, Rate);
			rx.process(buf.data(), 64, Rate);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		QCOMPARE(rx.report().packetsReceived, std::uint64_t{0});
		QVERIFY(rx.report().malformedPackets > 0);

		LinkAudioStream rx2, tx2;
		QVERIFY2(rx2.configure(receiver(10), &why), why.c_str());
		QVERIFY2(tx2.configure(senderTo(rx2.report().boundPort), &why), why.c_str());
		for (int i = 0; i < 50; ++i)
		{
			tx2.process(buf.data(), 64, 44100);   // sender at 44.1 kHz
			rx2.process(buf.data(), 64, Rate);    // receiver engine at 48 kHz
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		QCOMPARE(rx2.report().packetsReceived, std::uint64_t{0});
		QVERIFY(rx2.report().rateMismatches > 0);
	}

	/*! Criterion 1, with Link ACTIVE: two in-process Ableton Link participants whose discovery is
	 *  confined to the loopback interface (AbletonLinkSession::setLoopbackOnly - the scanner seam in
	 *  src/core/AbletonLinkSession.cpp) share a session while PCM streams over 127.0.0.1. Tempo, start and
	 *  stop travel through Link during the stream, the phase agrees, and the audio arrives intact. */
	void pcmStreamsWhileLinkSyncIsActive()
	{
		if (!AbletonLinkSession::compiledIn()) { QSKIP("this build has no Ableton Link library"); }
		if (!AbletonLinkSession::loopbackOnlySupported()) { QSKIP("the loopback discovery seam is POSIX only (unverified on Windows)"); }
		AbletonLinkSession::setLoopbackOnly(true);   // BEFORE any session exists or is enabled
		struct Restore { ~Restore() { AbletonLinkSession::setLoopbackOnly(false); } } restore;
		LinkScenario link;
		LinkAudioStream rx, tx;
		std::string why;
		QVERIFY2(rx.configure(receiver(10), &why), why.c_str());
		QVERIFY2(tx.configure(senderTo(rx.report().boundPort), &why), why.c_str());

		constexpr std::size_t Period = 64;
		std::vector<SampleFrame> out(Period), mix(Period);
		std::vector<float> heard;
		std::uint64_t produced = 0;
		for (int i = 0; i < 6000 && !link.done(); ++i)
		{
			fillRamp(out, produced + 1);
			produced += Period;
			tx.process(out.data(), Period, Rate);
			std::fill(mix.begin(), mix.end(), SampleFrame(0.f, 0.f));
			rx.process(mix.data(), Period, Rate);
			for (const SampleFrame& f : mix) { heard.push_back(f.left()); }
			std::this_thread::sleep_for(std::chrono::microseconds(1300));
			QCoreApplication::processEvents();
			QVERIFY2(link.advance(i), qPrintable(link.describeStall(i)));
		}
		QVERIFY(link.done());
		if (const std::optional<bool> confined = link.confinedToLoopback())
		{
			QVERIFY2(*confined, "Link joined its multicast group on an interface other than lo");
		}
		else
		{
			qInfo("multicast membership is not inspectable on this platform: confinement check skipped");
		}
		QVERIFY2(std::fabs(link.phaseError()) < 0.01, qPrintable(QString::number(link.phaseError())));
		QVERIFY(link.temposAgree());
		QVERIFY2(isOrdered(heard, 5000), "audio was lost or out of order while Link was active");
		const AudioStreamReport r = rx.report();
		QVERIFY(r.packetsReceived > 50);
		QCOMPARE(r.malformedPackets, std::uint64_t{0});
	}

	void commandsAreRegisteredAndUndoable()
	{
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("link.audio_get_state")));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("link.audio_set_enabled")));
		QJsonObject s = run(QStringLiteral("link.audio_get_state")).result;
		QVERIFY(!s.value(QStringLiteral("enabled")).toBool());
		QCOMPARE(s.value(QStringLiteral("bind_address")).toString(), QStringLiteral("127.0.0.1"));

		const ControlResult on = run(QStringLiteral("link.audio_set_enabled"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("listen_port"), 0}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		s = on.result;
		QVERIFY(s.value(QStringLiteral("running")).toBool());
		QVERIFY(s.value(QStringLiteral("bound_port")).toInt() > 0);
		QVERIFY(s.value(QStringLiteral("receive")).toBool());
		QVERIFY(!s.value(QStringLiteral("send")).toBool());

		const ControlResult refused = run(QStringLiteral("link.audio_set_enabled"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("peer"), QStringLiteral("example.com")}});
		QVERIFY(!refused.ok);
		QVERIFY(run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());  // untouched

		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QVERIFY(!run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());
		run(QStringLiteral("control.redo"));
		QVERIFY(run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());
		QVERIFY(run(QStringLiteral("link.audio_set_enabled"), {{QStringLiteral("enabled"), false}}).ok);
		QVERIFY(!run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());
	}

	void theStreamLeavesLinkSyncAlone()
	{
		// Link stays OFF here (it would multicast); the claim is that the two surfaces share no
		// state: link.* answers identically with the stream running, and link's own setters work.
		const QJsonObject before = run(QStringLiteral("link.get_state")).result;
		const ControlResult started = run(QStringLiteral("link.audio_set_enabled"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("listen_port"), 0}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const QJsonObject during = run(QStringLiteral("link.get_state")).result;
		for (const char* key : {"enabled", "model", "quantum", "start_stop_sync", "session_tempo", "peer_count"})
		{
			QCOMPARE(during.value(QLatin1String(key)), before.value(QLatin1String(key)));
		}
		QVERIFY(run(QStringLiteral("link.set_quantum"), {{QStringLiteral("quantum"), 8}}).ok);
		QCOMPARE(run(QStringLiteral("link.get_state")).result.value(QStringLiteral("quantum")).toInt(), 8);
		QVERIFY(run(QStringLiteral("link.audio_get_state")).result.value(QStringLiteral("running")).toBool());
		run(QStringLiteral("link.set_quantum"), {{QStringLiteral("quantum"), before.value(QStringLiteral("quantum")).toInt()}});
		run(QStringLiteral("link.audio_set_enabled"), {{QStringLiteral("enabled"), false}});
	}
};

QTEST_GUILESS_MAIN(LinkAudioStreamTest)
#include "LinkAudioStreamTest.moc"
