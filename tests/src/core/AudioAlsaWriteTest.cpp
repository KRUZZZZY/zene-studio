/*
 * AudioAlsaWriteTest.cpp - ALSA playback period write behaviour
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

#include <QtTest>

#include <array>
#include <cstddef>

#include "AudioAlsaWrite.h"

class AudioAlsaWriteTest : public QObject
{
	Q_OBJECT

private slots:
	void completesPeriodAfterShortWrites()
	{
		constexpr std::size_t channels = 2;
		const std::array<float, 10> period{};
		const std::array<std::ptrdiff_t, 3> shortWrites{2, 1, 2};
		std::array<const float*, 3> addresses{};
		std::array<std::size_t, 3> requests{};
		std::size_t calls = 0;

		const auto framesWritten = lmms::alsa::detail::writeAllFrames(period.data(), std::size_t{5}, channels,
			[&](const float* data, std::size_t remaining) -> std::ptrdiff_t
			{
				addresses[calls] = data;
				requests[calls] = remaining;
				return shortWrites[calls++];
			});

		QCOMPARE(framesWritten, std::ptrdiff_t{5});
		QCOMPARE(calls, std::size_t{3});
		QCOMPARE(addresses[0], period.data());
		QCOMPARE(addresses[1], period.data() + 4);
		QCOMPARE(addresses[2], period.data() + 6);
		QCOMPARE(requests[0], std::size_t{5});
		QCOMPARE(requests[1], std::size_t{3});
		QCOMPARE(requests[2], std::size_t{2});
	}
};

QTEST_APPLESS_MAIN(AudioAlsaWriteTest)

#include "AudioAlsaWriteTest.moc"
