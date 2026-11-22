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

namespace
{
//! Qt 6 counts a QByteArray in qsizetype, so the exact wide count is returned; Qt 5 counts it in int,
//! so a count past INT_MAX is refused rather than handed to an int parameter.
void expectWideOrRefused(const std::optional<qsizetype>& bytes, size_t frames)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	QVERIFY(bytes.has_value());
	QCOMPARE(static_cast<size_t>(*bytes), frames * sizeof(SampleFrame));
	QVERIFY(*bytes > std::numeric_limits<int>::max());
#else
	Q_UNUSED(frames);
	QVERIFY(!bytes.has_value());
#endif
}
} // namespace


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
		// 300 million frames: 2.4e9 bytes, past INT_MAX.
		const size_t frames = 300'000'000;
		const auto bytes = SampleBuffer::base64SourceBytes(frames);
		expectWideOrRefused(bytes, frames);
	}

	void countThatWrapsToSmallIsNotTruncated()
	{
		// 2^29 frames * 8 bytes = 2^32 bytes: the int cast gives exactly 0, an empty saved sample.
		const size_t frames = size_t{1} << 29;
		const auto bytes = SampleBuffer::base64SourceBytes(frames);
		expectWideOrRefused(bytes, frames);
	}

	void qt5CapRefusesWhatAnIntCountedByteArrayCannotHold()
	{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		// Qt 5's QByteArray::fromRawData takes an int: a count above INT_MAX / 4 * 3 would be narrowed.
		const size_t capFrames = (static_cast<size_t>(std::numeric_limits<int>::max() / 4) * 3) / sizeof(SampleFrame);
		QVERIFY(SampleBuffer::base64SourceBytes(capFrames).has_value());
		QVERIFY(!SampleBuffer::base64SourceBytes(capFrames + 1));
#else
		QSKIP("Qt 5 only: Qt 6 counts a QByteArray in qsizetype");
#endif
	}

	void anyAcceptedCountLeavesRoomForTheBase64Text()
	{
		// Probe around the limit of this Qt: every accepted count must keep its 4/3 text inside the
		// type the QByteArray counts in (int on Qt 5, qsizetype on Qt 6).
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		using Counter = int;
		const size_t edge = (static_cast<size_t>(std::numeric_limits<int>::max() / 4) * 3) / sizeof(SampleFrame);
#else
		using Counter = qsizetype;
		const size_t edge = (static_cast<size_t>(std::numeric_limits<qsizetype>::max() / 16) * 3) / sizeof(SampleFrame);
#endif
		for (const size_t frames : {edge - 1, edge, edge + 1, edge * 2})
		{
			const auto bytes = SampleBuffer::base64SourceBytes(frames);
			if (!bytes) { continue; }
			const auto text = (static_cast<quint64>(*bytes) + 2) / 3 * 4;
			QVERIFY(text <= static_cast<quint64>(std::numeric_limits<Counter>::max()));
		}
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
