/*
 * BrowserFeaturesWindowTest.cpp - browser feature analysis decodes only its window (Q050-048)
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

#include <cmath>
#include <numbers>
#include <sndfile.h>
#include <vector>

#include <QFile>
#include <QTemporaryDir>

#include "BrowserIndex.h"
#include "SampleDecoder.h"

using namespace lmms;

namespace
{

constexpr int Rate = 22050;

//! Writes @a seconds of a 440 Hz tone, mono, in the libsndfile format @a format (written in chunks).
bool writeTone(const QString& path, int format, double seconds)
{
	SF_INFO info{};
	info.samplerate = Rate;
	info.channels = 1;
	info.format = format;
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	const sf_count_t total = static_cast<sf_count_t>(seconds * Rate);
	std::vector<float> chunk(Rate);
	for (sf_count_t done = 0; done < total;)
	{
		const sf_count_t n = std::min<sf_count_t>(chunk.size(), total - done);
		for (sf_count_t i = 0; i < n; ++i)
		{
			chunk[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(done + i) / Rate));
		}
		sf_writef_float(file, chunk.data(), n);
		done += n;
	}
	sf_close(file);
	return true;
}

constexpr int Wav = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
constexpr int Flac = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
constexpr int Ogg = SF_FORMAT_OGG | SF_FORMAT_VORBIS;

} // namespace

class BrowserFeaturesWindowTest : public QObject
{
	Q_OBJECT
private:
	QTemporaryDir m_dir;
	QString path(const QString& name) const { return m_dir.filePath(name); }
	static std::size_t windowFrames() { return static_cast<std::size_t>(BrowserFeatures::FeatureSeconds * Rate); }

private slots:
	void initTestCase() { QVERIFY(m_dir.isValid()); }

	//! Acceptance 1 and 2: a 10-minute file decodes only the leading window yet reports its full length.
	void longFileDecodesOnlyTheWindow_data()
	{
		QTest::addColumn<int>("format");
		QTest::addColumn<QString>("name");
		QTest::newRow("wav") << Wav << QStringLiteral("long.wav");
		QTest::newRow("flac") << Flac << QStringLiteral("long.flac");
		QTest::newRow("ogg") << Ogg << QStringLiteral("long.ogg");
	}
	void longFileDecodesOnlyTheWindow()
	{
		QFETCH(int, format);
		QFETCH(QString, name);
		constexpr double seconds = 600.0;
		if (!writeTone(path(name), format, seconds)) { QSKIP("this libsndfile cannot write the format"); }

		const auto bounded = SampleDecoder::decodeLimited(path(name), BrowserFeatures::FeatureSeconds);
		QVERIFY(bounded.has_value());
		QCOMPARE(bounded->window.sampleRate, Rate);
		QCOMPARE(bounded->window.data.size(), windowFrames());
		QVERIFY2(bounded->window.data.capacity() <= windowFrames() + 4096, "the frame buffer follows the window");
		// The header's length, to the frame (Vorbis: its last granule position, equal for this encoder).
		QCOMPARE(bounded->totalFrames, static_cast<std::int64_t>(seconds * Rate));

		const BrowserFeatures features = BrowserFeatures::analyse(path(name));
		QVERIFY(features.valid);
		QVERIFY2(std::abs(features.durationSeconds - seconds) < 0.01, "duration is the whole file's");
		QVERIFY(features.loudnessDb > -20.0 && features.loudnessDb < -3.0); // the tone was heard, not silence
	}

	//! Acceptance 3: the buffer is the same size for a 1-minute and a 10-minute file; the full decode is not.
	void peakBufferDoesNotScaleWithFileLength()
	{
		QVERIFY(writeTone(path(QStringLiteral("one.wav")), Wav, 60.0));
		QVERIFY(writeTone(path(QStringLiteral("ten.wav")), Wav, 600.0));
		const auto one = SampleDecoder::decodeLimited(path(QStringLiteral("one.wav")), BrowserFeatures::FeatureSeconds);
		const auto ten = SampleDecoder::decodeLimited(path(QStringLiteral("ten.wav")), BrowserFeatures::FeatureSeconds);
		QVERIFY(one && ten);
		QCOMPARE(one->window.data.capacity(), ten->window.data.capacity());
		QCOMPARE(one->window.data.size(), ten->window.data.size());
		QCOMPARE(one->totalFrames, static_cast<std::int64_t>(60 * Rate));
		QCOMPARE(ten->totalFrames, static_cast<std::int64_t>(600 * Rate));

		// Negative control: the unbounded decoder's buffer is the whole file.
		const auto full = SampleDecoder::decode(path(QStringLiteral("one.wav")));
		QVERIFY(full.has_value());
		QCOMPARE(full->data.size(), static_cast<std::size_t>(60 * Rate));
		QVERIFY(full->data.size() > 2 * one->window.data.size());
	}

	//! A short file is decoded whole, and the bounded and full paths agree sample for sample on the window.
	void shortFileAndSampleParity()
	{
		QVERIFY(writeTone(path(QStringLiteral("short.wav")), Wav, 3.0));
		const auto bounded = SampleDecoder::decodeLimited(path(QStringLiteral("short.wav")), BrowserFeatures::FeatureSeconds);
		const auto full = SampleDecoder::decode(path(QStringLiteral("short.wav")));
		QVERIFY(bounded && full);
		QCOMPARE(bounded->totalFrames, static_cast<std::int64_t>(3 * Rate));
		QCOMPARE(bounded->window.data.size(), full->data.size());
		for (std::size_t i = 0; i < full->data.size(); i += 997)
		{
			QCOMPARE(bounded->window.data[i].left(), full->data[i].left());
			QCOMPARE(bounded->window.data[i].right(), full->data[i].right());
		}
	}

	//! A WAV whose header carries no length (sizes 0xFFFFFFFF, as a streamed writer leaves them) still reports the
	//! true duration: libsndfile derives it from the file, and when it does not, the rest is counted in chunks.
	void headerWithoutLengthStillReportsDuration()
	{
		const QString name = path(QStringLiteral("nolength.wav"));
		QVERIFY(writeTone(name, Wav, 90.0));
		QFile file(name);
		QVERIFY(file.open(QIODevice::ReadWrite));
		const char zero[4] = {char(0xFF), char(0xFF), char(0xFF), char(0xFF)};
		QVERIFY(file.seek(4) && file.write(zero, 4) == 4);   // RIFF size
		QVERIFY(file.seek(40) && file.write(zero, 4) == 4);  // data size (canonical 44-byte header)
		file.close();
		const auto bounded = SampleDecoder::decodeLimited(name, BrowserFeatures::FeatureSeconds);
		QVERIFY(bounded.has_value());
		QCOMPARE(bounded->window.data.size(), windowFrames());
		QCOMPARE(bounded->totalFrames, static_cast<std::int64_t>(90 * Rate));
	}

	void unreadableFileIsRefused()
	{
		QVERIFY(!SampleDecoder::decodeLimited(path(QStringLiteral("absent.wav")), 20.0).has_value());
		QVERIFY(!SampleDecoder::decodeLimited(path(QStringLiteral("long.wav")), 0.0).has_value());
	}
};

QTEST_MAIN(BrowserFeaturesWindowTest)
#include "BrowserFeaturesWindowTest.moc"
