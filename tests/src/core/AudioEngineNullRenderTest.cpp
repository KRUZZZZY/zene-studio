/*
 * AudioEngineNullRenderTest.cpp - an offline render of a unity sample clip at
 *                                 position 0 must BE its source, frame for frame
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
 * You should have received a copy of the GNU General Public License along
 * with this program (if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA)
 *
 */

/*! BUG 1 (cluster null-1): the offline render was exactly ONE SAMPLE LATE.
 *
 *  A 32-bit float WAV on one sample track, the clip at position 0, track volume
 *  100, master at unity, rendered at the source's own rate with `zene render`
 *  semantics (`ProjectRenderer`, Depth32Bit), came out one sample late: after
 *  shifting by one it was bit-exact, and with no shift the residual was about
 *  -2.8 dBFS peak. Two renders were identical, so it was deterministic, not a
 *  race.
 *
 *  This test is the permanent version of that null test, with NO alignment:
 *  frame 0 of the clip is required to be frame 0 of the render, bit for bit, at
 *  48000 Hz and at 44100 Hz. There is no cross-correlation, no lag search and no
 *  tolerance - a render that is one sample late differs at frame 0 and fails.
 *
 *  The negative control is the same project at volume 99 %: it must NOT null, or
 *  the equality above would be measuring nothing. The first frame of the source
 *  is deliberately non-zero, so a one-sample shift is a mismatch at index 0
 *  rather than a run of zeros that could pass unnoticed.
 *
 *  Why one sample: `Sample::play()` always ran the libsamplerate converter, and
 *  the linear converter delays by one sample at ratio 1.0 (it emits the first
 *  input frame twice). Its own probe lives in the fix's log, not here; what this
 *  test pins is the user-visible contract - a unity render at the source's rate
 *  is the source.
 */

#include <QtTest>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioFileWave.h"
#include "AudioResampler.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "Sample.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kSeconds = 0.1;
//! 120 bpm, 48 ticks per beat, 2 beats per second (the fixture project's tempo).
constexpr int kTicksPerSecond = 2 * 48;
//! Track volume 100 is exactly unity; 99 is the negative control.
constexpr int kUnityVolume = 100;
constexpr int kQuietVolume = 99;

void put16(QByteArray& out, quint16 value)
{
	out.append(char(value & 0xff)).append(char((value >> 8) & 0xff));
}

void put32(QByteArray& out, quint32 value)
{
	out.append(char(value & 0xff)).append(char((value >> 8) & 0xff))
		.append(char((value >> 16) & 0xff)).append(char((value >> 24) & 0xff));
}

/*! A deterministic stereo signal whose FIRST frame is non-zero.
 *
 *  The two channels carry different content, so a mono fold, a channel swap or a
 *  pan-law error would show as a mismatch rather than cancel out. Float32 is
 *  written exactly, because the render is 32-bit float: any processing that is
 *  not the identity lands on a different bit pattern. */
std::vector<SampleFrame> makeSource(int rate)
{
	const auto frames = static_cast<std::size_t>(std::lround(kSeconds * rate));
	std::vector<SampleFrame> source(frames);
	for (std::size_t i = 0; i < frames; ++i)
	{
		const double t = static_cast<double>(i) / rate;
		const auto left = static_cast<float>(0.5 * std::sin(2.0 * kPi * 440.0 * t) + 0.25);
		const auto right = static_cast<float>(-0.4 * std::cos(2.0 * kPi * 220.0 * t));
		source[i] = SampleFrame(left, right);
	}
	// Frame 0 must identify the start on its own: the sine above is 0.25 in the
	// left channel already, and the right channel gets a value a shift cannot
	// reproduce.
	source[0] = SampleFrame(0.5f, -0.25f);
	return source;
}

