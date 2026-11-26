/*
 * SampleDecoderShortReadTest.cpp - a file whose header promises more frames than
 *                                  its payload holds is refused, not padded with silence
 *
 * The decoder sizes its result from the header's frame count and does not check
 * how many frames libsndfile actually read, so the unread tail stays silence.
 * A cut-short FLAC (valid STREAMINFO, no audio frames) must not decode to that.
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
 */

#include <QtTest>

#include <algorithm>
#include <sndfile.h>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include "ImportDetection.h"
#include "SampleDecoder.h"

using namespace lmms;

namespace
{

constexpr int Rate = 44100;
constexpr int Wav = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

//! Writes @a frames of a mono tone as a complete WAV.
bool writeTone(const QString& path, sf_count_t frames)
{
	SF_INFO info{};
	info.samplerate = Rate;
	info.channels = 1;
	info.format = Wav;
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	std::vector<float> chunk(256, 0.5f);
	for (sf_count_t done = 0; done < frames;)
	{
		const sf_count_t n = std::min<sf_count_t>(chunk.size(), frames - done);
		sf_writef_float(file, chunk.data(), n);
		done += n;
	}
	return sf_close(file) == 0;
}

/*! Writes a FLAC file whose STREAMINFO header declares \a declaredFrames frames
 *  and which carries no audio frames at all: the shape of a file cut short after
 *  its header. libsndfile trusts the FLAC header's total sample count, so the
 *  file opens and reports a length while holding no audio.
 *
 *  Copied from BrowserCatalogTest.cpp (writeHeaderOnlyFlac).
 */
bool writeHeaderOnlyFlac(const QString& path, quint64 declaredFrames)
{
	const quint32 sampleRate = 48000;
	const quint8 channels = 1;
	const quint8 bitsPerSample = 16;

	QByteArray info;
	const auto push = [&info](quint32 value, int bytes)
	{
		for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8)
		{
			info.append(static_cast<char>((value >> shift) & 0xff));
		}
	};
	push(4096, 2);  // min block size
	push(4096, 2);  // max block size
	push(0, 3);     // min frame size (unknown)
	push(0, 3);     // max frame size (unknown)
	// Sample rate (20 bits), channels-1 (3), bits per sample - 1 (5) and the top
	// 4 bits of the 36-bit total sample count, packed across four bytes and then
	// the count's low 32 bits.
	info.append(static_cast<char>((sampleRate >> 12) & 0xff));
	info.append(static_cast<char>((sampleRate >> 4) & 0xff));
	info.append(static_cast<char>(((sampleRate & 0xf) << 4) | (((channels - 1) & 0x7) << 1)
		| (((bitsPerSample - 1) >> 4) & 0x1)));
	info.append(static_cast<char>((((bitsPerSample - 1) & 0xf) << 4) | ((declaredFrames >> 32) & 0xf)));
	push(static_cast<quint32>(declaredFrames & 0xffffffff), 4);
	info.append(QByteArray(16, '\0'));  // the MD5 of the unencoded audio
	if (info.size() != 34) { return false; }

	QByteArray out("fLaC", 4);
	out.append(static_cast<char>(0x80));  // last metadata block, type STREAMINFO
	// The block's length is a 24-bit big-endian count of the bytes that follow.
	for (int shift = 16; shift >= 0; shift -= 8)
	{
		out.append(static_cast<char>((info.size() >> shift) & 0xff));
	}
	out.append(info);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	const bool written = file.write(out) == out.size();
	file.close();
	return written;
}

} // namespace

class SampleDecoderShortReadTest : public QObject
{
	Q_OBJECT
private:
	QTemporaryDir m_dir;
	QString path(const QString& name) const { return m_dir.filePath(name); }

private slots:
	void initTestCase() { QVERIFY(m_dir.isValid()); }

	//! The control: a complete WAV still decodes all of its frames, with audio in them.
	void completeWavStillDecodesItsFrames()
	{
		const QString file = path(QStringLiteral("complete.wav"));
		QVERIFY(writeTone(file, 1000));
		const auto result = SampleDecoder::decode(file);
		QVERIFY(result.has_value());
		QCOMPARE(result->sampleRate, Rate);
		QCOMPARE(result->data.size(), static_cast<std::size_t>(1000));
		bool anyAudio = false;
		for (const auto& frame : result->data)
		{
			if (frame.left() != 0.0f || frame.right() != 0.0f) { anyAudio = true; break; }
		}
		QVERIFY2(anyAudio, "a complete file decoded to silence");
	}

	//! The boundary control: a one-frame complete file is accepted.
	void oneFrameWavStillDecodes()
	{
		const QString file = path(QStringLiteral("one.wav"));
		QVERIFY(writeTone(file, 1));
		const auto result = SampleDecoder::decode(file);
		QVERIFY(result.has_value());
		QCOMPARE(result->data.size(), static_cast<std::size_t>(1));
	}

	//! A FLAC whose header declares 160 frames but holds none is refused. The bounded
	//! read proves libsndfile opened it and that the header is the length source, so
	//! the refusal is the decoder's, not a file the library could not open.
	void shortSoundFileReadIsNotPadded()
	{
		const QString file = path(QStringLiteral("short.flac"));
		QVERIFY(writeHeaderOnlyFlac(file, 160));

		const auto bounded = SampleDecoder::decodeLimited(file, 1.0);
		QVERIFY2(bounded.has_value(), "libsndfile did not open the header-only FLAC");
		QCOMPARE(bounded->totalFrames, static_cast<std::int64_t>(160));

		const auto full = SampleDecoder::decode(file);
		QVERIFY2(!full.has_value(),
			qPrintable(QStringLiteral("a 160-frame FLAC with no audio decoded to %1 padded frames")
				.arg(full ? full->data.size() : 0)));
	}

	void analysisRefusesAFlacWithAudioButMissingDeclaredFrames()
	{
		QFile fixture(QStringLiteral(LMMS_TEST_DATA_DIR "/../../data/samples/waveforms/lfo_trancegate_whole.flac"));
		QVERIFY(fixture.open(QIODevice::ReadOnly));
		QByteArray bytes = fixture.readAll();
		QVERIFY(bytes.size() > 26 && bytes.startsWith("fLaC"));

		// Increase the STREAMINFO frame count to 196608 without changing the audio.
		const QString file = path(QStringLiteral("missing-tail.flac"));
		bytes.replace(22, 4, QByteArray("\0\3\0\0", 4));
		QFile damaged(file);
		QVERIFY(damaged.open(QIODevice::WriteOnly));
		QCOMPARE(damaged.write(bytes), static_cast<qint64>(bytes.size()));
		damaged.close();

		const auto prefix = SampleDecoder::decodeLimited(file, 1.0);
		QVERIFY(prefix && !prefix->window.data.empty());
		QVERIFY(prefix->totalFrames > static_cast<qint64>(prefix->window.data.size()));
		const auto analysis = analyseAudioFile(file, 1.0);
		QVERIFY2(!analysis.ok, "analysis accepted a FLAC with a missing tail");
		QVERIFY(!analysis.error.isEmpty());
	}
};

QTEST_MAIN(SampleDecoderShortReadTest)
#include "SampleDecoderShortReadTest.moc"
