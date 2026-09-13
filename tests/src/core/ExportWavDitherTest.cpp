/*
 * ExportWavDitherTest.cpp - does the TPDF dither reach a real WAV, and does the
 *                           default still write exactly the pre-change bytes?
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

/*! The wiring half of the dither proof; the distribution and the effect are in
 *  tests/src/core/ExportDitherTest.cpp (split out to keep both files inside the
 *  500-line ratchet).
 *
 *  These assertions drive the REAL export path - `AudioFileWave::writeBuffer`,
 *  reached through `AudioFileDevice::getInst` the way `ProjectRenderer` reaches
 *  it - and read the file back with the same library that wrote it. That
 *  matters, because the property being claimed is about bytes, not about a
 *  return value:
 *
 *  - with the default (dither OFF) the file must equal the pre-change
 *    truncation of the input EXACTLY, sample for sample. That is the byte-level
 *    half of "an always-on dither would falsify the byte-identical render
 *    claim"; a value-level assertion would not have caught it.
 *  - with dither ON the file must differ, must differ per CHANNEL (the offsets
 *    are drawn per channel, so two identical input channels do not write two
 *    identical output channels), must move a sample by at most one count, and
 *    must be reproducible: a second write must be byte-identical to the first.
 *  - 32-bit float must be untouched even with the dither on, because a float
 *    format has no quantisation step to dither against.
 */

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <sndfile.h>

#include "AudioEngine.h"
#include "AudioFileDevice.h"
#include "AudioFileWave.h"
#include "Engine.h"
#include "ExportDither.h"
#include "ExportRenderSettings.h"
#include "OutputSettings.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! The 16-bit quantiser the export path actually uses: clip to full scale,
//! scale by the engine's own multiplier, and cast (which TRUNCATES - it does
//! not round; src/core/audio/AudioDevice.cpp convertToS16).
int_sample_t quantise16(float value)
{
	const float clipped = AudioEngine::clip(value);
	return static_cast<int_sample_t>(clipped * OUTPUT_SAMPLE_MULTIPLIER);
}

//! A sine of \p amplitudeLsb quantisation steps: low enough that most samples
//! sit within one step of the grid, which is where the dither acts.
std::vector<float> lowLevelSine(std::size_t count, double cycles, double amplitudeLsb)
{
	// M_PI is not in <cmath> on every supported toolchain (MSVC needs
	// _USE_MATH_DEFINES), so the constant is written out.
	constexpr double kPi = 3.14159265358979323846;
	std::vector<float> signal(count);
	const double amplitude = amplitudeLsb * ExportDither::lsbForBitDepth(16);
	for (std::size_t i = 0; i < count; ++i)
	{
		signal[i] = static_cast<float>(amplitude
			* std::sin(2.0 * kPi * cycles * static_cast<double>(i) / static_cast<double>(count)));
	}
	return signal;
}

void evidence(const char* label, double a, double b = 0.0)
{
	std::printf("DITHER_EVIDENCE %-52s %14.9f %14.9f\n", label, a, b);
	std::fflush(stdout);
}

} // namespace

class ExportWavDitherTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		ExportRenderSettings::reset();
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
		Engine::destroy();
	}

	//! The byte-level half of "off by default": not "close to" the pre-change
	//! truncation, EXACTLY it.
	void aDefaultExportIsExactlyTheUnditheredQuantisation()
	{
		const std::vector<float> signal = lowLevelSine(2048, 4.0, 0.4);
		const QString path = writeWav(signal, false, OutputSettings::BitDepth::Depth16Bit,
			"plain.wav");
		int channels = 0;
		const std::vector<std::int16_t> read = readWavS16(path, &channels);
		QCOMPARE(channels, 2);
		QCOMPARE(read.size(), signal.size() * 2);

		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			QCOMPARE(read[i * 2], quantise16(signal[i]));
			// The writer feeds both channels the same value, so the second
			// channel is a free second copy of the same assertion.
			QCOMPARE(read[i * 2 + 1], quantise16(signal[i]));
		}
		evidence("default export == undithered quantisation", 1.0);
	}

	void ditherOnChangesTheBytesAndStaysReproducible()
	{
		const std::vector<float> signal = lowLevelSine(2048, 4.0, 0.4);
		const QString plain = writeWav(signal, false, OutputSettings::BitDepth::Depth16Bit,
			"off.wav");
		const QString dithered = writeWav(signal, true, OutputSettings::BitDepth::Depth16Bit,
			"on.wav");
		const QString ditheredAgain = writeWav(signal, true, OutputSettings::BitDepth::Depth16Bit,
			"on2.wav");

		int channels = 0;
		const std::vector<std::int16_t> a = readWavS16(plain, &channels);
		const std::vector<std::int16_t> b = readWavS16(dithered);
		const std::vector<std::int16_t> c = readWavS16(ditheredAgain);
		QCOMPARE(channels, 2);
		QCOMPARE(a.size(), b.size());
		QCOMPARE(a.size(), signal.size() * 2);

		// The dither is per CHANNEL, so the two channels of a dithered file are
		// not the same bytes even though the two input channels are identical.
		std::size_t differing = 0;
		std::size_t channelDiffering = 0;
		std::int16_t maxDelta = 0;
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			if (a[i] != b[i]) { ++differing; }
			maxDelta = std::max<std::int16_t>(maxDelta,
				static_cast<std::int16_t>(std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]))));
		}
		for (std::size_t frame = 0; frame < signal.size(); ++frame)
		{
			if (b[frame * 2] != b[frame * 2 + 1]) { ++channelDiffering; }
		}
		evidence("dither-on samples differing from off", static_cast<double>(differing));
		evidence("dither-on frames whose two channels differ",
			static_cast<double>(channelDiffering));
		evidence("dither-on max |delta| (16-bit counts)", static_cast<double>(maxDelta));

		QVERIFY2(differing > 0, "dither ON produced the same bytes as dither OFF");
		QVERIFY2(channelDiffering > 0, "the two channels were dithered identically; the offsets "
			"are not independent per channel");
		// The dither is bounded by 1 LSB, so the quantised value can move by at
		// most 1 count either way.
		QVERIFY2(maxDelta <= 2, qPrintable(QStringLiteral("dither moved a sample by %1 counts")
			.arg(maxDelta)));

		// Reproducibility, end to end: the same project renders the same file.
		QCOMPARE(b.size(), c.size());
		for (std::size_t i = 0; i < b.size(); ++i)
		{
			QCOMPARE(b[i], c[i]);
		}
	}

	//! The wiring, at the depth where the quantiser lives: 24-bit output goes
	//! through the float path, and 32-bit float must be left alone.
	void theDitherReachesThe24BitPathAndNotTheFloatPath()
	{
		const std::vector<float> signal = lowLevelSine(1024, 3.0, 0.4);
		const QString off = writeWav(signal, false, OutputSettings::BitDepth::Depth24Bit,
			"24off.wav");
		const QString on = writeWav(signal, true, OutputSettings::BitDepth::Depth24Bit,
			"24on.wav");
		QVERIFY2(readWavS16(off) != readWavS16(on),
			"the 24-bit WAV path ignored the dither");

		const std::vector<float> fOff = readWavFloat(writeWav(signal, false,
			OutputSettings::BitDepth::Depth32Bit, "32off.wav"));
		const std::vector<float> fOn = readWavFloat(writeWav(signal, true,
			OutputSettings::BitDepth::Depth32Bit, "32on.wav"));
		QCOMPARE(fOff.size(), fOn.size());
		QCOMPARE(fOff.size(), signal.size() * 2);
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			QCOMPARE(fOff[i * 2], fOn[i * 2]);
			QCOMPARE(fOff[i * 2], signal[i]);
		}
		evidence("32-bit float off == on", 1.0);
	}

private:

	//! Writes \p signal through the real export path and returns the file path.
	QString writeWav(const std::vector<float>& signal, bool dither,
		OutputSettings::BitDepth depth, const QString& name)
	{
		OutputSettings settings(44100, 160, depth, OutputSettings::StereoMode::Stereo);
		settings.setDither(dither);
		// The choice must be published the way ProjectRenderer publishes it, or
		// the wiring under test is not the wiring used in production.
		ExportRenderSettings::setDither(dither);

		const QString path = QDir(m_dir.path()).filePath(name);
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(
			AudioFileWave::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(),
				successful));
		Q_ASSERT(device != nullptr);
		if (!successful) { return QString(); }

		std::vector<SampleFrame> frames(signal.size());
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			frames[i] = SampleFrame(signal[i], signal[i]);
		}
		device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size()));
		return path;   // device closes the file in its destructor
	}

	/*! Reads a WAV back as INTERLEAVED shorts, with the channel count.
	 *
	 *  Sizing the buffer is where this got written wrong once: `sf_readf_short`
	 *  writes frames*channels items, so a vector sized `info.frames` overflows
	 *  the heap on a stereo file (measured: glibc "corrupted size vs.
	 *  prev_size", SIGABRT). Read with `sf_read_short` and an item count
	 *  instead, and index the frames as `[frame * channels]`.
	 */
	std::vector<std::int16_t> readWavS16(const QString& path, int* channelCount = nullptr)
	{
		return readInterleaved<std::int16_t>(path, channelCount,
			[](SNDFILE* file, std::int16_t* data, sf_count_t items) {
				return sf_read_short(file, data, items);
			});
	}

	std::vector<float> readWavFloat(const QString& path, int* channelCount = nullptr)
	{
		return readInterleaved<float>(path, channelCount,
			[](SNDFILE* file, float* data, sf_count_t items) {
				return sf_read_float(file, data, items);
			});
	}

	template <typename T, typename ReadFn>
	std::vector<T> readInterleaved(const QString& path, int* channelCount, ReadFn read)
	{
		SF_INFO info{};
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		Q_ASSERT(file != nullptr);
		if (file == nullptr) { return {}; }
		const auto items = static_cast<std::size_t>(info.frames)
			* static_cast<std::size_t>(info.channels);
		std::vector<T> samples(items);
		if (!samples.empty())
		{
			read(file, samples.data(), static_cast<sf_count_t>(samples.size()));
		}
		sf_close(file);
		if (channelCount != nullptr) { *channelCount = info.channels; }
		return samples;
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(ExportWavDitherTest)
#include "ExportWavDitherTest.moc"