bool writeFloatWav(const QString& path, const std::vector<SampleFrame>& frames, int rate)
{
	const int channels = 2;
	const int bytesPerFrame = channels * 4;
	QByteArray data;
	data.reserve(static_cast<int>(frames.size()) * bytesPerFrame);
	for (const SampleFrame& frame : frames)
	{
		for (int channel = 0; channel < channels; ++channel)
		{
			const float value = frame[channel];
			quint32 bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));
			put32(data, bits);
		}
	}

	QByteArray fmt;
	put16(fmt, 3);                                    // WAVE_FORMAT_IEEE_FLOAT
	put16(fmt, static_cast<quint16>(channels));
	put32(fmt, static_cast<quint32>(rate));
	put32(fmt, static_cast<quint32>(rate * bytesPerFrame));
	put16(fmt, static_cast<quint16>(bytesPerFrame));
	put16(fmt, 32);                                   // bits per sample

	QByteArray header;
	header.append("RIFF");
	put32(header, static_cast<quint32>(4 + 8 + fmt.size() + 8 + data.size()));
	header.append("WAVE");
	header.append("fmt ");
	put32(header, static_cast<quint32>(fmt.size()));
	header.append(fmt);
	header.append("data");
	put32(header, static_cast<quint32>(data.size()));

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	file.write(header + data);
	file.close();
	return true;
}

quint32 le32(const QByteArray& bytes, int at)
{
	return quint32(quint8(bytes[at])) | (quint32(quint8(bytes[at + 1])) << 8)
		| (quint32(quint8(bytes[at + 2])) << 16) | (quint32(quint8(bytes[at + 3])) << 24);
}

quint16 le16(const QByteArray& bytes, int at)
{
	return quint16(quint8(bytes[at])) | (quint16(quint8(bytes[at + 1])) << 8);
}

/*! The frames of a 32-bit float WAV, exactly as written.
 *
 *  Only the format this test exports is accepted; anything else returns an empty
 *  vector, which the cases treat as "the render is broken", never as "the render
 *  matches". */
/*! Parse the WAV's chunk walk into \a format and \a data; false when the layout is not
 *  float stereo 32-bit. Split from the decode so no single helper carries the whole job. */
bool walkFloatChunks(const QByteArray& blob, int* channels, int* sampleRate, int* bits, QByteArray* data)
{
	int at = 12;
	while (at + 8 <= blob.size())
	{
		const QByteArray id = blob.mid(at, 4);
		const quint32 size = le32(blob, at + 4);
		if (id == "fmt ")
		{
			if (size < 16) { return false; }
			if (le16(blob, at + 8) != 3) { return false; }   // not float
			*channels = le16(blob, at + 10);
			*sampleRate = static_cast<int>(le32(blob, at + 12));
			*bits = le16(blob, at + 22);
		}
		else if (id == "data") { *data = blob.mid(at + 8, static_cast<int>(size)); }
		at += 8 + static_cast<int>(size) + static_cast<int>(size & 1);
	}
	return *channels == 2 && *bits == 32 && !data->isEmpty();
}

//! Decode interleaved 32-bit float samples, two channels per frame.
std::vector<SampleFrame> decodeFloatFrames(const QByteArray& data)
{
	std::vector<SampleFrame> frames(static_cast<std::size_t>(data.size()) / 8);
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		for (int channel = 0; channel < 2; ++channel)
		{
			quint32 bitsValue = le32(data, static_cast<int>(i) * 8 + channel * 4);
			float value = 0.0f;
			std::memcpy(&value, &bitsValue, sizeof(value));
			frames[i][channel] = value;
		}
	}
	return frames;
}

std::vector<SampleFrame> readFloatWav(const QString& path, int* rate = nullptr)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	const QByteArray blob = file.readAll();
	if (blob.size() < 44) { return {}; }
	if (blob.mid(0, 4) != "RIFF") { return {}; }
	if (blob.mid(8, 4) != "WAVE") { return {}; }
	int channels = 0;
	int sampleRate = 0;
	int bits = 0;
	QByteArray data;
	if (!walkFloatChunks(blob, &channels, &sampleRate, &bits, &data)) { return {}; }
	if (rate != nullptr) { *rate = sampleRate; }
	return decodeFloatFrames(data);
}

