/*
 * SampleDecoderOggChannelTest.cpp - the Ogg Vorbis decoder keeps both channels
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

#include <cmath>
#include <sndfile.h>
#include <vector>

#include <QFile>
#include <QTemporaryDir>

#include "SampleDecoder.h"

using namespace lmms;

namespace
{

constexpr int Rate = 44100;
constexpr int Ogg = SF_FORMAT_OGG | SF_FORMAT_VORBIS;

//! The levels the stereo fixture carries: the left channel and the right channel differ, so a
//! decoder that mixes them up (or reads the right channel out of the left one) cannot pass.
constexpr float LeftLevel = 0.5f;
constexpr float RightLevel = -0.25f;

//! Writes @a frames of a stereo signal with two different constant levels.
bool writeStereoLevels(const QString& path, int format, sf_count_t frames)
{
	SF_INFO info{};
	info.samplerate = Rate;
	info.channels = 2;
	info.format = format;
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	std::vector<float> chunk(1024 * 2, 0.0f);
	for (sf_count_t done = 0; done < frames;)
	{
		const sf_count_t n = std::min<sf_count_t>(chunk.size() / 2, frames - done);
		for (sf_count_t i = 0; i < n; ++i)
		{
			chunk[static_cast<std::size_t>(i) * 2] = LeftLevel;
			chunk[static_cast<std::size_t>(i) * 2 + 1] = RightLevel;
		}
		sf_writef_float(file, chunk.data(), n);
		done += n;
	}
	sf_close(file);
	return true;
}

} // namespace

class SampleDecoderOggChannelTest : public QObject
{
	Q_OBJECT
private:
	QTemporaryDir m_dir;
	QString path(const QString& name) const { return m_dir.filePath(name); }

private slots:
	void initTestCase() { QVERIFY(m_dir.isValid()); }

	//! A stereo Ogg decodes to one frame per decoded sample, with the left and the right channel in
	//! their own places: ov_read_float hands back PLANAR data, so the frames are built from
	//! output[0][i] and output[1][i] - not by indexing one channel as if it were interleaved.
	void stereoOggKeepsBothChannels()
	{
		const QString file = path(QStringLiteral("stereo.ogg"));
		if (!writeStereoLevels(file, Ogg, Rate)) { QSKIP("this libsndfile cannot write Ogg Vorbis"); }

		const auto decoded = SampleDecoder::decodeOggVorbisOnlyForTesting(file);
		QVERIFY(decoded.has_value());
		QCOMPARE(decoded->sampleRate, Rate);

		// One frame per decoded sample: the decoder must not halve the length.
		QVERIFY2(decoded->data.size() >= static_cast<std::size_t>(Rate) - 1024,
			"the stereo file decodes to its full length");
		QVERIFY2(decoded->data.size() <= static_cast<std::size_t>(Rate) + 4096,
			"the stereo file decodes to its full length");

		for (std::size_t i = 0; i < decoded->data.size(); i += 97)
		{
			QVERIFY2(std::abs(decoded->data[i].left() - LeftLevel) < 0.05f, "the left channel is the left channel");
			QVERIFY2(std::abs(decoded->data[i].right() - RightLevel) < 0.05f, "the right channel is the right channel");
		}
	}

	//! The same file through libsndfile decodes to the same frames: the Ogg decoder is not allowed
	//! to lose half of them or to mix the channels up.
	void stereoOggMatchesTheSndfileDecoder()
	{
		const QString file = path(QStringLiteral("both.ogg"));
		if (!writeStereoLevels(file, Ogg, Rate)) { QSKIP("this libsndfile cannot write Ogg Vorbis"); }

		const auto viaSndfile = SampleDecoder::decode(file);
		const auto viaOgg = SampleDecoder::decodeOggVorbisOnlyForTesting(file);
		QVERIFY(viaSndfile.has_value());
		QVERIFY(viaOgg.has_value());
		QVERIFY2(std::abs(static_cast<long long>(viaSndfile->data.size()) - static_cast<long long>(viaOgg->data.size())) <= 1024,
			"both decoders return the same number of frames");
		const std::size_t frames = std::min(viaSndfile->data.size(), viaOgg->data.size());
		for (std::size_t i = 0; i < frames; i += 97)
		{
			QVERIFY2(std::abs(viaOgg->data[i].left() - viaSndfile->data[i].left()) < 0.02f, "the same left samples");
			QVERIFY2(std::abs(viaOgg->data[i].right() - viaSndfile->data[i].right()) < 0.02f, "the same right samples");
		}
	}
};

QTEST_MAIN(SampleDecoderOggChannelTest)
#include "SampleDecoderOggChannelTest.moc"
