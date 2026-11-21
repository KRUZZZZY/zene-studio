/*
 * BiquadSectionTest.cpp - channel indexing of the shared biquad section (new-p16-306).
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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

#include <cstring>

#include "BiquadDesign.h"

using namespace lmms;

namespace {

// y = x + x1: the output shows the previous input of the same channel, so each channel's state reads back.
BiquadCoefficients delayAndAdd()
{
	return BiquadCoefficients{1.f, 1.f, 0.f, 0.f, 0.f};
}

// The section sits between two sentinels: a write before or after its arrays lands in one of them.
struct Guarded
{
	float before = 7.f;
	BiquadSection<2> section;
	float after = 7.f;
};

} // namespace

class BiquadSectionTest : public QObject
{
	Q_OBJECT

private slots:
	void validChannelsKeepSeparateState();
	void outOfRangeChannelPassesInputThroughAndTouchesNothing();
};

void BiquadSectionTest::validChannelsKeepSeparateState()
{
	BiquadSection<2> section;
	section.set(delayAndAdd());

	// Channel 0 gets its impulse first and channel 1 a step later: each keeps its own delay line.
	QCOMPARE(section.run(1.f, 0), 1.f);
	QCOMPARE(section.run(0.f, 1), 0.f);
	QCOMPARE(section.run(0.f, 0), 1.f);
	QCOMPARE(section.run(1.f, 1), 1.f);
	QCOMPARE(section.run(0.f, 0), 0.f);
	QCOMPARE(section.run(0.f, 1), 1.f);
}

void BiquadSectionTest::outOfRangeChannelPassesInputThroughAndTouchesNothing()
{
	Guarded g;
	g.section.set(delayAndAdd());
	QCOMPARE(g.section.run(1.f, 0), 1.f);  // channel 0 now holds x = 1 as its delayed sample
	const Guarded snapshot = g;

	// A volatile index keeps the compiler from folding the out-of-range access away.
	volatile int channel = 2;
	QCOMPARE(g.section.run(0.25f, channel), 0.25f);
	channel = -1;
	QCOMPARE(g.section.run(0.25f, channel), 0.25f);

	// Neither the sentinels nor the section's coefficients and channel state may have moved.
	QVERIFY2(std::memcmp(&g, &snapshot, sizeof(Guarded)) == 0,
		"an out-of-range channel wrote outside the section's channel state");

	// And channel 0 still delivers the sample it was holding.
	QCOMPARE(g.section.run(0.f, 0), 1.f);
}

QTEST_APPLESS_MAIN(BiquadSectionTest)

#include "BiquadSectionTest.moc"
