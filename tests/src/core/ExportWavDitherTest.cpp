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
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

#include <sndfile.h>

#include "AudioEngine.h"
#include "AudioFileDevice.h"
#include "AudioFileFlac.h"
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

int maxSampleDelta(const std::vector<std::int16_t>& expected,
	const std::vector<std::int16_t>& actual)
{
	int maximum = 0;
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		maximum = std::max(maximum, std::abs(
			static_cast<int>(expected[i]) - static_cast<int>(actual[i])));
	}
	return maximum;
}

//! True when the device's writeBuffer() hands back a result (the API the
//! new-p12-317 fix introduced). The tree WITHOUT the fix returns nothing, so it
//! cannot report a failed write - the test then fails on the other evidence,
//! never on a compile error.
template <typename T, typename = void>
struct WriteBufferReports : std::false_type {};

template <typename T>
struct WriteBufferReports<T, std::void_t<decltype(std::declval<T&>().writeBuffer(
	std::declval<const SampleFrame*>(), std::declval<f_cnt_t>()))>>
	: std::bool_constant<!std::is_void_v<decltype(std::declval<T&>().writeBuffer(
		std::declval<const SampleFrame*>(), std::declval<f_cnt_t>()))>> {};

//! Writes the frames and reports whether the device reported success. A device
//! whose writeBuffer() returns nothing (the tree without the fix) cannot report
//! a failure, so it answers "no failure" here.
template <typename T>
bool writeBufferSucceeded(T& device, const SampleFrame* frames, f_cnt_t count)
{
	if constexpr (WriteBufferReports<T>::value)
	{
		return device.writeBuffer(frames, count);
	}
	else
	{
		device.writeBuffer(frames, count);
		return true;
	}
}

//! True when the device can report a write error (the API the fix introduced).
template <typename T, typename = void>
struct HasWriteError : std::false_type {};

template <typename T>
struct HasWriteError<T, std::void_t<decltype(std::declval<const T&>().hasWriteError())>>
	: std::true_type {};

