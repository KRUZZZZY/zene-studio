/*
 * MasteringTestSupport.h - a real project fixture and waveform helpers
 *
 * Copyright (c) 2026 Zene Studio developers
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

#ifndef LMMS_MASTERING_TEST_SUPPORT_H
#define LMMS_MASTERING_TEST_SUPPORT_H

#include <QDir>
#include <QFile>
#include <QPair>
#include <QString>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <sndfile.h>

#include "LmmsTypes.h"
#include "SampleFrame.h"

namespace lmms
{
namespace masteringtest
{

//! How long one fixture track's clip is: 2 bars at 120 bpm in 4/4 (192 ticks
//! a bar), i.e. 4 s. The render adds the renderer's own one-bar tail.
constexpr int ClipTicks = 384;
constexpr double ClipSeconds = 4.0;

//! A decaying burst every this many seconds in the transient tracks. Real
//! programme has transient crest factor; a fixture of steady sines does not,
//! and a chain that never has to limit cannot be shown to respect a ceiling.
constexpr double BurstPeriodSeconds = 0.5;
constexpr double BurstDecaySeconds = 0.03;

/**
 * The fixture signal for one track. \p bursts off is a steady tone; on is the
 * same tone chopped into decaying bursts, which is what gives the fixture a
 * crest factor the limiter has to act on.
 */
inline std::vector<SampleFrame> probeSignal(sample_rate_t sampleRate, double seconds, double toneHz,
	double amplitude, bool bursts)
{
	const auto frames = static_cast<std::size_t>(seconds * sampleRate);
	std::vector<SampleFrame> signal(frames);
	const double twoPi = 6.283185307179586;
	for (std::size_t i = 0; i < frames; ++i)
	{
		const double t = static_cast<double>(i) / sampleRate;
		double envelope = 1.0;
		if (bursts)
		{
			const double sinceBurst = std::fmod(t, BurstPeriodSeconds);
			envelope = std::exp(-sinceBurst / BurstDecaySeconds);
		}
		const auto value = static_cast<sample_t>(amplitude * envelope
			* std::sin(twoPi * toneHz * t));
		signal[i].setLeft(value);
		signal[i].setRight(value);
	}
	return signal;
}

//! Writes a 16-bit PCM stereo WAV - the same shape the engine renders to.
inline bool writeWav(const QString& path, const std::vector<SampleFrame>& frames,
	sample_rate_t sampleRate)
{
	SF_INFO info{};
	info.samplerate = static_cast<int>(sampleRate);
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		return false;
	}
	std::vector<float> interleaved(frames.size() * 2);
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		interleaved[i * 2] = frames[i].left();
		interleaved[i * 2 + 1] = frames[i].right();
	}
	const sf_count_t written = sf_writef_float(file, interleaved.data(),
		static_cast<sf_count_t>(frames.size()));
	sf_close(file);
	return written == static_cast<sf_count_t>(frames.size());
}

//! Reads any PCM/float stereo WAV back into frames.
inline bool readWav(const QString& path, std::vector<SampleFrame>& frames, sample_rate_t& sampleRate,
	QString* error)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_READ, &info);
	if (file == nullptr)
	{
		if (error != nullptr)
		{
			*error = QString::fromUtf8(sf_strerror(nullptr));
		}
		return false;
	}
	const auto count = static_cast<std::size_t>(std::max<sf_count_t>(0, info.frames));
	std::vector<float> interleaved(count * static_cast<std::size_t>(info.channels));
	const sf_count_t read = sf_readf_float(file, interleaved.data(), info.frames);
	sf_close(file);
	if (read < 0 || info.channels != 2)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("unreadable channel layout");
		}
		return false;
	}

	sampleRate = static_cast<sample_rate_t>(info.samplerate);
	frames.resize(static_cast<std::size_t>(read));
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		frames[i].setLeft(interleaved[i * 2]);
		frames[i].setRight(interleaved[i * 2 + 1]);
	}
	return true;
}

//! Largest per-sample difference of two signals, in 16-bit LSB.
inline std::uint32_t maxAbsDeltaLsb(const std::vector<SampleFrame>& a,
	const std::vector<SampleFrame>& b)
{
	std::uint32_t worst = 0;
	const std::size_t common = std::min(a.size(), b.size());
	for (std::size_t i = 0; i < common; ++i)
	{
		const double dl = std::fabs(static_cast<double>(a[i].left()) - b[i].left());
		const double dr = std::fabs(static_cast<double>(a[i].right()) - b[i].right());
		worst = std::max(worst, static_cast<std::uint32_t>(std::max(dl, dr) * 32768.0 + 0.5));
	}
	return worst;
}

