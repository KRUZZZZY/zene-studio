/*
 * BrowserTestSupport.h - the WAV fixture the browser tests read.
 *
 * The file is written BYTE BY BYTE, not with libsndfile. Two reasons:
 *   - libsndfile's include directory is PRIVATE to lmmsobjs, so a test source
 *     cannot `#include <sndfile.h>`; and
 *   - a fixture produced by the same library the probe uses would prove less
 *     than one produced independently. This writer emits the RIFF/WAVE layout
 *     the specification defines: an `fmt ` chunk, a `LIST`/`INFO` chunk (so
 *     there is an embedded tag to read back) and a `data` chunk.
 * The assertions in the tests are what validate the writer: if the header were
 * wrong, libsndfile would refuse the file and the sample rate, channel count and
 * frame count would not come back.
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

#ifndef ZENE_BROWSER_TEST_SUPPORT_H
#define ZENE_BROWSER_TEST_SUPPORT_H

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QString>

namespace browserfixture
{

//! What the fixture file holds.
//!
//! The numbers are chosen so the burst lands on a bucket boundary at BOTH
//! resolutions the tests read: the cache stores min(frames, BaseBuckets) peaks,
//! so `frames == 2048` means one stored peak per frame and an aggregation to any
//! bucket count is exact. The burst is 64 frames at frame 1024, i.e. exactly
//! bucket 16 of 32 (64 frames each) and bucket 8 of 16 (128 frames each).
//!
//! The two-level mapping is not a division of the file: each stored peak lands
//! in exactly one output bucket, but a burst that straddles a boundary appears
//! in both neighbours. Asserting an exact bucket is therefore only honest when
//! the burst is aligned, which is what these numbers do.
struct WaveSpec
{
	int sampleRate = 48000;
	int channels = 2;
	int frames = 2048;
	int burstStartFrame = 1024;
	int burstFrames = 64;
	//! Peak amplitude in the 16-bit domain; 20000/32768 = 0.6103515625.
	int burstAmplitude = 20000;
	//! The RIFF `LIST`/`INFO` `INAM` value - the embedded title libsndfile reads
	//! back as SF_STR_TITLE, which is what makes "match on metadata" testable.
	QString title = QStringLiteral("Kick Drum");
};

namespace detail
{

inline void writeChunkHeader(QDataStream& stream, const char* id, quint32 size)
{
	stream.writeRawData(id, 4);
	stream << size;
}

inline void writeRiffHeader(QDataStream& stream, quint32 riffSize)
{
	stream.writeRawData("RIFF", 4);
	stream << riffSize;
	stream.writeRawData("WAVE", 4);
}

inline void writeFormatChunk(QDataStream& stream, const WaveSpec& spec, quint16 blockAlign, quint32 byteRate)
{
	writeChunkHeader(stream, "fmt ", 16);
	stream << quint16(1) << quint16(spec.channels) << quint32(spec.sampleRate) << byteRate
		<< blockAlign << quint16(16);
}

//! The `LIST`/`INFO` chunk, or nothing when the spec carries no title. `INFO`
//! sub-chunks are padded to an even length, as the format requires.
inline QByteArray listInfoChunk(const WaveSpec& spec)
{
	if (spec.title.isEmpty()) { return QByteArray(); }
	QByteArray body("INFO", 4);
	QByteArray value = spec.title.toUtf8();
	value.append('\0');
	QByteArray sub;
	QDataStream subStream(&sub, QIODevice::WriteOnly);
	subStream.setByteOrder(QDataStream::LittleEndian);
	subStream.writeRawData("INAM", 4);
	subStream << quint32(value.size());
	subStream.writeRawData(value.constData(), value.size());
	body.append(sub);
	if (sub.size() % 2 != 0) { body.append('\0'); }
	return body;
}

//! The `data` chunk's samples: silence everywhere except one burst.
inline QByteArray sampleBytes(const WaveSpec& spec)
{
	QByteArray bytes;
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);
	for (int frame = 0; frame < spec.frames; ++frame)
	{
		const bool inBurst = frame >= spec.burstStartFrame
			&& frame < spec.burstStartFrame + spec.burstFrames;
		// The burst alternates sign, so a bucket inside it has BOTH a negative
		// minimum and a positive maximum: an assertion on either alone would
		// pass even if the other half of the peak pair were never updated.
		const bool positive = frame % 2 == 0;
		const qint16 value = inBurst
			? static_cast<qint16>(positive ? spec.burstAmplitude : -spec.burstAmplitude) : qint16(0);
		for (int channel = 0; channel < spec.channels; ++channel) { stream << value; }
	}
	return bytes;
}

} // namespace detail

//! Writes \a spec to \a path as a canonical RIFF/WAVE PCM-16 file. False when
//! the file cannot be created, so a caller can QVERIFY on the result rather than
//! testing against a file that was never written.
inline bool writeWave(const QString& path, const WaveSpec& spec)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }

	QByteArray body;
	QDataStream stream(&body, QIODevice::WriteOnly);
	stream.setByteOrder(QDataStream::LittleEndian);

	const quint16 blockAlign = static_cast<quint16>(spec.channels * 2);
	const quint32 byteRate = static_cast<quint32>(spec.sampleRate) * blockAlign;
	const QByteArray info = detail::listInfoChunk(spec);
	const QByteArray samples = detail::sampleBytes(spec);

	detail::writeFormatChunk(stream, spec, blockAlign, byteRate);
	if (!info.isEmpty())
	{
		detail::writeChunkHeader(stream, "LIST", static_cast<quint32>(info.size()));
		stream.writeRawData(info.constData(), info.size());
	}
	detail::writeChunkHeader(stream, "data", static_cast<quint32>(samples.size()));
	stream.writeRawData(samples.constData(), samples.size());

	QByteArray out;
	QDataStream outStream(&out, QIODevice::WriteOnly);
	outStream.setByteOrder(QDataStream::LittleEndian);
	detail::writeRiffHeader(outStream, static_cast<quint32>(body.size() + 4));
	out.append(body);

	const bool written = file.write(out) == out.size();
	file.close();
	return written;
}

//! Writes a small text file, for the "the probe refuses what libsndfile cannot
//! read" case and for a query that must not match an audio file.
inline bool writeText(const QString& path, const QString& text)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	const bool written = file.write(text.toUtf8()) >= 0;
	file.close();
	return written;
}

} // namespace browserfixture

#endif // ZENE_BROWSER_TEST_SUPPORT_H
