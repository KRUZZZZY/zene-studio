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

// Complete fixture method definitions; included once after the fixture declaration.

void LinkAudioStreamTest::initTestCase() {
	Engine::init(true);
	ControlRegistry::setReady(true);
}
void LinkAudioStreamTest::cleanupTestCase() {
	std::string e;
	const bool stopped = LinkAudioStream::instance()->configure(AudioStreamConfig{}, &e);
	// The fixture owns the render threads started in initTestCase. Join them before
	// process-lifetime worker containers and plugin libraries are destroyed.
	ControlRegistry::setReady(false);
	Engine::destroy();
	QVERIFY2(stopped, e.c_str());
	QVERIFY2(Engine::audioEngine() == nullptr, "fixture must stop the audio engine before static destruction");
}
void LinkAudioStreamTest::packetCodecRoundTripsAndRejectsGarbage() {
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
void LinkAudioStreamTest::nonFiniteSamplesInAValidPacketAreSilencedNotMixed() {
	// x + NaN and x + inf are not finite: a non-finite sample of a valid packet must not reach the mix.
	std::vector<float> in(AudioMaxPacketFrames * 2, 0.5f);
	in[0] = NAN;
	in[3] = INFINITY;
	in[10] = -INFINITY;
	AudioPacketHeader h{7, 48000, static_cast<std::uint16_t>(AudioMaxPacketFrames)};
	std::vector<std::uint8_t> wire(AudioMaxPacketBytes);
	const std::size_t bytes = encodeAudioPacket(h, in.data(), wire.data(), wire.size());

	std::vector<float> samples(in.size(), 9.f);
	QVERIFY(decodeAudioPacket(wire.data(), bytes, h, samples.data(), samples.size()));
	QVERIFY(std::all_of(samples.begin(), samples.end(), [](float s) { return std::isfinite(s); }));
	QVERIFY(samples[0] == 0.f && samples[3] == 0.f && samples[10] == 0.f && samples[1] == 0.5f && samples[11] == 0.5f);  // non-finite -> silence, neighbours kept
}
void LinkAudioStreamTest::ipv4ParserAcceptsLiteralsOnly() {
	std::uint32_t a = 0;
	QVERIFY(parseIpv4("127.0.0.1", a));
	QCOMPARE(a, 0x7F000001u);
	for (const char* bad : {"localhost", "example.com", "1.2.3", "1.2.3.4.5", "256.0.0.1", "1..2.3", "", "1.2.3.4 ", "::1"})
	{
		QVERIFY2(!parseIpv4(bad, a), bad);
	}
}
void LinkAudioStreamTest::ringKeepsOrderAcrossWrapAndRefusesOverflow() {
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
void LinkAudioStreamTest::refusalsChangeNothing() {
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
void LinkAudioStreamTest::largePeriodsKeepTheJitterTargetInsideItsSupportedRange() {
	constexpr std::size_t Period = 9000;
	QVERIFY2(LinkAudioStreamTestAccess::jitterBoundsAreOrdered(Period),
		"the jitter target's lower bound must not exceed its ceiling (std::clamp needs lo <= hi)");
	QCOMPARE(LinkAudioStreamTestAccess::jitterMinimum(Period, AudioRingFrames), AudioRingFrames / 4);
	QCOMPARE(LinkAudioStreamTestAccess::jitterMaximum(Period, AudioRingFrames), AudioRingFrames / 4);

	constexpr std::size_t NormalPeriod = 512;
	QVERIFY(LinkAudioStreamTestAccess::jitterBoundsAreOrdered(NormalPeriod));
	QCOMPARE(LinkAudioStreamTestAccess::jitterMinimum(NormalPeriod, AudioRingFrames), NormalPeriod * 2);
	QCOMPARE(LinkAudioStreamTestAccess::jitterMaximum(NormalPeriod, AudioRingFrames), AudioRingFrames / 4);
	QVERIFY(LinkAudioStreamTestAccess::jitterBoundsAreOrdered(SIZE_MAX / 2 + 1));

	LinkAudioStream stream;
	LinkAudioStreamTestAccess::enableReceiver(stream, 5);
	std::vector<SampleFrame> warmup(1, SampleFrame(0.f, 0.f));
	stream.process(warmup.data(), warmup.size(), Rate);
	std::vector<float> incoming(17000 * AudioChannels, 1.f);
	QCOMPARE(LinkAudioStreamTestAccess::writeRx(stream, incoming.data(), 17000), std::size_t{17000});
	std::vector<SampleFrame> output(Period, SampleFrame(0.f, 0.f));
	stream.process(output.data(), output.size(), Rate);
	QCOMPARE(stream.report().primed, true);
	QCOMPARE(output.front().left(), 1.f);
	QCOMPARE(stream.report().underruns, std::uint64_t{0});
	QCOMPARE(stream.report().trims, std::uint64_t{0});
}
void LinkAudioStreamTest::loopbackExchangeIsSampleExact() {
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
void LinkAudioStreamTest::audioThreadAllocatesNothing() {
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
void LinkAudioStreamTest::realtimeStressReportsBoundedUnderrunsAndLatency() {
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
void LinkAudioStreamTest::disablingWhileFloodedJoinsPromptly() {
	LinkAudioStream rx, tx;
	std::string why;
	QVERIFY2(rx.configure(receiver(10), &why), why.c_str());
	QVERIFY2(tx.configure(senderTo(rx.report().boundPort), &why), why.c_str());
	std::vector<SampleFrame> first(128);
	rx.process(first.data(), 128, Rate);   // the receiver learns the engine rate (packets at another rate are dropped)
	std::atomic<bool> flood{true};
	std::thread sender([&] {
		std::vector<SampleFrame> buf(128);
		fillRamp(buf, 1);
		while (flood.load()) { tx.process(buf.data(), 128, Rate); }
	});
	QElapsedTimer clock;
	while (rx.report().packetsReceived <= 100 && clock.elapsed() < 5000) { QTest::qWait(5); }
	const std::uint64_t received = rx.report().packetsReceived;
	clock.restart();
	const bool disabled = rx.configure(AudioStreamConfig{}, &why);   // disable: joins the socket thread
	const qint64 joinMs = clock.elapsed();
	flood = false;
	sender.join();
	QVERIFY2(received > 100, "the flood never arrived");
	QVERIFY2(disabled, why.c_str());
	QVERIFY2(joinMs < 1000, qPrintable(QStringLiteral("join took %1 ms under flood").arg(joinMs)));
	QVERIFY(!rx.report().running);
}
void LinkAudioStreamTest::packetsFromAStrangerOrAtAnotherRateAreCounted() {
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
void LinkAudioStreamTest::droppedGapSilenceIsCountedAsAnOverrun() {
	LinkAudioStream rx;
	std::string why;
	QVERIFY2(rx.configure(receiver(20), &why), why.c_str());
	const quint16 port = static_cast<quint16>(rx.report().boundPort);
	QVERIFY(port > 0);

	std::vector<SampleFrame> mix(64, SampleFrame(0.f, 0.f));
	rx.process(mix.data(), static_cast<f_cnt_t>(mix.size()), Rate);   // the receiver learns the engine rate
	QCOMPARE(rx.report().packetsReceived, std::uint64_t{0});

	QUdpSocket sender;
	// Fill the ring exactly: the first packet, then 56 packets that each skip 8 sequences
	// (8 * 128 frames of silence plus 128 frames of payload), then one that skips 7 - its
	// silence takes exactly the rest of the ring and its payload does not fit at all.
	QVERIFY(sendAudioPacket(sender, port, 0, Rate, AudioMaxPacketFrames, 0.25f));
	QVERIFY(waitForPackets(rx, 1, 5000));
	for (std::uint32_t k = 1; k <= 56; ++k)
	{
		QVERIFY(sendAudioPacket(sender, port, 9 * k, Rate, AudioMaxPacketFrames, 0.25f));
		QVERIFY(waitForPackets(rx, k + 1, 5000));
	}
	QVERIFY(sendAudioPacket(sender, port, 512, Rate, AudioMaxPacketFrames, 0.25f));
	QVERIFY(waitForPackets(rx, 58, 5000));
	const std::uint64_t beforeGap = rx.report().rxOverruns;
	QCOMPARE(beforeGap, std::uint64_t{128});   // only the payload that did not fit

	// The next packet skips 8 sequences: not one of the 1024 silence frames fits either.
	QVERIFY(sendAudioPacket(sender, port, 521, Rate, AudioMaxPacketFrames, 0.25f));
	QVERIFY(waitForPackets(rx, 59, 5000));
	QCOMPARE(rx.report().rxOverruns - beforeGap, std::uint64_t{1024 + 128});
}
void LinkAudioStreamTest::theFrameArrayIsNotReadAsAFlatFloatArray() {
	QFile source(QStringLiteral(ZENE_SOURCE_DIR "/src/core/LinkAudioStream.cpp"));
	QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.fileName()));
	const QString text = QString::fromUtf8(source.readAll());
	QVERIFY2(!text.contains(QRegularExpression("reinterpret_cast\\s*<\\s*(const\\s+)?float\\s*\\*")),
		"the stream must not read a SampleFrame array through a float*");
	QVERIFY2(!text.contains("sizeof(SampleFrame)"),
		"the size of a frame does not make an array of frames a float array");
}
void LinkAudioStreamTest::aSampleRateChangeDropsTheOldRateRxFrames() { checkRateChangeDropsTheOldRateRxFrames(); }
void LinkAudioStreamTest::aSampleRateChangeDoesNotSendOldRateFramesAsNewRate() { checkRateChangeDoesNotSendOldRateFramesAsNewRate(); }
void LinkAudioStreamTest::anOldRatePacketIsNotPlayedAtTheNewRate() { checkOldRatePacketIsNotPlayedAtTheNewRate(); }
void LinkAudioStreamTest::pcmStreamsWhileLinkSyncIsActive() {
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
void LinkAudioStreamTest::commandsAreRegisteredAndUndoable() {
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