//! The fixture project: one sample track, one clip at position 0, no effects.
QString writeProject(const QString& dir, const QString& wav, int rate, int volume)
{
	const int ticks = static_cast<int>(std::ceil(kSeconds * kTicksPerSecond)) + 48;
	const QString xml = QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<!DOCTYPE multimedia-project>\n"
		"<multimedia-project version=\"1.0\" creator=\"Zene Studio\" creatorversion=\"1.3.0\" type=\"song\">\n"
		"  <head timesig_numerator=\"4\" mastervol=\"100\" timesig_denominator=\"4\" bpm=\"120\" masterpitch=\"0\" />\n"
		"  <song>\n"
		"    <trackcontainer width=\"600\" x=\"5\" y=\"5\" maximized=\"0\" height=\"300\" visible=\"1\" type=\"song\" minimized=\"0\">\n"
		"      <track muted=\"0\" type=\"2\" name=\"Null\">\n"
		"        <sampletrack vol=\"%1\" pan=\"0\" mixch=\"0\">\n"
		"          <fxchain numofeffects=\"0\" enabled=\"0\" />\n"
		"        </sampletrack>\n"
		"        <sampleclip pos=\"0\" len=\"%2\" muted=\"0\" src=\"%3\" off=\"0\" autoresize=\"0\" sample_rate=\"%4\"/>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"    <mixer width=\"865\" x=\"5\" y=\"310\" maximized=\"0\" height=\"278\" visible=\"1\" minimized=\"0\">\n"
		"      <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n"
		"        <fxchain numofeffects=\"0\" enabled=\"0\" />\n"
		"      </mixerchannel>\n"
		"    </mixer>\n"
		"    <timeline lp1pos=\"%2\" lp0pos=\"0\" lpstate=\"0\" />\n"
		"    <controllers />\n"
		"  </song>\n"
		"</multimedia-project>\n")
		.arg(volume).arg(ticks).arg(wav).arg(rate);

	const QString path = QDir(dir).filePath("null.mmp");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return QString(); }
	file.write(xml.toUtf8());
	file.close();
	return path;
}

} // namespace

class AudioEngineNullRenderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		QVERIFY(Engine::audioEngine()->audioDev() != nullptr);
		// The dummy device renders in the background; every case drives the
		// export synchronously, so it must not race the render.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! The contract: at the source's own rate, with the clip at 0 and unity gain,
	//! frame 0 of the render is frame 0 of the source - no alignment, no tolerance.
	void unityClipAtPositionZeroRendersItsSourceBitExactly()
	{
		for (const int rate : {48000, 44100})
		{
			const std::vector<SampleFrame> source = makeSource(rate);
			const std::vector<SampleFrame> rendered = render(rate, kUnityVolume);
			QVERIFY2(rendered.size() >= source.size(),
				qPrintable(QStringLiteral("the %1 Hz render is shorter than its source (%2 < %3 frames)")
					.arg(rate).arg(rendered.size()).arg(source.size())));

			int firstMismatch = -1;
			int mismatches = 0;
			double peakResidual = 0.0;
			for (std::size_t i = 0; i < source.size(); ++i)
			{
				for (int channel = 0; channel < 2; ++channel)
				{
					if (rendered[i][channel] == source[i][channel]) { continue; }
					++mismatches;
					if (firstMismatch < 0) { firstMismatch = static_cast<int>(i); }
					peakResidual = std::max(peakResidual,
						std::fabs(static_cast<double>(rendered[i][channel]) - source[i][channel]));
				}
			}
			QVERIFY2(mismatches == 0,
				qPrintable(QStringLiteral("the %1 Hz unity render is not its source: %2 mismatched samples, "
					"first at frame %3 (render %4/%5, source %6/%7), peak residual %8")
					.arg(rate).arg(mismatches).arg(firstMismatch)
					.arg(firstMismatch >= 0 ? rendered[static_cast<std::size_t>(firstMismatch)][0] : 0.0f)
					.arg(firstMismatch >= 0 ? rendered[static_cast<std::size_t>(firstMismatch)][1] : 0.0f)
					.arg(firstMismatch >= 0 ? source[static_cast<std::size_t>(firstMismatch)][0] : 0.0f)
					.arg(firstMismatch >= 0 ? source[static_cast<std::size_t>(firstMismatch)][1] : 0.0f)
					.arg(peakResidual)));
		}
	}

	//! The negative control: the SAME project at volume 99 % must not null, so the
	//! equality above is a measurement of the gain path and not a constant.
	void ninetyNinePercentVolumeDoesNotNull()
	{
		const int rate = 48000;
		const std::vector<SampleFrame> source = makeSource(rate);
		const std::vector<SampleFrame> rendered = render(rate, kQuietVolume);
		QVERIFY(rendered.size() >= source.size());

		double peakDifference = 0.0;
		double sourcePeak = 0.0;
		for (std::size_t i = 0; i < source.size(); ++i)
		{
			for (int channel = 0; channel < 2; ++channel)
			{
				peakDifference = std::max(peakDifference,
					std::fabs(static_cast<double>(rendered[i][channel]) - source[i][channel]));
				sourcePeak = std::max(sourcePeak, std::fabs(static_cast<double>(source[i][channel])));
			}
		}
		QVERIFY2(sourcePeak > 0.1, "the source is silent, so this control proves nothing");
		QVERIFY2(peakDifference > 1e-4,
			qPrintable(QStringLiteral("volume 99 % rendered the source exactly (peak difference %1) - "
				"the unity case cannot be discriminating").arg(peakDifference)));
	}

	/*! A run that passes THROUGH unity must leave the converter in step with the source
	 *  it has consumed, so the next non-unity output is what it always was.
	 *
	 *  The unity branch replaces the converter's OUTPUT with the source, so it is tempting
	 *  to skip the converter there. That is the round-3 regression this case pins: the
	 *  staging would advance while the converter stayed behind, and the next .5 call would
	 *  resume from stale filter state and a stale input position.
	 *
	 *  The case drives one Sample and one PlaybackState in fixed 256-frame calls - four at
	 *  ratio 0.5, three at 1.0, four at 0.5 again - and pins the whole post-unity 0.5
	 *  output to the value the base implementation produces for that exact sequence. The
	 *  golden checksum below was captured by running this same case against the base
	 *  implementation, so it is the historical behaviour, not a re-implementation of the
	 *  fix. No renderer, no timing. */
	void ratioTransitionsThroughUnityPreserveConverterStateAndCursor()
	{
		// Captured from the base implementation for this exact fixture and call plan.
		constexpr double kHistoricalChecksum = -12.267697812872939;

		const auto engineRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		QVERIFY(engineRate > 0);
		// The source at the engine's own rate, so a ratio argument of 1.0 is exactly the
		// unity case under test and .5 is a genuine 1:2 resample on either side of it.
		const int sourceRate = engineRate;
		const std::vector<SampleFrame> source = transitionSource(sourceRate);
		Sample sample(source.data(), source.size(), sourceRate);

		const CallPlan plan;
		// Pin the converter mode: `modeForCurrentSrcQuality()` follows the process-wide
		// ExportRenderSettings, so an earlier case in this binary (which renders through
		// ProjectRenderer) would otherwise change the mode under this one and with it the
		// checksum. Linear is the shipped default.
		Sample::PlaybackState state(AudioResampler::Mode::Linear);
		state.setFrameIndex(0);
		std::vector<SampleFrame> out(plan.block, SampleFrame{});
		const double checksum = playTransitionPlan(sample, &state, plan, out);

		QVERIFY2(std::fabs(checksum - kHistoricalChecksum) <= 1e-9,
			qPrintable(QStringLiteral("the post-unity output changed: checksum %1, the same sequence "
				"on the base implementation gives %2 - the unity path did not keep the converter in "
				"step with the source")
				.arg(checksum, 0, 'g', 17).arg(kHistoricalChecksum, 0, 'g', 17)));
		QVERIFY2(state.frameIndex() > 0, "the cursor never advanced, so the case proved nothing");
	}

