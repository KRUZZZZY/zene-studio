/*
 * MasteringTestSourceMix.cpp - source WAV metadata validation and chunked reads
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

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <vector>

#include "MasteringJob.h"

using namespace lmms;

namespace
{

//! A 32-bit float WAV of \a channels: the source loader reads floats, so the
//! round trip through this fixture is exact and a sample assertion is exact.
bool writeFloatWavWithChannels(const QString& path, const std::vector<SampleFrame>& frames,
	std::size_t channels, sample_rate_t sampleRate)
{
	SF_INFO info{};
	info.samplerate = static_cast<int>(sampleRate);
	info.channels = static_cast<int>(channels);
	info.format = (channels > 2 ? SF_FORMAT_WAVEX : SF_FORMAT_WAV) | SF_FORMAT_FLOAT;

	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		return false;
	}
	std::vector<float> interleaved(frames.size() * channels, 0.0f);
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		interleaved[i * channels] = frames[i].left();
		if (channels > 1)
		{
			interleaved[i * channels + 1] = frames[i].right();
		}
	}
	sf_count_t written = 0;
	if (!frames.empty())
	{
		written = sf_writef_float(file, interleaved.data(),
			static_cast<sf_count_t>(frames.size()));
	}
	sf_close(file);
	return written == static_cast<sf_count_t>(frames.size());
}

//! A 16-bit stereo FLAC: the short-read fixture. A FLAC cut in half keeps a
//! readable header but no bytes for the tail frames the header promises.
bool writeInt16Flac(const QString& path, const std::vector<SampleFrame>& frames,
	sample_rate_t sampleRate)
{
	SF_INFO info{};
	info.samplerate = static_cast<int>(sampleRate);
	info.channels = 2;
	info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		return false;
	}
	std::vector<short> interleaved(frames.size() * 2, 0);
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		interleaved[i * 2] = static_cast<short>(qRound(frames[i].left() * 32767.0f));
		interleaved[i * 2 + 1] = static_cast<short>(qRound(frames[i].right() * 32767.0f));
	}
	sf_count_t written = 0;
	if (!frames.empty())
	{
		written = sf_writef_short(file, interleaved.data(),
			static_cast<sf_count_t>(frames.size()));
	}
	sf_close(file);
	return written == static_cast<sf_count_t>(frames.size());
}

//! Rewrites the sample-rate and byte-rate fields of a 44-byte-header PCM/float
//! WAV. libsndfile refuses to WRITE some of the rates the loader must refuse,
//! so the fixture is written valid and then patched, as a hostile file would be.
bool patchWavSampleRate(const QString& path, quint32 sampleRate)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadWrite) || !file.seek(32))
	{
		return false;
	}
	char alignBytes[2];
	if (file.read(alignBytes, 2) != 2)
	{
		return false;
	}
	const auto blockAlign = static_cast<quint32>(
		static_cast<unsigned char>(alignBytes[0])
		| (static_cast<unsigned char>(alignBytes[1]) << 8));
	char buffer[4];
	qToLittleEndian(sampleRate, buffer);
	if (!file.seek(24) || file.write(buffer, 4) != 4)
	{
		return false;
	}
	qToLittleEndian(sampleRate * blockAlign, buffer);
	return file.seek(28) && file.write(buffer, 4) == 4;
}

} // namespace

class MasteringSourceMixTest : public QObject
{
	Q_OBJECT

private slots:
	//! Baseline: a valid 44.1 kHz float stereo source loads with the exact frame
	//! count and the exact samples written - the unchanged behaviour the fixes
	//! must not disturb.
	void sourceMixValidStereoLoadsExactly()
	{
		std::vector<SampleFrame> signal(1000);
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			signal[i].setLeft(static_cast<sample_t>((static_cast<int>(i % 13) - 6) / 16.0f));
			signal[i].setRight(static_cast<sample_t>((static_cast<int>(i % 7) - 3) / 32.0f));
		}
		const QString path = m_dir.filePath(QStringLiteral("source-valid.wav"));
		QVERIFY2(writeFloatWavWithChannels(path, signal, 2, 44100),
			"could not write the valid fixture");

		std::vector<SampleFrame> loaded;
		sample_rate_t rate = 0;
		QString error;
		QVERIFY2(MasteringJob::readSourceMix(path, loaded, rate, &error), qPrintable(error));
		QCOMPARE(loaded.size(), signal.size());
		QCOMPARE(rate, sample_rate_t{44100});
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			QCOMPARE(loaded[i].left(), signal[i].left());
			QCOMPARE(loaded[i].right(), signal[i].right());
		}
	}

	//! The mastering source render is the render path's own output, and that
	//! path accepts 44100..192000 Hz only (main.cpp:1099, the export-preset
	//! schema). A rate outside that window must be refused before it reaches
	//! the meter or a candidate WAV's header.
	void sourceMixRateOutsideRenderWindowIsRefused_data()
	{
		QTest::addColumn<quint32>("rate");
		QTest::addColumn<bool>("accepted");
		QTest::newRow("zero") << quint32{0} << false;
		QTest::newRow("8000") << quint32{8000} << false;
		QTest::newRow("768000") << quint32{768000} << false;
		QTest::newRow("0x7fffffff") << quint32{0x7fffffffu} << false;
		QTest::newRow("44099") << quint32{44099} << false;
		QTest::newRow("192001") << quint32{192001} << false;
		QTest::newRow("44100") << quint32{44100} << true;
		QTest::newRow("192000") << quint32{192000} << true;
	}

	void sourceMixRateOutsideRenderWindowIsRefused()
	{
		QFETCH(quint32, rate);
		QFETCH(bool, accepted);
		std::vector<SampleFrame> signal(32);
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			signal[i].setLeft(static_cast<sample_t>(i) / 64.0f);
			signal[i].setRight(static_cast<sample_t>(-static_cast<int>(i)) / 64.0f);
		}
		const QString path = m_dir.filePath(QStringLiteral("source-rate-%1.wav").arg(rate));
		QVERIFY2(writeFloatWavWithChannels(path, signal, 2, 44100),
			"could not write the rate fixture");
		QVERIFY2(patchWavSampleRate(path, rate), "could not patch the fixture's rate");

		std::vector<SampleFrame> loaded;
		sample_rate_t loadedRate = 12345;
		QString error;
		const bool acceptedByLoader = MasteringJob::readSourceMix(path, loaded, loadedRate, &error);
		if (accepted)
		{
			QVERIFY2(acceptedByLoader,
				qPrintable(QStringLiteral("rate %1 was refused: %2").arg(rate).arg(error)));
			QCOMPARE(loadedRate, static_cast<sample_rate_t>(rate));
			QCOMPARE(loaded.size(), signal.size());
			return;
		}
		QVERIFY2(!acceptedByLoader,
			qPrintable(QStringLiteral("rate %1 was accepted").arg(rate)));
		QVERIFY2(!error.isEmpty(), "the refusal carries no message");
		QVERIFY(loaded.empty());
		QCOMPARE(loadedRate, sample_rate_t{12345});
	}

	//! Only stereo is mastered; the existing channel gate must keep refusing
	//! everything else before it allocates or touches the caller's state.
	void sourceMixChannelCountsOtherThanStereoAreRefused_data()
	{
		QTest::addColumn<int>("channels");
		QTest::newRow("mono") << 1;
		QTest::newRow("six") << 6;
	}

	void sourceMixChannelCountsOtherThanStereoAreRefused()
	{
		QFETCH(int, channels);
		std::vector<SampleFrame> signal(16);
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			signal[i].setLeft(static_cast<sample_t>(i) / 32.0f);
			signal[i].setRight(static_cast<sample_t>(i) / 32.0f);
		}
		const QString path = m_dir.filePath(QStringLiteral("source-%1ch.wav").arg(channels));
		QVERIFY2(writeFloatWavWithChannels(path, signal,
			static_cast<std::size_t>(channels), 44100), "could not write the channel fixture");

		std::vector<SampleFrame> loaded;
		sample_rate_t rate = 12345;
		QString error;
		QVERIFY2(!MasteringJob::readSourceMix(path, loaded, rate, &error),
			qPrintable(QStringLiteral("%1 channels were accepted").arg(channels)));
		QVERIFY2(!error.isEmpty(), "the refusal carries no message");
		QVERIFY(loaded.empty());
		QCOMPARE(rate, sample_rate_t{12345});
	}

	//! A zero-frame WAV is still accepted with an empty source: the fix is about
	//! what the header claims, not about refusing silence.
	void sourceMixZeroFramesIsStillAccepted()
	{
		const QString path = m_dir.filePath(QStringLiteral("source-zero-frames.wav"));
		QVERIFY2(writeFloatWavWithChannels(path, {}, 2, 44100),
			"could not write the zero-frame fixture");

		std::vector<SampleFrame> loaded;
		sample_rate_t rate = 0;
		QString error;
		QVERIFY2(MasteringJob::readSourceMix(path, loaded, rate, &error), qPrintable(error));
		QVERIFY(loaded.empty());
		QCOMPARE(rate, sample_rate_t{44100});
	}

	//! The loader reads in fixed 65536-frame chunks. An append offset mistake
	//! shows up as a repeated or shifted sample after the first chunk, so the
	//! values are exactly representable and distinct per frame.
	void sourceMixSpansChunkBoundaries_data()
	{
		QTest::addColumn<quint64>("frames");
		QTest::newRow("exact-chunk") << quint64{65536};
		QTest::newRow("chunk-plus-tail") << quint64{65536 * 2 + 123};
	}

	void sourceMixSpansChunkBoundaries()
	{
		QFETCH(quint64, frames);
		std::vector<SampleFrame> signal(static_cast<std::size_t>(frames));
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			// i / 2^20 is an exact float for these counts, and unique per frame.
			signal[i].setLeft(static_cast<sample_t>(static_cast<float>(i) / 1048576.0f));
			signal[i].setRight(static_cast<sample_t>(-static_cast<float>(i) / 2097152.0f));
		}
		const QString path = m_dir.filePath(
			QStringLiteral("source-chunks-%1.wav").arg(frames));
		QVERIFY2(writeFloatWavWithChannels(path, signal, 2, 44100),
			"could not write the chunk fixture");

		std::vector<SampleFrame> loaded;
		sample_rate_t rate = 0;
		QString error;
		QVERIFY2(MasteringJob::readSourceMix(path, loaded, rate, &error), qPrintable(error));
		QCOMPARE(loaded.size(), signal.size());
		QCOMPARE(rate, sample_rate_t{44100});
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			QCOMPARE(loaded[i].left(), signal[i].left());
			QCOMPARE(loaded[i].right(), signal[i].right());
		}
	}

	//! A truncated FLAC keeps its header but not all its frames. Mastering a
	//! prefix is not mastering the mix, so the load is refused with no output
	//! and the caller's rate untouched.
	void sourceMixShortReadIsRefused()
	{
		std::vector<SampleFrame> signal(4000);
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			signal[i].setLeft(static_cast<sample_t>(i) / 8192.0f);
			signal[i].setRight(static_cast<sample_t>(-static_cast<int>(i)) / 16384.0f);
		}
		const QString path = m_dir.filePath(QStringLiteral("source-short.flac"));
		QVERIFY2(writeInt16Flac(path, signal, 44100), "could not write the flac fixture");

		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadWrite));
		const qint64 size = file.size();
		QVERIFY(size > 64);
		QVERIFY(file.resize(size / 2));
		file.close();

		std::vector<SampleFrame> loaded;
		sample_rate_t rate = 12345;
		QString error;
		const bool accepted = MasteringJob::readSourceMix(path, loaded, rate, &error);
		QVERIFY2(!accepted, "a truncated flac was mastered as a prefix");
		QVERIFY2(!error.isEmpty(), "the refusal carries no message");
		QVERIFY(loaded.empty());
		QCOMPARE(rate, sample_rate_t{12345});
	}

	//! The pure count helper carries the bound the loader's allocation uses.
	void sourceMixSampleCountIsBounded_data()
	{
		QTest::addColumn<quint64>("frames");
		QTest::addColumn<quint64>("channels");
		QTest::addColumn<bool>("accepted");
		QTest::addColumn<quint64>("expectedCount");

		const std::size_t cap = MasteringJob::maxSourceFrames;
		QTest::newRow("overflow")
			<< static_cast<quint64>(std::numeric_limits<std::size_t>::max() / 2 + 1)
			<< quint64{2} << false << quint64{0};
		QTest::newRow("multiplication overflow") << static_cast<quint64>(cap)
			<< static_cast<quint64>(std::numeric_limits<std::size_t>::max() / 2 + 1)
			<< false << quint64{0};
		QTest::newRow("cap+1") << static_cast<quint64>(cap + 1) << quint64{2} << false
			<< quint64{0};
		QTest::newRow("cap") << static_cast<quint64>(cap) << quint64{2} << true
			<< static_cast<quint64>(cap * 2);
		QTest::newRow("1000") << quint64{1000} << quint64{2} << true << quint64{2000};
	}

	void sourceMixSampleCountIsBounded()
	{
		QFETCH(quint64, frames);
		QFETCH(quint64, channels);
		QFETCH(bool, accepted);
		QFETCH(quint64, expectedCount);

		std::size_t count = 0;
		const bool result = MasteringJob::checkedSampleCount(
			static_cast<std::size_t>(frames), static_cast<std::size_t>(channels), &count);
		if (accepted)
		{
			QVERIFY2(result,
				qPrintable(QStringLiteral("%1 frames were refused").arg(frames)));
			QCOMPARE(static_cast<quint64>(count), expectedCount);
			return;
		}
		QVERIFY2(!result,
			qPrintable(QStringLiteral("%1 frames were accepted").arg(frames)));
	}
private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(MasteringSourceMixTest)
#include "MasteringTestSourceMix.moc"
