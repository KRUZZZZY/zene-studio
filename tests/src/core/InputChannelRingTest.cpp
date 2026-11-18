/*
 * InputChannelRingTest.cpp - construction invariants of the interleaved input
 *                            ring (include/InputChannelRing.h)
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

#include "InputChannelRing.h"

#include <QtTest>

#include <limits>
#include <stdexcept>

using lmms::InputChannelRing;

class InputChannelRingTest : public QObject
{
	Q_OBJECT

private slots:
	void roundsCapacityUpToAPowerOfTwo()
	{
		const InputChannelRing small(1000, 2);
		QVERIFY(small.capacity() >= 1000);
		QVERIFY((small.capacity() & (small.capacity() - 1)) == 0);
		const InputChannelRing exact(1u << 16, 2);
		QCOMPARE(exact.capacity(), std::size_t(1u << 16));
	}

	// Past the largest power of two a size_t holds, `result <<= 1` wrapped to
	// zero and the constructor never returned. SampleFrameRingBuffer::nextPowerOfTwo
	// already refuses such a value; this ring must refuse it too.
	void refusesACapacityThatCannotBeRoundedUp()
	{
		const auto beyondLargestPowerOfTwo = (std::numeric_limits<std::size_t>::max() >> 1) + 2;
		QVERIFY_EXCEPTION_THROWN({ InputChannelRing ring(beyondLargestPowerOfTwo, 2); },
			std::length_error);
	}

	// capacity * maxChannels wrapped, so storage was allocated too small and
	// every write went out of bounds.
	void refusesAStorageSizeThatOverflows()
	{
		const auto hugeChannels = 1 << 20;
		const auto framesSoStorageOverflows = std::numeric_limits<std::size_t>::max() / hugeChannels + 2;
		QVERIFY_EXCEPTION_THROWN({ InputChannelRing ring(framesSoStorageOverflows, 2, hugeChannels); },
			std::length_error);
	}
};

QTEST_GUILESS_MAIN(InputChannelRingTest)
#include "InputChannelRingTest.moc"