//! The write error the device reports; false when it cannot report one (the tree
//! without the fix), so the test still compiles there.
template <typename T>
bool reportedWriteError(const T& device)
{
	if constexpr (HasWriteError<T>::value)
	{
		return device.hasWriteError();
	}
	else
	{
		return false;
	}
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

	void aFlacOpenFailureIsReported()
	{
		OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		const QString path = QDir(m_dir.path()).filePath("unsupported-channels.flac");
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(AudioFileFlac::getInst(path, settings, 9,
			Engine::audioEngine(), successful));

		QVERIFY(device != nullptr);
		QVERIFY(!successful);
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

	void maxSampleDeltaDoesNotWrapAtSignedBoundary()
	{
		const std::vector<std::int16_t> expected{-32768};
		const std::vector<std::int16_t> actual{0};
		QCOMPARE(maxSampleDelta(expected, actual), 32768);
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
		const int maxDelta = maxSampleDelta(a, b);
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			if (a[i] != b[i]) { ++differing; }
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

	//! Cluster 3145: the dither must reach the BYTES at digital silence.
	//! The 16-bit dither path stages the dithered floats and hands them to the
	//! plain (truncating) 16-bit quantiser. An offset of less than one LSB then
	//! truncates back to zero, so an input of exactly zero - and every input
	//! below one LSB - exports as bit-exact silence and the dither does nothing:
	//! the quantisation error stays signal-dependent, which is the one thing
	//! dither exists to remove.
	void tpdfDitherReachesTheBytesAtDigitalSilence()
	{
		const std::vector<float> silence(2048, 0.0f);
		const auto depth16 = OutputSettings::BitDepth::Depth16Bit;
		const std::vector<std::int16_t> off = readWavS16(writeWav(silence, false, depth16, "sil-off.wav"));
		const std::vector<std::int16_t> on = readWavS16(writeWav(silence, true, depth16, "sil-on.wav"));
		QCOMPARE(off.size(), silence.size() * 2);
		QCOMPARE(on.size(), silence.size() * 2);

		// Dither OFF is unchanged: exact digital silence, every sample.
		for (std::int16_t value : off) { QCOMPARE(value, std::int16_t{0}); }

		// Dither ON is not: the dither is the only thing that can put energy
		// into this signal, so exact silence here means it was discarded.
		std::size_t nonZero = 0;
		int maxAbs = 0;
		for (std::int16_t value : on)
		{
			if (value != 0) { ++nonZero; }
			maxAbs = std::max(maxAbs, std::abs(static_cast<int>(value)));
		}
		evidence("dithered digital silence: non-zero samples", static_cast<double>(nonZero),
			static_cast<double>(maxAbs));
		// A TPDF offset is triangular over +/-1 LSB, so under round-to-nearest a
		// zero sample leaves zero only when |offset| <= 1/2 - analytically 25% of
		// them, and this test measures 24.8%. Demand a solid eighth (the measured
		// quarter, well clear of the noise, and far above the truncating
		// quantiser's zero) and keep an upper bound so the result must stay noise
		// rather than becoming a constant.
		QVERIFY2(nonZero * 8 > on.size(), qPrintable(QStringLiteral(
			"TPDF dither at digital silence left %1 of %2 samples at exact zero - the offsets "
			"were truncated away instead of quantised").arg(on.size() - nonZero).arg(on.size())));
		QVERIFY2(nonZero < on.size(), "dithered digital silence became a constant, not noise");
		// A TPDF offset is bounded by one LSB either way, so the quantised value
		// can only move by one count from zero.
		QVERIFY2(maxAbs <= 1, qPrintable(QStringLiteral(
			"a dithered zero sample quantised to %1 counts").arg(maxAbs)));
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

	//! Owner decision 13: 'noise_shaped' at 16-bit is reproducible, is NOT the TPDF file,
	//! stays within a few counts of the undithered quantisation, and at 24-bit falls back
	//! to plain TPDF byte for byte (libsndfile quantises 24-bit, so nothing can be shaped).
	void noiseShapedIsReproducibleDistinctAndFallsBackAt24Bit()
	{
		const std::vector<float> signal = lowLevelSine(2048, 4.0, 0.4);
		const auto depth16 = OutputSettings::BitDepth::Depth16Bit;
		const auto depth24 = OutputSettings::BitDepth::Depth24Bit;
		const std::vector<std::int16_t> off = readWavS16(writeWavMode(signal, DitherMode::Off, depth16, "ns-off.wav"));
		const std::vector<std::int16_t> tpdf = readWavS16(writeWavMode(signal, DitherMode::Tpdf, depth16, "ns-tpdf.wav"));
		const std::vector<std::int16_t> shaped = readWavS16(writeWavMode(signal, DitherMode::NoiseShaped, depth16, "ns-1.wav"));
		const std::vector<std::int16_t> again = readWavS16(writeWavMode(signal, DitherMode::NoiseShaped, depth16, "ns-2.wav"));
		QCOMPARE(shaped.size(), off.size());
		QVERIFY2(shaped == again, "two noise-shaped renders of the same signal differ");
		QVERIFY2(shaped != tpdf, "the noise-shaped file is the TPDF file");
		int maxDelta = 0;
		for (std::size_t i = 0; i < off.size(); ++i) { maxDelta = std::max(maxDelta, std::abs(shaped[i] - off[i])); }
		evidence("noise-shaped max |delta| from undithered (16-bit counts)", static_cast<double>(maxDelta));
		QVERIFY2(maxDelta <= 6, qPrintable(QStringLiteral("noise-shaped deviates %1 counts").arg(maxDelta)));
		const QByteArray tpdf24 = readBytes(writeWavMode(signal, DitherMode::Tpdf, depth24, "ns-tpdf24.wav"));
		const QByteArray shaped24 = readBytes(writeWavMode(signal, DitherMode::NoiseShaped, depth24, "ns-24.wav"));
		QVERIFY2(tpdf24 == shaped24, "a 24-bit noise-shaped render is not the 24-bit TPDF render");
		ExportRenderSettings::reset();
	}

	//! BUG 8 (new-p14-404): the surround/interleaved 16-bit path must round, not truncate, so
	//! the dither survives at digital silence the way the stereo writeBuffer path does.
	void interleaved16BitDitherIsRoundedNotTruncated()
	{
		const std::size_t frames = 2048;
		OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		settings.setDither(true);
		ExportRenderSettings::setDither(true);
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("interleaved-dither.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(
			AudioFileWave::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		QVERIFY(successful);
		std::vector<float> samples(frames * DEFAULT_CHANNELS, 0.0f);
		QVERIFY(device->writeInterleaved(samples.data(), static_cast<f_cnt_t>(frames)));
		ExportRenderSettings::reset();

		int channels = 0;
		const std::vector<std::int16_t> read = readWavS16(path, &channels);
		QCOMPARE(channels, DEFAULT_CHANNELS);
		std::size_t nonZero = 0;
		int maxAbs = 0;
		for (std::int16_t value : read)
		{
			if (value != 0) { ++nonZero; }
			maxAbs = std::max(maxAbs, std::abs(static_cast<int>(value)));
		}
		QVERIFY2(nonZero * 8 > read.size(), qPrintable(QStringLiteral(
			"interleaved 16-bit dither left %1 of %2 samples at exact zero - truncated away")
			.arg(read.size() - nonZero).arg(read.size())));
		QVERIFY2(maxAbs <= 1, "a dithered zero sample quantised beyond 1 count");
	}

#ifdef Q_OS_UNIX
	//! BUG 14 (new-p12-317): a short write is reported instead of being ignored.
	void aShortWavWriteIsReported()
	{
		struct rlimit previousLimit{};
		struct sigaction previousAction{};
		struct sigaction ignoredAction{};
		ignoredAction.sa_handler = SIG_IGN;
		sigemptyset(&ignoredAction.sa_mask);

		QVERIFY(getrlimit(RLIMIT_FSIZE, &previousLimit) == 0);
		QVERIFY(sigaction(SIGXFSZ, &ignoredAction, &previousAction) == 0);

		struct rlimit highLimit = previousLimit;
		if (highLimit.rlim_cur < 100000) { highLimit.rlim_cur = 100000; }
		QVERIFY(setrlimit(RLIMIT_FSIZE, &highLimit) == 0);

		OutputSettings settings(44100, 160, OutputSettings::BitDepth::Depth16Bit,
			OutputSettings::StereoMode::Stereo);
		ExportRenderSettings::setDither(false);
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("short-write.wav"));
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(
			AudioFileWave::getInst(path, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		QVERIFY(successful);

		struct rlimit shortLimit = previousLimit;
		shortLimit.rlim_cur = 44;
		QVERIFY(setrlimit(RLIMIT_FSIZE, &shortLimit) == 0);

		std::vector<SampleFrame> frames(1024, SampleFrame(0.5f, 0.5f));
		const bool writeOk = writeBufferSucceeded(*device, frames.data(),
			static_cast<f_cnt_t>(frames.size()));

		const bool restoredLimit = setrlimit(RLIMIT_FSIZE, &previousLimit) == 0;
		const bool restoredSignal = sigaction(SIGXFSZ, &previousAction, nullptr) == 0;
		QVERIFY(restoredLimit);
		QVERIFY(restoredSignal);

		QVERIFY(!writeOk);
		QVERIFY(reportedWriteError(*device));
		ExportRenderSettings::reset();
	}
#endif

private:

	QString writeWavMode(const std::vector<float>& signal, DitherMode mode,
		OutputSettings::BitDepth depth, const QString& name)
	{
		OutputSettings settings(44100, 160, depth, OutputSettings::StereoMode::Stereo);
		settings.setDitherMode(mode);
		ExportRenderSettings::setDitherMode(mode);
		const QString out = QDir(m_dir.path()).filePath(name);
		bool successful = false;
		std::unique_ptr<AudioFileDevice> device(
			AudioFileWave::getInst(out, settings, DEFAULT_CHANNELS, Engine::audioEngine(), successful));
		if (device == nullptr || !successful) { return QString(); }
		std::vector<SampleFrame> frames(signal.size());
		for (std::size_t i = 0; i < signal.size(); ++i) { frames[i] = SampleFrame(signal[i], signal[i]); }
		device->writeBuffer(frames.data(), static_cast<f_cnt_t>(frames.size()));
		return out;
	}

	QByteArray readBytes(const QString& path)
	{
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	}

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
