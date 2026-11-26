/*
 * AudioSampleRecorderTest.cpp - the take's frame total decides the destination, and the copy
 *                             lays the blocks out from it (cluster new-p10-505)
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

/*! THE BUG (cluster new-p10-505): createSampleBuffer() sized its destination from an unchecked
 *  sum of the captured blocks and then walked it with `auto framesCopied = 0;` - a signed `int`
 *  offset - so a take past INT_MAX frames (about 12.4 hours at 48 kHz) indexed the destination
 *  with an overflowed offset. framesRecorded() added those same counts unchecked.
 *
 *  This test is written against the recorder's OWN members, not against any helper the fix
 *  introduced, so the SAME SOURCE builds and runs on the pre-fix product and on the fixed one:
 *  reverting the product hunks makes two of these slots fail with their own assertions instead
 *  of failing to build.
 *   - the total the destination cannot hold: the pre-fix product throws std::length_error out of
 *     `std::vector<SampleFrame>(frames)` (an uncaught exception inside a DAW's stop path); the
 *     fixed product refuses the take with nullptr before it allocates anything;
 *   - framesRecorded() past what f_cnt_t can name: the pre-fix product wraps to a SMALLER count;
 *     the fixed product saturates;
 *   - the same copy loop at a size a test can afford: two blocks land at the offsets the total
 *     gives them, frame for frame, which is the behaviour the overflow destroys.
 *
 *  The copy-offset overflow itself needs a destination of more than INT_MAX frames - 2^31
 *  SampleFrames of 8 bytes, 17.2 GiB - so no test can execute that copy without allocating it;
 *  what is pinned here is the arithmetic that computes it (the total and the offsets) at every
 *  size a test can hold.
 */

#include <QtTest>

#include <QList>
#include <QPair>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"

/* White-box, and deliberately so: the recorder keeps its captured blocks in a private list and
 * the class is abstract (it leaves AudioDevice's two pure virtuals to a backend), so a test that
 * drives the REAL createSampleBuffer() has to open the access specifier. Both the pre-fix and the
 * fixed header declare the same members under `private:`, so this one source compiles against
 * both. The redefinition is undone immediately and every header the recorder needs is included
 * above it, so it reaches nothing but that class body. */
#define private public
#include "AudioSampleRecorder.h"
#undef private

using namespace lmms;

namespace
{

//! A recorder a test can construct: the two pure virtuals AudioSampleRecorder leaves to a
//! backend, and no processing.
class TestRecorder : public AudioSampleRecorder
{
public:
	explicit TestRecorder( AudioEngine* engine ) :
		AudioSampleRecorder( DEFAULT_CHANNELS, s_opened, engine )
	{
	}

	//! A block the recorder never captured - the count it claims is the test's, and the
	//! destructor still frees it with delete[] like every real block.
	void captureClaiming( const f_cnt_t frames )
	{
		m_buffers.push_back( qMakePair( new SampleFrame[1], frames ) );
	}

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}

	static bool s_opened;
};

bool TestRecorder::s_opened = false;

} // namespace


class AudioSampleRecorderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init( true );
		QVERIFY( Engine::audioEngine() != nullptr );
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void aTotalTheDestinationCannotHoldIsRefusedWithoutThrowing();
	void framesRecordedSaturatesInsteadOfWrapping();
	void aTakeThatFitsIsCopiedAtTheRightOffsets();
};


//! A take whose total no destination vector can hold: no throw, no allocation, nullptr.
//! Pre-fix: std::vector<SampleFrame>(frames) throws std::length_error out of this call.
void AudioSampleRecorderTest::aTotalTheDestinationCannotHoldIsRefusedWithoutThrowing()
{
	TestRecorder recorder( Engine::audioEngine() );
	recorder.captureClaiming( std::numeric_limits<f_cnt_t>::max() );

	std::shared_ptr<const SampleBuffer> take;
	try
	{
		take = recorder.createSampleBuffer();
	}
	catch( const std::exception& error )
	{
		QFAIL( qPrintable( QStringLiteral( "createSampleBuffer() threw: %1" )
			.arg( QString::fromUtf8( error.what() ) ) ) );
	}

	QVERIFY2( take == nullptr, "a total the destination cannot hold must be refused, not attempted" );
}


//! The sum past what f_cnt_t can name: reported as the largest nameable count, not wrapped.
//! Pre-fix: the second UINT64_MAX block wraps the running total down to UINT64_MAX - 1.
void AudioSampleRecorderTest::framesRecordedSaturatesInsteadOfWrapping()
{
	TestRecorder recorder( Engine::audioEngine() );
	const f_cnt_t largest = std::numeric_limits<f_cnt_t>::max();
	recorder.captureClaiming( largest );
	recorder.captureClaiming( largest );

	QCOMPARE( recorder.framesRecorded(), largest );
}


//! The copy at a size a test can hold: the second block starts exactly one destination offset
//! past the first, and no frame is dropped, duplicated or reordered.
void AudioSampleRecorderTest::aTakeThatFitsIsCopiedAtTheRightOffsets()
{
	TestRecorder recorder( Engine::audioEngine() );

	// Block 1: two frames, left = 1.0, 1.5. Block 2: three frames, left = 2.0, 2.5, 3.0.
	SampleFrame first[2] = { SampleFrame( 1.0f, -1.0f ), SampleFrame( 1.5f, -1.5f ) };
	SampleFrame second[3] = { SampleFrame( 2.0f, -2.0f ), SampleFrame( 2.5f, -2.5f ),
		SampleFrame( 3.0f, -3.0f ) };
	recorder.writeBuffer( first, 2 );
	recorder.writeBuffer( second, 3 );
	QCOMPARE( recorder.framesRecorded(), f_cnt_t( 5 ) );

	const std::shared_ptr<const SampleBuffer> take = recorder.createSampleBuffer();
	QVERIFY( take != nullptr );
	QCOMPARE( take->size(), std::size_t( 5 ) );

	const SampleFrame* frames = take->data();
	QCOMPARE( frames[0].left(), 1.0f );
	QCOMPARE( frames[1].left(), 1.5f );
	// Offset 2 - where the int copy cursor placed block 2.
	QCOMPARE( frames[2].left(), 2.0f );
	QCOMPARE( frames[3].left(), 2.5f );
	QCOMPARE( frames[4].left(), 3.0f );
	QCOMPARE( frames[4].right(), -3.0f );
}


QTEST_GUILESS_MAIN(AudioSampleRecorderTest)
#include "AudioSampleRecorderTest.moc"
