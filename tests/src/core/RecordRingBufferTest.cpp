/*
 * RecordRingBufferTest.cpp - invariants of the lock-free SPSC ring buffer used
 *                            by the two-track recording prototype (task #556)
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

#include "RecordRingBuffer.h"

#include <QtTest>

#include <limits>
#include <stdexcept>
#include <cmath>
#include <thread>
#include <vector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "RingBuffer.h"
#include "SampleFrame.h"

using lmms::RecordRingBuffer;
using lmms::RingBuffer;
using lmms::SampleFrame;
using lmms::Engine;
using lmms::f_cnt_t;
using lmms::sample_t;

namespace
{

qulonglong asNumber(std::size_t value) { return static_cast<qulonglong>(value); }

class SampleRateRestore
{
public:
	SampleRateRestore(lmms::AudioDevice* device, lmms::sample_rate_t sampleRate) :
		m_device(device), m_sampleRate(sampleRate)
	{
	}

	~SampleRateRestore()
	{
		m_device->setSampleRateForTesting(m_sampleRate);
		lmms::Engine::audioEngine()->sampleRateChanged();
	}

private:
	lmms::AudioDevice* m_device;
	lmms::sample_rate_t m_sampleRate;
};

// Exact float values (multiples of 1/64) so comparisons are exact.
sample_t testValue(std::uint64_t index)
{
	return static_cast<sample_t>(static_cast<long long>(index % 61) - 30) / 64.0f;
}

template <typename Buffer, typename Offset>
void readAtFrameOffset(Buffer& buffer, lmms::SampleFrame* destination, Offset offset)
{
	if constexpr (requires { buffer.read(destination, offset); })
	{
		buffer.read(destination, offset);
	}
	else
	{
		buffer.read(destination, static_cast<lmms::f_cnt_t>(offset));
	}
}

template <typename Buffer, typename Offset>
void readFramesAtFrameOffset(Buffer& buffer, lmms::SampleFrame* destination, Offset offset,
	lmms::f_cnt_t length)
{
	if constexpr (requires { buffer.read(destination, offset, length); })
	{
		buffer.read(destination, offset, length);
	}
	else
	{
		buffer.read(destination, static_cast<lmms::f_cnt_t>(offset), length);
	}
}

template <typename Buffer, typename Offset>
void moveByFrameOffset(Buffer& buffer, Offset offset)
{
	if constexpr (requires { buffer.movePosition(offset); })
	{
		buffer.movePosition(offset);
	}
	else
	{
		buffer.movePosition(static_cast<lmms::f_cnt_t>(offset));
	}
}

std::size_t wrappedIndex(std::int64_t offset, std::size_t size)
{
	const auto remainder = offset % static_cast<std::int64_t>(size);
	return remainder < 0 ? static_cast<std::size_t>(static_cast<std::int64_t>(size) + remainder) :
		static_cast<std::size_t>(remainder);
}

} // namespace


class RecordRingBufferTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! Millisecond constructors reject invalid values before float-to-frame conversion.
	void MillisecondConstructors_RejectInvalidValues()
	{
		QVERIFY_EXCEPTION_THROWN((void)lmms::RingBuffer{-1.0f}, std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN((void)lmms::RingBuffer{std::numeric_limits<float>::quiet_NaN()},
			std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN((void)lmms::RingBuffer{std::numeric_limits<float>::infinity()},
			std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN((void)lmms::RingBuffer{std::numeric_limits<float>::max()},
			std::length_error);

	}

	//! Millisecond resizes reject invalid values before changing the allocation.
	void MillisecondResize_RejectInvalidValues()
	{
		lmms::RingBuffer ring{1.0f};
		QVERIFY_EXCEPTION_THROWN(ring.changeSize(-1.0f), std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN(ring.changeSize(std::numeric_limits<float>::quiet_NaN()),
			std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN(ring.changeSize(std::numeric_limits<float>::infinity()),
			std::invalid_argument);
		QVERIFY_EXCEPTION_THROWN(ring.changeSize(std::numeric_limits<float>::max()),
			std::length_error);
	}

	//! Period addition must be checked before creating the allocation.
	void FrameConstructor_RejectPeriodAdditionOverflow()
	{
		const auto largest = std::numeric_limits<lmms::f_cnt_t>::max();
		QVERIFY_EXCEPTION_THROWN((void)lmms::RingBuffer{largest}, std::length_error);
	}

	//! Period addition must be checked before replacing the existing allocation.
	void FrameResize_RejectPeriodAdditionOverflow()
	{
		const auto largest = std::numeric_limits<lmms::f_cnt_t>::max();
		lmms::RingBuffer ring{lmms::f_cnt_t{128}};
		QVERIFY_EXCEPTION_THROWN(ring.changeSize(largest), std::length_error);
	}

	//! A samplerate-aware ring keeps the exact ceil-rounded frame count after a rate change.
	void SamplerateUpdate_PreservesScaledFrameCount()
	{
		auto* audio = lmms::Engine::audioEngine();
		auto* device = audio->audioDev();
		QVERIFY(device != nullptr);
		SampleRateRestore restore{device, audio->outputSampleRate()};

		constexpr lmms::sample_rate_t oldRate = 44100;
		constexpr lmms::sample_rate_t newRate = 48000;
		device->setSampleRateForTesting(oldRate);
		audio->sampleRateChanged();

		lmms::RingBuffer ring{1023.3333f}; // 45,129 frames at 44.1 kHz
		lmms::SampleFrame written{0.25f, -0.5f};

		device->setSampleRateForTesting(newRate);
		audio->sampleRateChanged();
		ring.write(&written, lmms::f_cnt_t{0}, lmms::f_cnt_t{1});

		const lmms::f_cnt_t expectedFrames = (lmms::f_cnt_t{45129} * newRate + oldRate - 1) / oldRate;
		const lmms::f_cnt_t expectedSize = expectedFrames + audio->framesPerPeriod();
		lmms::SampleFrame observed;
		ring.read(&observed, static_cast<std::int64_t>(expectedSize), lmms::f_cnt_t{1});
		QCOMPARE(observed.left(), written.left());
		QCOMPARE(observed.right(), written.right());
	}

	//! Negative frame and millisecond offsets address frames before the position, wrapping at zero.
	void RingBuffer_NegativeOffsetsMoveBackward()
	{
		const f_cnt_t period = Engine::audioEngine()->framesPerPeriod();
		const f_cnt_t capacity = period + 8;
		const auto size = static_cast<std::size_t>(capacity);
		RingBuffer ring{f_cnt_t{8}};
		std::vector<SampleFrame> input(size);
		for (std::size_t i = 0; i < size; ++i)
		{
			input[i] = SampleFrame(static_cast<sample_t>(i), static_cast<sample_t>(-static_cast<float>(i)));
		}
		const auto refill = [&] {
			ring.reset();
			ring.write(input.data(), f_cnt_t{0}, capacity);
		};
		std::vector<SampleFrame> output(static_cast<std::size_t>(period));

		refill();
		readAtFrameOffset(ring, output.data(), std::int64_t{-1});
		QCOMPARE(output.front().left(), input[wrappedIndex(-1, size)].left());
	SampleFrame previousFrame;
	readFramesAtFrameOffset(ring, &previousFrame, std::int64_t{-1}, f_cnt_t{1});
	QCOMPARE(previousFrame.left(), input[wrappedIndex(-1, size)].left());

		refill();
		moveByFrameOffset(ring, std::int64_t{-1});
		ring.read(output.data());
		QCOMPARE(output.front().left(), input[wrappedIndex(-1, size)].left());

		const float milliseconds = -1.0f;
		const auto millisecondOffset = static_cast<std::int64_t>(std::ceil(
			milliseconds * static_cast<float>(Engine::audioEngine()->outputSampleRate()) * 0.001f));
		refill();
		ring.read(output.data(), milliseconds);
		QCOMPARE(output.front().left(), input[wrappedIndex(millisecondOffset, size)].left());

		refill();
		ring.movePosition(milliseconds);
		ring.read(output.data());
		QCOMPARE(output.front().left(), input[wrappedIndex(millisecondOffset, size)].left());
	}

	//! Capacity is rounded up to a power of two and never below the request.
	void Capacity_IsPowerOfTwoAndAtLeastRequested()
	{
		RecordRingBuffer small{1000};
		QCOMPARE(asNumber(small.capacity()), asNumber(1024));
		QVERIFY(small.capacity() >= 1000);
		QVERIFY((small.capacity() & (small.capacity() - 1)) == 0);

		RecordRingBuffer exact{1u << 16};
		QCOMPARE(asNumber(exact.capacity()), asNumber(1u << 16));
	}

	// Past the largest power of two a size_t holds, `result <<= 1` wrapped to
	// zero and the constructor never returned. SampleFrameRingBuffer::nextPowerOfTwo
	// already refuses such a value; this ring must refuse it too.
	//
	// The capacity is read through a volatile on purpose: as a literal the
	// compiler constant-folds the shift loop, the fold stops at 2^63 and the
	// std::vector ctor throws a length_error of its own, which would satisfy
	// this assertion while the runtime path still spins forever.
	void refusesACapacityThatCannotBeRoundedUp()
	{
		volatile std::size_t beyondLargestPowerOfTwo = (std::numeric_limits<std::size_t>::max() >> 1) + 2;
		QVERIFY_EXCEPTION_THROWN({ RecordRingBuffer ring(beyondLargestPowerOfTwo); },
			std::length_error);
	}

	//! A fresh ring reports no data and read() returns 0.
	void Empty_OnConstruction()
	{
		RecordRingBuffer ring{128};
		QCOMPARE(asNumber(ring.available()), asNumber(0));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));

		sample_t out = -1.f;
		QCOMPARE(asNumber(ring.read(&out, 1)), asNumber(0));
		QCOMPARE(out, -1.f);
	}

	//! A single frame round-trips unchanged.
	void WriteRead_SingleFrame()
	{
		RecordRingBuffer ring{128};
		QVERIFY(ring.write(0.25f));
		QCOMPARE(asNumber(ring.available()), asNumber(1));

		sample_t out = 0.f;
		QCOMPARE(asNumber(ring.read(&out, 1)), asNumber(1));
		QCOMPARE(out, 0.25f);
		QCOMPARE(asNumber(ring.available()), asNumber(0));
	}

	//! A block round-trips in order, without loss or duplication.
	void WriteRead_BlockPreservesOrderAndValues()
	{
		constexpr std::size_t frames = 1000;
		RecordRingBuffer ring{2048};

		std::vector<sample_t> in(frames);
		for (std::size_t i = 0; i < frames; ++i) { in[i] = testValue(i); }
		QCOMPARE(asNumber(ring.writeBlock(in.data(), frames)), asNumber(frames));

		std::vector<sample_t> out(frames, 0.f);
		QCOMPARE(asNumber(ring.read(out.data(), frames)), asNumber(frames));
		for (std::size_t i = 0; i < frames; ++i) { QCOMPARE(out[i], in[i]); }
	}

	//! A full ring drops the newest frames and counts them; old data survives.
	void Full_DropsNewestAndCountsOverflow()
	{
		RecordRingBuffer ring{64};
		std::vector<sample_t> in(64);
		for (std::size_t i = 0; i < 64; ++i) { in[i] = testValue(i); }
		QCOMPARE(asNumber(ring.writeBlock(in.data(), 64)), asNumber(64));
		QCOMPARE(asNumber(ring.available()), asNumber(64));

		std::vector<sample_t> extra(10, 9.f);
		QCOMPARE(asNumber(ring.writeBlock(extra.data(), 10)), asNumber(0));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(10));
		QVERIFY(!ring.write(9.f));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(11));

		// The 64 original frames are intact and in order.
		std::vector<sample_t> out(64, 0.f);
		QCOMPARE(asNumber(ring.read(out.data(), 64)), asNumber(64));
		for (std::size_t i = 0; i < 64; ++i) { QCOMPARE(out[i], in[i]); }
	}

	//! Reading and writing across the wrap boundary preserves order.
	void WrapAround_PreservesOrder()
	{
		RecordRingBuffer ring{64};
		std::vector<sample_t> out(64);
		std::uint64_t produced = 0;

		for (int round = 0; round < 20; ++round)
		{
			std::vector<sample_t> in(50);
			for (std::size_t i = 0; i < in.size(); ++i) { in[i] = testValue(produced + i); }
			QCOMPARE(asNumber(ring.writeBlock(in.data(), in.size())), asNumber(in.size()));
			produced += in.size();

			const auto got = ring.read(out.data(), 50);
			QCOMPARE(asNumber(got), asNumber(50));
			for (std::size_t i = 0; i < got; ++i)
			{
				QCOMPARE(out[i], testValue(produced - 50 + i));
			}
		}
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));
	}

	//! writeStrided() demuxes one channel of an interleaved buffer.
	void WriteStrided_DemuxesOneChannel()
	{
		RecordRingBuffer ring{64};
		// interleaved stereo: [l0 r0 l1 r1 ...]
		std::vector<sample_t> interleaved(32);
		for (std::size_t frame = 0; frame < 16; ++frame)
		{
			interleaved[2 * frame] = testValue(frame);        // left
			interleaved[2 * frame + 1] = -testValue(frame);   // right
		}

		QCOMPARE(asNumber(ring.writeStrided(&interleaved[1], 2, 16)), asNumber(16));
		std::vector<sample_t> out(16, 0.f);
		QCOMPARE(asNumber(ring.read(out.data(), 16)), asNumber(16));
		for (std::size_t frame = 0; frame < 16; ++frame)
		{
			QCOMPARE(out[frame], -testValue(frame));
		}
	}

	//! reset() discards buffered frames and clears the overflow counter.
	void Reset_DiscardsFramesAndClearsOverflow()
	{
		RecordRingBuffer ring{64};
		std::vector<sample_t> in(64, 0.5f);
		ring.writeBlock(in.data(), 64);
		ring.write(0.5f);
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(1));

		ring.reset();
		QCOMPARE(asNumber(ring.available()), asNumber(0));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));

		QVERIFY(ring.write(0.5f));
		sample_t out = 0.f;
		QCOMPARE(asNumber(ring.read(&out, 1)), asNumber(1));
		QCOMPARE(out, 0.5f);
	}

	//! Producer and consumer threads: no frame is lost, duplicated or reordered.
	void ConcurrentProducerConsumer_NoLossNoDuplication()
	{
		constexpr std::uint64_t totalFrames = 1000000;
		// Capacity exceeds the total so the test is deterministic (no overflow).
		RecordRingBuffer ring{1u << 20};

		std::thread producer([&ring] {
			std::vector<sample_t> block(512);
			for (std::uint64_t pos = 0; pos < totalFrames; pos += block.size())
			{
				const auto frames = static_cast<std::size_t>(
					std::min<std::uint64_t>(block.size(), totalFrames - pos));
				for (std::size_t i = 0; i < frames; ++i)
				{
					block[i] = static_cast<sample_t>(pos + i); // exact below 2^24
				}
				const auto written = ring.writeBlock(block.data(), frames);
				Q_ASSERT(written == frames);
				std::this_thread::yield();
			}
		});

		std::vector<sample_t> out(1024);
		std::uint64_t received = 0;
		bool sequenceOk = true;
		while (received < totalFrames)
		{
			const auto frames = ring.read(out.data(), out.size());
			for (std::size_t i = 0; i < frames; ++i)
			{
				if (out[i] != static_cast<sample_t>(received + i)) { sequenceOk = false; }
			}
			received += frames;
			if (frames == 0) { std::this_thread::yield(); }
		}
		producer.join();

		QVERIFY(sequenceOk);
		QCOMPARE(asNumber(received), asNumber(totalFrames));
		QCOMPARE(asNumber(ring.overflowCount()), asNumber(0));
	}

	//! The producer path must not allocate (dynamic check on this thread).
	void ProducerPath_DoesNotAllocate()
	{
		RecordRingBuffer ring{1u << 16};
		std::vector<sample_t> block(4096, 0.5f); // allocated before counting

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (int i = 0; i < 1000; ++i)
		{
			ring.writeBlock(block.data(), block.size());
		}
		lmms::test::tlCountAllocations = false;

		QCOMPARE(asNumber(lmms::test::tlAllocationCount), asNumber(0));
	}
} ;


QTEST_GUILESS_MAIN(RecordRingBufferTest)

#include "RecordRingBufferTest.moc"
