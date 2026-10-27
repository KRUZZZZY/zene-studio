/*
 * LinkAudioCodecTest.cpp - the datagram codec of the Link audio stream on its own (Q050-024,
 *                          src/core/LinkAudioCodec.cpp, include/LinkAudioStream.h).
 *
 * The codec is pure: no socket, no engine, no clock. The stream built on it is covered by
 * LinkAudioStreamTest.
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

#include <algorithm>
#include <cstdint>
#include <vector>

#include "LinkAudioStream.h"

using namespace lmms::link;

class LinkAudioCodecTest : public QObject
{
	Q_OBJECT

private slots:
	// A zero rate means "not known yet": the sender waits for it and the receiver refuses it, so a packet
	// that carries it is not well formed. Neither side may produce or accept one.
	void zeroSampleRateIsNotEncoded()
	{
		std::vector<float> in(AudioMaxPacketFrames * AudioChannels, 0.25f);
		AudioPacketHeader h{3, 48000, static_cast<std::uint16_t>(AudioMaxPacketFrames)};
		std::vector<std::uint8_t> wire(AudioMaxPacketBytes);
		QVERIFY(encodeAudioPacket(h, in.data(), wire.data(), wire.size()) > 0);   // control: a real rate encodes
		h.sampleRate = 0;
		QCOMPARE(encodeAudioPacket(h, in.data(), wire.data(), wire.size()), std::size_t{0});
	}

	void zeroSampleRateIsNotDecoded()
	{
		std::vector<float> in(AudioMaxPacketFrames * AudioChannels, 0.25f);
		AudioPacketHeader h{3, 48000, static_cast<std::uint16_t>(AudioMaxPacketFrames)};
		std::vector<std::uint8_t> wire(AudioMaxPacketBytes);
		const std::size_t bytes = encodeAudioPacket(h, in.data(), wire.data(), wire.size());
		QVERIFY(bytes > 0);
		std::vector<float> samples(in.size());
		AudioPacketHeader out;
		QVERIFY(decodeAudioPacket(wire.data(), bytes, out, samples.data(), samples.size()));   // control: valid
		// The same packet with the rate field (bytes 8..11, little endian) zeroed: magic, length and frames are intact.
		std::fill(wire.begin() + 8, wire.begin() + 12, std::uint8_t{0});
		QVERIFY(!decodeAudioPacket(wire.data(), bytes, out, samples.data(), samples.size()));
	}
};

QTEST_GUILESS_MAIN(LinkAudioCodecTest)
#include "LinkAudioCodecTest.moc"
