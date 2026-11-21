/*
 * SidechainInputsTest.cpp - a hosted plug-in.s aux bus hears the mixer sidechain
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

#include <QtTest>

#include <array>

#include "AudioBuffer.h"
#include "SidechainInputs.h"

using namespace lmms;

//! Engine audit E9: sidechainInputs() points the aux-bus channels at the sidechain signal and
//! leaves the main channels alone; with no sidechain, or no aux bus, the input is passed through.
class SidechainInputsTest : public QObject
{
	Q_OBJECT

private slots:
	void theAuxBusHearsTheSidechain()
	{
		std::array<float, 4> a{}, b{}, c{}, d{};
		const float* in[] = {a.data(), b.data(), c.data(), d.data()};
		AudioBuffer sidechain(4, 2);
		std::array<const float*, MaxHostedInputChannels> scratch{};
		const float* const* fed = sidechainInputs(in, 4, &sidechain, 2, scratch);
		QCOMPARE(fed[0], in[0]);
		QCOMPARE(fed[1], in[1]);
		QCOMPARE(fed[2], sidechain.group(0).buffer(0));
		QCOMPARE(fed[3], sidechain.group(0).buffer(1));
	}

	void withoutASidechainTheInputPassesThrough()
	{
		std::array<float, 4> a{}, b{};
		const float* in[] = {a.data(), b.data()};
		std::array<const float*, MaxHostedInputChannels> scratch{};
		QCOMPARE(sidechainInputs(in, 2, nullptr, 0, scratch), static_cast<const float* const*>(in));
		AudioBuffer sidechain(4, 2);
		QCOMPARE(sidechainInputs(in, 2, &sidechain, -1, scratch), static_cast<const float* const*>(in));
	}

	//! A sidechain AudioBuffer move-assigned across memory resources must keep
	//! its group views inside its own storage, so sidechainInputs fed from it
	//! reads the live signal and not the moved-from table. Cluster new-p9-557.
	void movedSidechainKeepsGroupViews()
	{
		constexpr f_cnt_t frames = 16;
		alignas(16) std::array<std::byte, 8192> srcStorage{};
		alignas(16) std::array<std::byte, 8192> dstStorage{};
		std::pmr::monotonic_buffer_resource srcRes{
			srcStorage.data(), srcStorage.size(), std::pmr::null_memory_resource()};
		std::pmr::monotonic_buffer_resource dstRes{
			dstStorage.data(), dstStorage.size(), std::pmr::null_memory_resource()};

		auto groupVisitor = [](group_cnt_t idx, AudioBuffer::ChannelGroup&) {
			switch (idx)
			{
				case 0: return 2; // 1st group has 2 channels
				case 1: return 4; // 2nd group has 4 channels
				default: return 0;
			}
		};

		auto src = AudioBuffer{frames, 6, 2, &srcRes, groupVisitor};
		for (ch_cnt_t ch = 0; ch < src.totalChannels(); ++ch)
		{
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				src.buffer(ch)[f] = static_cast<float>(ch * 1000 + f);
			}
		}
		const auto srcAccessBefore = static_cast<const void*>(src.allBuffers().data());
		const auto srcCh0Before = static_cast<const void*>(src.buffer(0).data());

		auto dst = AudioBuffer{frames, 2, &dstRes};
		dst = std::move(src);

		QCOMPARE(dst.groupCount(), 2);
		QCOMPARE(dst.totalChannels(), 6);
		QCOMPARE(static_cast<const void*>(dst.group(0).buffers()),
			static_cast<const void*>(dst.allBuffers().data()));
		QVERIFY2(static_cast<const void*>(dst.group(0).buffers()) != srcAccessBefore,
			"moved sidechain group still points into the source access table");
		QVERIFY2(static_cast<const void*>(dst.buffer(0).data()) != srcCh0Before,
			"moved sidechain channel still points into the source storage");

		std::array<float, 4> a{};
		std::array<float, 4> b{};
		std::array<float, 4> c{};
		std::array<float, 4> d{};
		const float* in[] = {a.data(), b.data(), c.data(), d.data()};
		std::array<const float*, MaxHostedInputChannels> scratch{};
		const float* const* fed = sidechainInputs(in, 4, &dst, 2, scratch);
		QCOMPARE(fed[0], in[0]);
		QCOMPARE(fed[1], in[1]);
		QCOMPARE(static_cast<const void*>(fed[2]),
			static_cast<const void*>(dst.group(0).buffer(0)));
		QCOMPARE(fed[2][0], 0.f);
		dst.group(0).buffer(0)[0] = 123.f;
		QCOMPARE(fed[2][0], 123.f);
	}
};

QTEST_GUILESS_MAIN(SidechainInputsTest)
#include "SidechainInputsTest.moc"
