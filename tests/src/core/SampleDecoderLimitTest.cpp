/*
 * SampleDecoderLimitTest.cpp - the bounded decoder refuses a limit it cannot turn into frames
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

#include <exception>
#include <limits>
#include <sndfile.h>
#include <vector>

#include <QFile>
#include <QTemporaryDir>

#include "SampleDecoder.h"

using namespace lmms;

namespace
{

constexpr int Rate = 44100;
constexpr int Wav = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

//! Writes @a frames of a mono tone.
bool writeTone(const QString& path, int format, sf_count_t frames)
{
	SF_INFO info{};
	info.samplerate = Rate;
	info.channels = 1;
	info.format = format;
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	std::vector<float> chunk(1024, 0.5f);
	for (sf_count_t done = 0; done < frames;)
	{
		const sf_count_t n = std::min<sf_count_t>(chunk.size(), frames - done);
		sf_writef_float(file, chunk.data(), n);
		done += n;
	}
	sf_close(file);
	return true;
}

} // namespace

class SampleDecoderLimitTest : public QObject
{
	Q_OBJECT
private:
	QTemporaryDir m_dir;
	QString path(const QString& name) const { return m_dir.filePath(name); }

private slots:
	void initTestCase() { QVERIFY(m_dir.isValid()); }

	//! The control: an ordinary window still decodes the file's leading window.
	void ordinaryLimitStillDecodesTheWindow()
	{
		const QString file = path(QStringLiteral("tone.wav"));
		QVERIFY(writeTone(file, Wav, Rate));
		const auto bounded = SampleDecoder::decodeLimited(file, 0.5);
		QVERIFY(bounded.has_value());
		QCOMPARE(bounded->window.sampleRate, Rate);
		QCOMPARE(bounded->window.data.size(), static_cast<std::size_t>(Rate / 2));
		QCOMPARE(bounded->totalFrames, static_cast<std::int64_t>(Rate));
	}

	//! A limit beyond the supported maximum is refused instead of being converted out of range:
	//! the frame count would not fit the integer the decoder converts it into.
	void limitBeyondTheSupportedMaximumIsRefused()
	{
		const QString file = path(QStringLiteral("huge.wav"));
		QVERIFY(writeTone(file, Wav, Rate));
		std::optional<SampleDecoder::BoundedResult> bounded;
		try
		{
			bounded = SampleDecoder::decodeLimited(file, 1.0e12);
		}
		catch (const std::exception& e)
		{
			QFAIL(qPrintable(QStringLiteral("the limit was converted out of range: %1").arg(QString::fromUtf8(e.what()))));
		}
		QVERIFY(!bounded.has_value());
	}

	//! An infinite limit is refused: it is not a frame count at all.
	void infiniteLimitIsRefused()
	{
		const QString file = path(QStringLiteral("infinite.wav"));
		QVERIFY(writeTone(file, Wav, Rate));
		std::optional<SampleDecoder::BoundedResult> bounded;
		try
		{
			bounded = SampleDecoder::decodeLimited(file, std::numeric_limits<double>::infinity());
		}
		catch (const std::exception& e)
		{
			QFAIL(qPrintable(QStringLiteral("the infinite limit was converted out of range: %1").arg(QString::fromUtf8(e.what()))));
		}
		QVERIFY(!bounded.has_value());
	}
};

QTEST_MAIN(SampleDecoderLimitTest)
#include "SampleDecoderLimitTest.moc"
