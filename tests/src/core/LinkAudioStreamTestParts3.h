/*
 * LinkAudioStreamTestParts3.h - the frame position of the stream's packets (part 3 of
 *                               LinkAudioStreamTest.cpp, included by it; not a header of its own).
 *
 * The wire bytes are built by hand here (literal offsets), so these tests pin the format itself:
 * header version 1 is 20 bytes (the 16 bytes of version 0 with byte 15 = 1, then the u32 frame
 * position), version 0 is the legacy 16-byte header.
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

#include <cstdint>
#include <cstring>

namespace
{

void putU32(std::vector<std::uint8_t>& wire, std::size_t at, std::uint32_t v)
{
	for (std::size_t i = 0; i < 4; ++i) { wire[at + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu); }
}

std::uint32_t getU32(const QByteArray& wire, int at)
{
	std::uint32_t v = 0;
	for (int i = 0; i < 4; ++i) { v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(wire[at + i])) << (8 * i); }
	return v;
}

//! One stereo packet, built byte by byte: magic "ZLA1", sequence, rate, frames, channels, \a version, then
//! (for a version other than 0) the frame position, then \a frames frames of \a value.
std::vector<std::uint8_t> handWire(std::uint8_t version, std::uint32_t sequence, std::uint16_t frames,
	std::uint32_t position, float value)
{
	const std::size_t headerBytes = version == 0 ? 16 : 20;
	std::vector<std::uint8_t> wire(headerBytes + static_cast<std::size_t>(frames) * 2 * 4, 0);
	wire[0] = 'Z';
	wire[1] = 'L';
	wire[2] = 'A';
	wire[3] = '1';
	putU32(wire, 4, sequence);
	putU32(wire, 8, 48000);
	wire[12] = static_cast<std::uint8_t>(frames & 0xffu);
	wire[13] = static_cast<std::uint8_t>(frames >> 8);
	wire[14] = 2;
	wire[15] = version;
	if (version != 0) { putU32(wire, 16, position); }
	for (std::size_t i = 0; i < static_cast<std::size_t>(frames) * 2; ++i)
	{
		std::memcpy(&wire[headerBytes + i * 4], &value, 4);   // little endian hosts only, like the codec's float copy
	}
	return wire;
}

//! A receiver with no thread and no socket whose RX rate is the engine's (what acceptPacket checks).
void primeRxRate(LinkAudioStream& rx)
{
	std::vector<SampleFrame> mix(64, SampleFrame(0.f, 0.f));
	rx.process(mix.data(), static_cast<f_cnt_t>(mix.size()), Rate);
}

} // namespace

void LinkAudioStreamTest::aLostShortPacketIsConcealedWithItsOwnDuration() {
	LinkAudioStream rx;
	primeRxRate(rx);
	// seq 1 (32 frames at position 128) is lost: it is never delivered.
	const bool first = LinkAudioStreamTestAccess::deliver(rx, handWire(1, 0, 128, 0, 0.25f));
	const bool third = LinkAudioStreamTestAccess::deliver(rx, handWire(1, 2, 128, 160, 0.5f));

	QCOMPARE(LinkAudioStreamTestAccess::rxFill(rx), std::size_t{128 + 32 + 128});
	std::vector<float> heard(288 * 2, -1.f);
	QCOMPARE(LinkAudioStreamTestAccess::readRx(rx, heard.data(), 288), std::size_t{288});
	std::vector<float> expected(288 * 2, 0.5f);
	std::fill(expected.begin(), expected.begin() + 128 * 2, 0.25f);
	std::fill(expected.begin() + 128 * 2, expected.begin() + 160 * 2, 0.f);
	QVERIFY2(heard == expected, "the audio after the gap is shifted or wrong");
	QCOMPARE(rx.report().lostPackets, std::uint64_t{1});
	QVERIFY(first);
	QVERIFY(third);
}
void LinkAudioStreamTest::aLegacyHeaderPacketStillPlaysAndKeepsTheOldGapEstimate() {
	LinkAudioStream rx;
	primeRxRate(rx);
	const bool first = LinkAudioStreamTestAccess::deliver(rx, handWire(0, 0, 128, 0, 0.25f));
	const bool third = LinkAudioStreamTestAccess::deliver(rx, handWire(0, 2, 128, 0, 0.5f));
	QVERIFY(first);
	QVERIFY(third);
	// No frame position on the wire: the lost packet is estimated as long as the one that arrived.
	QCOMPARE(LinkAudioStreamTestAccess::rxFill(rx), std::size_t{128 + 128 + 128});
	QCOMPARE(rx.report().lostPackets, std::uint64_t{1});
}
void LinkAudioStreamTest::aHeaderVersionThatIsNotKnownIsRefused() {
	AudioPacketHeader header;
	std::vector<float> out(AudioMaxPacketFrames * AudioChannels);

	// A well-formed version 1 packet decodes (the baseline of the two refusals below).
	const std::vector<std::uint8_t> good = handWire(1, 3, 8, 77, 0.5f);
	QVERIFY(decodeAudioPacket(good.data(), good.size(), header, out.data(), out.size()));
	QCOMPARE(header.sequence, std::uint32_t{3});
	QCOMPARE(header.frames, std::uint16_t{8});

	// A version nobody defined, with the size of a version 1 packet.
	const std::vector<std::uint8_t> unknown = handWire(2, 3, 8, 77, 0.5f);
	QVERIFY(!decodeAudioPacket(unknown.data(), unknown.size(), header, out.data(), out.size()));

	// Version 1 needs 20 header bytes: a packet that is 4 bytes short (16 + floats * 4) is malformed.
	std::vector<std::uint8_t> shortWire = handWire(1, 3, 8, 77, 0.5f);
	shortWire.resize(16 + 8 * 2 * 4);
	QVERIFY(!decodeAudioPacket(shortWire.data(), shortWire.size(), header, out.data(), out.size()));
}
void LinkAudioStreamTest::sentPacketsCarryTheirFramePosition() {
	QUdpSocket peer;
	QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost), 0));
	peer.setSocketOption(QUdpSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
	LinkAudioStream tx;
	std::string why;
	QVERIFY2(tx.configure(senderTo(peer.localPort()), &why), why.c_str());

	constexpr int periods = 40;
	constexpr int period = 64;
	std::vector<SampleFrame> buffer(period, SampleFrame(0.25f, 0.25f));
	for (int i = 0; i < periods; ++i) { tx.process(buffer.data(), static_cast<f_cnt_t>(period), Rate); }

	// Wait for a counter, not for a moment in time: the split into packets depends on the I/O thread.
	const std::uint64_t wanted = static_cast<std::uint64_t>(periods) * period;
	QElapsedTimer clock;
	clock.start();
	while (tx.report().framesSent < wanted && clock.elapsed() < 5000) { QTest::qWait(1); }
	QVERIFY2(tx.report().framesSent >= wanted, "the sender did not send the audio");

	std::uint64_t totalFrames = 0;
	std::uint32_t nextSequence = 0;
	int datagrams = 0;
	int badSize = 0, badVersion = 0, badPosition = 0, badSequence = 0;
	while (peer.hasPendingDatagrams())
	{
		QByteArray wire(static_cast<int>(peer.pendingDatagramSize()), '\0');
		peer.readDatagram(wire.data(), wire.size());
		++datagrams;
		const int frames = (wire.size() - 20) / 8;
		if (wire.size() != 20 + frames * 8 || frames < 1) { ++badSize; continue; }
		if (static_cast<std::uint8_t>(wire[15]) != 1) { ++badVersion; }
		if (getU32(wire, 16) != static_cast<std::uint32_t>(totalFrames)) { ++badPosition; }
		if (getU32(wire, 4) != nextSequence) { ++badSequence; }
		++nextSequence;
		totalFrames += static_cast<std::uint64_t>(frames);
	}
	QVERIFY2(datagrams > 0, "the peer received nothing");
	QCOMPARE(badSize, 0);
	QCOMPARE(badVersion, 0);
	QCOMPARE(badPosition, 0);
	QCOMPARE(badSequence, 0);
	QCOMPARE(totalFrames, wanted);
}
