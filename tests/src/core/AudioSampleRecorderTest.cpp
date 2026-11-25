/*
 * AudioSampleRecorderTest.cpp - the wide, checked frame total createSampleBuffer() sizes its
 *                             destination with (cluster new-p10-505)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth
 * Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <QtTest>

#include <cstddef>
#include <limits>
#include <vector>

#include "AudioSampleRecorder.h"

using namespace lmms;

namespace
{

//! The recorded blocks' frame counts, in the order the recorder holds them.
using FrameCounts = std::vector<f_cnt_t>;

//! A value no accepted total may report: it proves a refusal did not write the out parameter.
constexpr std::size_t Untouched = 12345;

} // namespace


class AudioSampleRecorderTest : public QObject
{
	Q_OBJECT

private slots:
	void aTotalPastTheSignedCopyCursorIsAcceptedWhenTheDestinationCanHoldIt();
	void oneFramePastThatBoundaryIntoAShortDestinationIsRejected();
	void anExactFitIsAcceptedAndTheBlocksStayInOrder();
	void aCountPastTheDestinationsRoomIsRejectedWithoutWrapping();
	void noBlocksIsAnEmptyTotal();
};


//! The sum an `int` copy cursor wrapped at: INT_MAX frames of a first block, then one more.
//! The destination can hold them, so the take is accepted with the exact wide total.
void AudioSampleRecorderTest::aTotalPastTheSignedCopyCursorIsAcceptedWhenTheDestinationCanHoldIt()
{
	const FrameCounts counts{ static_cast<f_cnt_t>( std::numeric_limits<int>::max() ), 1 };
	const std::size_t capacity = static_cast<std::size_t>( std::numeric_limits<int>::max() ) + 1;
	std::size_t total = Untouched;

	QVERIFY( AudioSampleRecorder::totalFramesWithin( counts.data(), counts.size(), capacity, total ) );
	QCOMPARE( total, capacity );
	QVERIFY( total > static_cast<std::size_t>( std::numeric_limits<int>::max() ) );
}


//! The same two blocks into a destination that holds four frames: refused, so the caller
//! allocates nothing with a total the destination cannot represent.
void AudioSampleRecorderTest::oneFramePastThatBoundaryIntoAShortDestinationIsRejected()
{
	const FrameCounts counts{ static_cast<f_cnt_t>( std::numeric_limits<int>::max() ), 1 };
	std::size_t total = Untouched;

	QVERIFY( !AudioSampleRecorder::totalFramesWithin( counts.data(), counts.size(), 4, total ) );
	QCOMPARE( total, Untouched );
}


//! {2, 3} into 5 frames: accepted, and the blocks stay in order - the second block's own
//! total is the room the first one leaves behind.
void AudioSampleRecorderTest::anExactFitIsAcceptedAndTheBlocksStayInOrder()
{
	const FrameCounts counts{ 2, 3 };
	std::size_t total = Untouched;

	QVERIFY( AudioSampleRecorder::totalFramesWithin( counts.data(), counts.size(), 5, total ) );
	QCOMPARE( total, std::size_t( 5 ) );

	// The offset the copy hands the second block is the first block's frames; the room it
	// leaves in the destination is that block's capacity.
	std::size_t second = Untouched;
	QVERIFY( AudioSampleRecorder::totalFramesWithin( counts.data() + 1, 1, 5 - 2, second ) );
	QCOMPARE( second, std::size_t( 3 ) );
}


//! A count that alone would fill a size_t destination, plus one more frame: the checked sum
//! must refuse it rather than wrap to 0, which would size an empty destination and copy into it.
void AudioSampleRecorderTest::aCountPastTheDestinationsRoomIsRejectedWithoutWrapping()
{
	const FrameCounts counts{ std::numeric_limits<std::size_t>::max(), 1 };
	std::size_t total = Untouched;

	QVERIFY( !AudioSampleRecorder::totalFramesWithin( counts.data(), counts.size(),
		std::numeric_limits<std::size_t>::max(), total ) );
	QCOMPARE( total, Untouched );
}


//! No captured block is no frames, not an unset total.
void AudioSampleRecorderTest::noBlocksIsAnEmptyTotal()
{
	std::size_t total = Untouched;

	QVERIFY( AudioSampleRecorder::totalFramesWithin( nullptr, 0, 0, total ) );
	QCOMPARE( total, std::size_t( 0 ) );
}


QTEST_GUILESS_MAIN(AudioSampleRecorderTest)
#include "AudioSampleRecorderTest.moc"