//! Signal RMS in dBFS; -inf for an empty signal.
inline double rmsDbfs(const std::vector<SampleFrame>& frames)
{
	if (frames.empty())
	{
		return -std::numeric_limits<double>::infinity();
	}
	double sum = 0.0;
	for (const auto& frame : frames)
	{
		sum += static_cast<double>(frame.left()) * frame.left()
			+ static_cast<double>(frame.right()) * frame.right();
	}
	const double mean = sum / (2.0 * static_cast<double>(frames.size()));
	return mean > 0.0 ? 10.0 * std::log10(mean) : -std::numeric_limits<double>::infinity();
}

/**
 * Writes a .mmp whose sample tracks play the fixture WAVs. The XML is the
 * shape data/projects uses (see plugins/RnnoiseDenoiser/testdata); the audio
 * files are absolute paths into the caller's temporary directory, so the
 * project is self-describing and loads through the normal path.
 */
inline bool writeProject(const QString& path, const QString& directory,
	const QVector<QPair<QString, QString>>& trackNamesAndFiles)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		return false;
	}
	QTextStream out(&file);
	out << "<?xml version=\"1.0\"?>\n<!DOCTYPE multimedia-project>\n";
	out << "<multimedia-project version=\"1.0\" creator=\"LMMS\" creatorversion=\"1.3.0\" "
		"type=\"song\">\n";
	out << "  <head timesig_numerator=\"4\" mastervol=\"100\" timesig_denominator=\"4\" "
		"bpm=\"120\" masterpitch=\"0\" />\n";
	out << "  <song>\n";
	out << "    <trackcontainer width=\"600\" x=\"5\" y=\"5\" maximized=\"0\" height=\"300\" "
		"visible=\"1\" type=\"song\" minimized=\"0\">\n";
	for (const auto& track : trackNamesAndFiles)
	{
		const QString source = QDir(directory).filePath(track.second);
		out << "      <track muted=\"0\" type=\"2\" name=\"" << track.first << "\">\n";
		out << "        <sampletrack vol=\"100\" pan=\"0\" mixch=\"0\">\n";
		out << "          <fxchain numofeffects=\"0\" enabled=\"0\"/>\n";
		out << "        </sampletrack>\n";
		out << "        <sampleclip pos=\"0\" len=\"" << ClipTicks << "\" muted=\"0\" src=\""
			<< source << "\" off=\"0\" autoresize=\"0\" sample_rate=\"44100\"/>\n";
		out << "      </track>\n";
	}
	out << "    </trackcontainer>\n";
	out << "    <mixer width=\"865\" x=\"5\" y=\"310\" maximized=\"0\" height=\"278\" visible=\"1\" "
		"minimized=\"0\">\n";
	out << "      <mixerchannel num=\"0\" muted=\"0\" volume=\"1\" name=\"Master\">\n";
	out << "        <fxchain numofeffects=\"0\" enabled=\"0\"/>\n";
	out << "      </mixerchannel>\n";
	out << "    </mixer>\n";
	out << "    <controllerrackview width=\"258\" x=\"880\" y=\"310\" maximized=\"0\" height=\"278\" "
		"visible=\"1\" minimized=\"0\"/>\n";
	out << "    <timeline lp1pos=\"192\" lp0pos=\"0\" lpstate=\"0\"/>\n";
	out << "    <controllers/>\n";
	out << "  </song>\n";
	out << "</multimedia-project>\n";
	file.close();
	return true;
}

/**
 * Writes the whole fixture - three transient tracks of the same two bars at
 * different levels and tones - and returns the project path. The tracks differ
 * in level so the mix has the short-term variation a loudness measure needs to
 * say anything at all.
 */
inline bool writeFixture(const QString& directory, QString& projectPath)
{
	const auto bass = probeSignal(44100, ClipSeconds, 55.0, 0.12, false);
	const auto pad = probeSignal(44100, ClipSeconds, 220.0, 0.12, true);
	const auto lead = probeSignal(44100, ClipSeconds, 1000.0, 0.25, true);
	if (!writeWav(QDir(directory).filePath(QStringLiteral("bass.wav")), bass, 44100)
		|| !writeWav(QDir(directory).filePath(QStringLiteral("pad.wav")), pad, 44100)
		|| !writeWav(QDir(directory).filePath(QStringLiteral("lead.wav")), lead, 44100))
	{
		return false;
	}

	QVector<QPair<QString, QString>> tracks;
	tracks.append({QStringLiteral("Bass"), QStringLiteral("bass.wav")});
	tracks.append({QStringLiteral("Pad"), QStringLiteral("pad.wav")});
	tracks.append({QStringLiteral("Lead"), QStringLiteral("lead.wav")});

	projectPath = QDir(directory).filePath(QStringLiteral("fixture.mmp"));
	return writeProject(projectPath, directory, tracks);
}

} // namespace masteringtest
} // namespace lmms

#endif // LMMS_MASTERING_TEST_SUPPORT_H
