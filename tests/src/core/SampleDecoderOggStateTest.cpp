/*
 * SampleDecoderOggStateTest.cpp - the Ogg Vorbis decoder releases its state on every exit
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
			chunk[static_cast<std::size_t>(i) * 2] = 0.5f;
			chunk[static_cast<std::size_t>(i) * 2 + 1] = -0.25f;
		}
		sf_writef_float(file, chunk.data(), n);
		done += n;
	}
	sf_close(file);
	return true;
}

} // namespace

class SampleDecoderOggStateTest : public QObject
{
	Q_OBJECT
private:
	QTemporaryDir m_dir;
	QString path(const QString& name) const { return m_dir.filePath(name); }

private slots:
	void initTestCase() { QVERIFY(m_dir.isValid()); }

	//! Every exit from the Ogg decoder releases libvorbisfile's decoder state - the read-error path
	//! included: a file that fails while decoding must not leak the state it opened. The
	//! file-close callback is what libvorbisfile calls when the state is released (ov_clear), so
	//! the callback count is the number of decoder states that were released.
	void readErrorPathReleasesTheDecoderState()
	{
		const QString file = path(QStringLiteral("error.ogg"));
		if (!writeStereoLevels(file, Ogg, Rate)) { QSKIP("this libsndfile cannot write Ogg Vorbis"); }

		// The control: the successful path releases the state exactly once.
		SampleDecoder::resetOggCloseCountForTesting();
		const auto ok = SampleDecoder::decodeOggVorbisOnlyForTesting(file);
		QVERIFY(ok.has_value());
		QCOMPARE(SampleDecoder::oggCloseCountForTesting(), 1);

		// The error path: the state must be released there as well.
		SampleDecoder::resetOggCloseCountForTesting();
		SampleDecoder::failNextOggReadForTesting();
		const auto failed = SampleDecoder::decodeOggVorbisOnlyForTesting(file);
		QVERIFY(!failed.has_value());
		QCOMPARE(SampleDecoder::oggCloseCountForTesting(), 1);
	}
};

QTEST_MAIN(SampleDecoderOggStateTest)
#include "SampleDecoderOggStateTest.moc"
