/*
 * SampleBufferBase64Test.cpp - the byte count SampleBuffer::toBase64() serialises
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
/*! Cluster new-p10-343: toBase64() cast the byte count to int, so a sample of more than 2^31 bytes
 *  (268 million stereo frames of 8 bytes) wrapped and the saved project lost the audio. The size
 *  arithmetic is a static function so the large counts are tested without allocating them. */

#include <QtTest>

#include <limits>
#include <vector>

#include "SampleBuffer.h"

using namespace lmms;

class SampleBufferBase64Test : public QObject
{
	Q_OBJECT

private slots:
	void smallCountIsFramesTimesFrameSize()
	{
		QCOMPARE(SampleBuffer::base64SourceBytes(0).value_or(-1), qsizetype{0});
		QCOMPARE(SampleBuffer::base64SourceBytes(1000).value_or(-1), qsizetype{1000} * qsizetype{sizeof(SampleFrame)});
	}

	void countAboveIntMaxIsNotWrapped()
	{
		// 300 million frames: 2.4e9 bytes, past INT_MAX, and 2^32 + 4 frames wraps to a tiny int.
		const size_t frames = 300'000'000;
		const auto bytes = SampleBuffer::base64SourceBytes(frames);
		QVERIFY(bytes.has_value());
		QCOMPARE(static_cast<size_t>(*bytes), frames * sizeof(SampleFrame));
		QVERIFY(*bytes > std::numeric_limits<int>::max());
	}

	void countThatWrapsToSmallIsNotTruncated()
	{
		// 2^29 frames * 8 bytes = 2^32 bytes: the int cast gives exactly 0, an empty saved sample.
		const size_t frames = size_t{1} << 29;
		const auto bytes = SampleBuffer::base64SourceBytes(frames);
		QVERIFY(bytes.has_value());
		QCOMPARE(static_cast<size_t>(*bytes), frames * sizeof(SampleFrame));
	}

	void countThatCannotBeHeldIsRefused()
	{
		QVERIFY(!SampleBuffer::base64SourceBytes(std::numeric_limits<size_t>::max() / sizeof(SampleFrame)));
	}

	void roundTripStillWorks()
	{
		std::vector<SampleFrame> frames(16);
		for (size_t i = 0; i < frames.size(); ++i) { frames[i] = SampleFrame(float(i), -float(i)); }
		const SampleBuffer buffer(frames, 44100);
		const auto back = SampleBuffer::fromBase64(buffer.toBase64(), 44100);
		QCOMPARE(back->size(), frames.size());
		QCOMPARE(back->data()[5].left(), 5.f);
		QCOMPARE(back->data()[5].right(), -5.f);
	}
};

QTEST_GUILESS_MAIN(SampleBufferBase64Test)
#include "SampleBufferBase64Test.moc"