private:
	//! The call plan the transition case drives, and its block size.
	struct CallPlan
	{
		static constexpr std::size_t block = 256;
		static constexpr int beforeCalls = 4;
		static constexpr int unityCalls = 3;
		static constexpr int afterCalls = 4;
		static constexpr int totalCalls = beforeCalls + unityCalls + afterCalls;

		//! Ratio for call @a call: .5 before and after the unity span, 1.0 inside it.
		double ratioAt(int call) const
		{
			return call < beforeCalls || call >= beforeCalls + unityCalls ? 0.5 : 1.0;
		}
		bool afterUnity(int call) const { return call >= beforeCalls + unityCalls; }
	};

	/*! A one-second source at \a rate: two tones, distinct channels, non-zero start.
	 *
	 *  The tone phase is computed from the FRAME INDEX at a fixed reference rate, not from
	 *  the sample rate, so the same content is produced whatever the engine's rate is: the
	 *  checksum this case pins then does not depend on which rate an earlier case in the
	 *  binary happened to leave the engine on. The converter's behaviour depends only on
	 *  the ratio and the content, both of which are fixed.
	 */
	static std::vector<SampleFrame> transitionSource(int rate)
	{
		constexpr double kReferenceRate = 48000.0;
		// A fixed frame COUNT, so the fixture is identical whatever the engine's rate is;
		// `rate` only tells the Sample what its own rate is (unity means rate == engine rate).
		constexpr std::size_t frames = 48000;
		std::vector<SampleFrame> source(frames);
		for (std::size_t i = 0; i < frames; ++i)
		{
			const double t = static_cast<double>(i) / kReferenceRate;
			const auto value = static_cast<float>(
				0.6 * std::sin(2.0 * kPi * 220.0 * t) + 0.3 * std::sin(2.0 * kPi * 1310.0 * t));
			source[i] = SampleFrame(value, -0.5f * value);
		}
		return source;
	}

	//! Drive the plan and return the checksum of the post-unity output only.
	static double playTransitionPlan(Sample& sample, Sample::PlaybackState* state,
		const CallPlan& plan, std::vector<SampleFrame>& out)
	{
		double checksum = 0.0;
		for (int call = 0; call < CallPlan::totalCalls; ++call)
		{
			sample.play(out.data(), state, out.size(), Sample::Loop::Off, plan.ratioAt(call));
			if (!plan.afterUnity(call)) { continue; }
			for (const SampleFrame& frame : out) { checksum += frame[0] + frame[1]; }
		}
		return checksum;
	}

private:
	//! One render of the fixture project at \a rate, as 32-bit float frames.
	std::vector<SampleFrame> render(int rate, int volume)
	{
		const QString wav = QDir(m_dir.path()).filePath(QStringLiteral("source-%1.wav").arg(rate));
		if (!writeFloatWav(wav, makeSource(rate), rate)) { return {}; }
		const QString project = writeProject(m_dir.path(), wav, rate, volume);
		if (project.isEmpty()) { return {}; }
		Engine::getSong()->loadProject(project);

		const QString out = QDir(m_dir.path()).filePath(
			QStringLiteral("render-%1-%2.wav").arg(rate).arg(volume));
		QFile::remove(out);

		{
			const OutputSettings settings(sample_rate_t(rate), 256,
				OutputSettings::BitDepth::Depth32Bit, OutputSettings::StereoMode::Stereo);
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, out);
			if (!renderer.isReady()) { return {}; }
			renderer.startProcessing();
			renderer.wait();
		}
		// The render leaves the engine on its own file device; the next case's
		// ProjectRenderer stores and replaces it, but the dummy device must not be
		// left rendering in the background.
		if (auto* device = Engine::audioEngine()->audioDev()) { device->stopProcessing(); }

		return readFloatWav(out);
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(AudioEngineNullRenderTest)
#include "AudioEngineNullRenderTest.moc"
