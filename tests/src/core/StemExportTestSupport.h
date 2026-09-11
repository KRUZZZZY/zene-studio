/*
 * StemExportTestSupport.h - fixtures for the stem-export acceptance test
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

#ifndef LMMS_TESTS_STEM_EXPORT_TEST_SUPPORT_H
#define LMMS_TESTS_STEM_EXPORT_TEST_SUPPORT_H

//! Helpers for StemExportTest: write a real project, render it, and read back
//! the WAVs.
//!
//! The project is three SampleTracks with different content, different lengths
//! and different levels, so a whole-project render is 6 bars of content plus the
//! renderer's 1-bar tail (7 bars), and the stems differ from the mix and from
//! each other. No mocking: the test drives Engine + RenderManager +
//! ProjectRenderer + AudioFileWave and measures the files that come out.

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "AudioDevice.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include "Song.h"

namespace stemsupport
{

constexpr int kTicksPerBar = 192;
constexpr int kRate = 44100;
constexpr int kBpm = 120;

// --- reading 16-bit PCM WAVs -------------------------------------------------

//! A 16-bit PCM stereo WAV, read whole.
struct Wav
{
	int channels = 0;
	int rate = 0;
	std::vector<std::pair<int, int>> frames;   // L, R

	int frameCount() const { return static_cast<int>(frames.size()); }
	double seconds() const { return rate > 0 ? static_cast<double>(frameCount()) / rate : 0.0; }

	double rms() const
	{
		if (frames.empty()) { return 0.0; }
		double acc = 0.0;
		for (const auto& f : frames) { acc += double(f.first) * f.first + double(f.second) * f.second; }
		return std::sqrt(acc / (frames.size() * 2.0));
	}

	int peak() const
	{
		int p = 0;
		for (const auto& f : frames) { p = std::max(p, std::max(std::abs(f.first), std::abs(f.second))); }
		return p;
	}
};

inline quint32 le32(const QByteArray& b, int at)
{
	return quint32(quint8(b[at])) | (quint32(quint8(b[at + 1])) << 8)
		| (quint32(quint8(b[at + 2])) << 16) | (quint32(quint8(b[at + 3])) << 24);
}

inline quint16 le16(const QByteArray& b, int at)
{
	return quint16(quint8(b[at])) | (quint16(quint8(b[at + 1])) << 8);
}


//! Collect the `fmt ` and `data` chunk bodies of a RIFF/WAVE file.
inline bool riffChunks(const QByteArray& blob, QByteArray* fmt, QByteArray* data)
{
	int pos = 12;
	while (pos + 8 <= blob.size())
	{
		const QByteArray id = blob.mid(pos, 4);
		const quint32 size = le32(blob, pos + 4);
		if (id == "fmt ") { *fmt = blob.mid(pos + 8, int(size)); }
		else if (id == "data") { *data = blob.mid(pos + 8, int(size)); }
		pos += 8 + int(size) + int(size & 1);
	}
	return !fmt->isEmpty() && !data->isEmpty();
}

//! 16-bit PCM stereo only: that is what this test writes and expects back.
//! Offsets are relative to the `fmt ` chunk body: 0 = format, 2 = channels,
//! 4 = sample rate, 14 = bits per sample.
inline bool parseRiffFormat(const QByteArray& fmt, Wav* wav)
{
	if (fmt.size() < 16 || le16(fmt, 0) != 1 || le16(fmt, 14) != 16) { return false; }
	wav->channels = le16(fmt, 2);
	wav->rate = int(le32(fmt, 4));
	return wav->channels == 2;
}

inline Wav readWav(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return Wav{}; }
	const QByteArray blob = file.readAll();
	if (blob.size() < 44 || blob.mid(0, 4) != "RIFF" || blob.mid(8, 4) != "WAVE") { return Wav{}; }

	QByteArray fmt;
	QByteArray data;
	if (!riffChunks(blob, &fmt, &data)) { return Wav{}; }

	Wav wav;
	if (!parseRiffFormat(fmt, &wav) || data.isEmpty()) { return Wav{}; }
	for (int i = 0; i + 4 <= data.size(); i += 4)
	{
		wav.frames.emplace_back(int(qint16(le16(data, i))), int(qint16(le16(data, i + 2))));
	}
	return wav;
}

inline bool silentFrameAt(const Wav& wav, int i)
{
	return wav.frames[i].first == 0 && wav.frames[i].second == 0;
}

// --- writing the fixture: a real project and its samples ---------------------

inline void put32(QByteArray& out, quint32 v)
{
	out.append(char(v & 0xff)).append(char((v >> 8) & 0xff))
		.append(char((v >> 16) & 0xff)).append(char((v >> 24) & 0xff));
}

inline void put16(QByteArray& out, quint16 v)
{
	out.append(char(v & 0xff)).append(char((v >> 8) & 0xff));
}

//! Write a decaying stereo sine as 16-bit PCM: the test's track content.
inline void writeSample(const QString& path, double freq, double seconds = 0.6)
{
	const int count = int(seconds * kRate);
	QByteArray data;
	data.reserve(count * 4);
	for (int i = 0; i < count; ++i)
	{
		const double t = double(i) / kRate;
		const double env = std::exp(-3.0 * t / seconds);
		const auto quantise = [](double v) {
			return qint16(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0));
		};
		put16(data, quint16(quantise(0.7 * env * std::sin(2.0 * M_PI * freq * t))));
		put16(data, quint16(quantise(0.7 * env * std::sin(2.0 * M_PI * freq * 1.5 * t))));
	}

	QByteArray header;
	header.append("RIFF");
	put32(header, quint32(36 + data.size()));
	header.append("WAVEfmt ");
	put32(header, 16); put16(header, 1); put16(header, 2);
	put32(header, quint32(kRate)); put32(header, quint32(kRate * 4));
	put16(header, 4); put16(header, 16);
	header.append("data");
	put32(header, quint32(data.size()));

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return; }
	file.write(header + data);
}

struct DemoTrack
{
	QString name;
	double freq;
	int bars;
	int secondClipBar;   // -1 for a single clip
	int volume;
	int pan;
};

//! Track order is the project order: Bass 2 bars, Pad 4 bars, Lead 6 bars.
inline const DemoTrack kDemoTracks[] = {
	{"Bass", 220.0, 2, -1, 40, -40},
	{"Pad", 440.0, 4, -1, 30, 40},
	{"Lead", 880.0, 6, 4, 35, 0},
};

inline QString trackXml(const DemoTrack& t, const QString& dir)
{
	const QString sample = QDir(dir).filePath(t.name.toLower() + ".wav");
	writeSample(sample, t.freq);

	QString clips;
	if (t.secondClipBar < 0)
	{
		clips = QStringLiteral("<sampleclip pos=\"0\" len=\"%1\" muted=\"0\" src=\"%2\" off=\"0\"/>")
			.arg(t.bars * kTicksPerBar).arg(sample);
	}
	else
	{
		// A second clip so this track has audio right up to the project end.
		clips = QStringLiteral(
			"<sampleclip pos=\"0\" len=\"%1\" muted=\"0\" src=\"%2\" off=\"0\"/>\n"
			"        <sampleclip pos=\"%1\" len=\"%3\" muted=\"0\" src=\"%2\" off=\"0\"/>")
			.arg(t.secondClipBar * kTicksPerBar).arg(sample)
			.arg((t.bars - t.secondClipBar) * kTicksPerBar);
	}

	return QStringLiteral(
		"      <track muted=\"0\" name=\"%1\" solo=\"0\" type=\"2\">\n"
		"        <sampletrack vol=\"%2\" pan=\"%3\" mixch=\"0\"/>\n"
		"        %4\n"
		"      </track>\n").arg(t.name).arg(t.volume).arg(t.pan).arg(clips);
}

//! Write a real project file (`<dir>/demo.mmp` plus its samples); returns its path.
inline QString writeDemoProject(const QString& dir)
{
	QString tracks;
	for (const auto& t : kDemoTracks) { tracks += trackXml(t, dir); }

	const QString xml = QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<!DOCTYPE lmms-project>\n"
		"<lmms-project creator=\"LMMS\" version=\"1.0\" type=\"song\" creatorversion=\"1.2.0\">\n"
		"  <head timesig_numerator=\"4\" bpm=\"%1\" timesig_denominator=\"4\" mastervol=\"102\" masterpitch=\"0\"/>\n"
		"  <song>\n"
		"    <trackcontainer maximized=\"0\" height=\"212\" visible=\"1\" minimized=\"0\" x=\"5\" y=\"5\" type=\"song\" width=\"600\">\n"
		"%2"
		"    </trackcontainer>\n"
		"  </song>\n"
		"</lmms-project>\n").arg(kBpm).arg(tracks);

	const QString path = QDir(dir).filePath("demo.mmp");
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return QString(); }
	file.write(xml.toUtf8());
	file.close();
	return path;
}

// --- driving the render path -------------------------------------------------

inline lmms::OutputSettings settings()
{
	return lmms::OutputSettings(lmms::sample_rate_t(kRate), 128,
			lmms::OutputSettings::BitDepth::Depth16Bit,
			lmms::OutputSettings::StereoMode::Stereo);
}

//! Stop the engine's own (headless, dummy) device thread. The dummy device is a
//! thread that renders periods, so between renders it must be stopped or it
//! races the next render.
inline void stopHeadlessDevice()
{
	if (auto* dev = lmms::Engine::audioEngine()->audioDev()) { dev->stopProcessing(); }
}

//! The whole-project render, exactly as the CLI/GUI does it. Returns the file's
//! SHA-256 so two renders can be compared.
inline QByteArray renderMix(const QString& outPath)
{
	{
		lmms::RenderManager manager(settings(), lmms::ProjectRenderer::ExportFileFormat::Wave, outPath);
		QEventLoop loop;
		QObject::connect(&manager, &lmms::RenderManager::finished, &loop, &QEventLoop::quit);
		QTimer::singleShot(120000, &loop, &QEventLoop::quit);
		manager.renderProject();
		loop.exec();
	}
	stopHeadlessDevice();

	QFile file(outPath);
	if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

//! One stem export into `outDir`; `stemBars` (optional) receives the length in
//! bars every stem was rendered to.
inline void exportStems(const QString& outDir, const lmms::StemExportOptions& options,
		int* stemBars = nullptr)
{
	QDir().mkpath(outDir);
	int bars = 0;
	{
		lmms::RenderManager manager(settings(), lmms::ProjectRenderer::ExportFileFormat::Wave, outDir);
		QEventLoop loop;
		QObject::connect(&manager, &lmms::RenderManager::finished, &loop, &QEventLoop::quit);
		QTimer::singleShot(120000, &loop, &QEventLoop::quit);
		manager.exportStems(options);
		loop.exec();
		bars = manager.stemLengthBars();
	}
	stopHeadlessDevice();
	if (stemBars) { *stemBars = bars; }
}

//! Stem file names in `dir`, sorted (the naming contract sorts in track order).
inline QStringList stemPaths(const QString& dir, const QString& extension = ".wav")
{
	QStringList names = QDir(dir).entryList(QDir::Files, QDir::Name);
	names.erase(std::remove_if(names.begin(), names.end(),
			[&extension](const QString& n) { return !n.endsWith(extension); }), names.end());
	return names;
}

inline std::vector<Wav> readStems(const QString& dir)
{
	std::vector<Wav> out;
	for (const auto& name : stemPaths(dir)) { out.push_back(readWav(QDir(dir).filePath(name))); }
	return out;
}

// --- the measurements the test asserts on ------------------------------------

//! Sum every stem frame by frame: for a linear graph this is the mix.
struct StemEvidence
{
	int compared = 0;      // audio frames outside the renderer's startup transient
	int skipped = 0;       // frames excluded (transient, or silent in either file)
	int median = 0;        // |summed - mix| percentiles, in LSB
	int p75 = 0;
	int worst = 0;
	double sumRms = 0.0;
	double mixRms = 0.0;
	double deltaDb = 0.0;      // difference RMS relative to the mix
	double energyDb = 0.0;     // summed energy relative to the mix's, over the whole file
};

inline Wav sumStems(const std::vector<Wav>& stems)
{
	Wav out;
	if (stems.empty()) { return out; }
	out.channels = stems.front().channels;
	out.rate = stems.front().rate;
	out.frames.assign(stems.front().frames.size(), {0, 0});
	for (const auto& s : stems)
	{
		for (std::size_t i = 0; i < out.frames.size() && i < s.frames.size(); ++i)
		{
			out.frames[i].first += s.frames[i].first;
			out.frames[i].second += s.frames[i].second;
		}
	}
	return out;
}

//! How far the summed stems are from the mix. `skipFrames` excludes the
//! renderer's startup transient (see docs/STEM-EXPORT.md section 4.4).
inline StemEvidence compareStemsToMix(const Wav& mix, const Wav& summed, int skipFrames)
{
	StemEvidence e;
	std::vector<int> deltas;
	double diffAcc = 0.0;
	double mixAcc = 0.0;
	double sumEnergy = 0.0;
	double mixEnergy = 0.0;
	const int length = std::min(mix.frameCount(), summed.frameCount());

	for (int i = 0; i < length; ++i)
	{
		const double sumL = summed.frames[i].first;
		const double sumR = summed.frames[i].second;
		const double mixL = mix.frames[i].first;
		const double mixR = mix.frames[i].second;
		sumEnergy += sumL * sumL + sumR * sumR;
		mixEnergy += mixL * mixL + mixR * mixR;

		if (i < skipFrames || silentFrameAt(mix, i) || silentFrameAt(summed, i))
		{
			++e.skipped;
			continue;
		}
		++e.compared;
		const int left = summed.frames[i].first - mix.frames[i].first;
		const int right = summed.frames[i].second - mix.frames[i].second;
		deltas.push_back(std::max(std::abs(left), std::abs(right)));
		diffAcc += double(left) * left + double(right) * right;
		mixAcc += double(mix.frames[i].first) * mix.frames[i].first
			+ double(mix.frames[i].second) * mix.frames[i].second;
	}

	std::sort(deltas.begin(), deltas.end());
	if (!deltas.empty())
	{
		e.median = deltas[deltas.size() / 2];
		e.p75 = deltas[deltas.size() * 3 / 4];
		e.worst = deltas.back();
	}
	if (e.compared > 0)
	{
		e.sumRms = std::sqrt(diffAcc / (e.compared * 2.0));
		e.mixRms = std::sqrt(mixAcc / (e.compared * 2.0));
		e.deltaDb = 20.0 * std::log10(e.sumRms / e.mixRms);
	}
	e.energyDb = 10.0 * std::log10(sumEnergy / mixEnergy);
	return e;
}

//! Every stem is audible, is quieter than the mix, and the stems differ from
//! each other. `why` receives a description when it returns false.
inline bool allStemsSane(const std::vector<Wav>& stems, const Wav& mix, QString* why)
{
	for (std::size_t i = 0; i < stems.size(); ++i)
	{
		if (stems[i].rms() <= 1.0)
		{
			*why = QStringLiteral("stem %1 is silent").arg(int(i));
			return false;
		}
		if (stems[i].rms() >= mix.rms())
		{
			*why = QStringLiteral("stem %1 is as loud as the mix").arg(int(i));
			return false;
		}
		for (std::size_t j = 0; j < i; ++j)
		{
			if (stems[i].rms() == stems[j].rms())
			{
				*why = QStringLiteral("stems %1 and %2 are identical").arg(int(j)).arg(int(i));
				return false;
			}
		}
	}
	return true;
}

//! Every stem's shape matches the mix's, so the stems line up with it.
inline bool allStemsAligned(const std::vector<Wav>& stems, const Wav& mix, QString* why)
{
	for (const auto& s : stems)
	{
		if (s.frameCount() != mix.frameCount() || s.channels != mix.channels || s.rate != mix.rate)
		{
			*why = QStringLiteral("stem %1 frames/%2 vs the mix %3/%4")
				.arg(s.frameCount()).arg(s.channels).arg(mix.frameCount()).arg(mix.channels);
			return false;
		}
	}
	return true;
}

inline void printTrack(const char* label, const Wav& wav, const QString& name)
{
	std::fprintf(stderr, "STEM_EVIDENCE %s %-9s frames=%-8d channels=%d rate=%d %.3fs "
			"RMS=%.1f peak=%d\n", label, qPrintable(name), wav.frameCount(), wav.channels,
			wav.rate, wav.seconds(), wav.rms(), wav.peak());
	std::fflush(stderr);
}

inline void printSumEvidence(const StemEvidence& e)
{
	std::fprintf(stderr, "STEM_EVIDENCE SUM |delta| LSB: median=%d p75=%d max=%d; "
			"difference RMS=%.3f (%.1f dB below the mix); summed energy %.4f dB vs the mix; "
			"compared %d frames (%d skipped)\n", e.median, e.p75, e.worst, e.sumRms, e.deltaDb,
			e.energyDb, e.compared, e.skipped);
	std::fflush(stderr);
}

} // namespace stemsupport

#endif // LMMS_TESTS_STEM_EXPORT_TEST_SUPPORT_H
