/*
 * SurroundExportTestSupport.h - the fixture and the measurements of SurroundExportTest (0.5.0 F7)
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

#ifndef LMMS_TESTS_SURROUND_EXPORT_TEST_SUPPORT_H
#define LMMS_TESTS_SURROUND_EXPORT_TEST_SUPPORT_H

/*! The fixture is a real project: one sample track per source, each a constant sine at its own
 *  frequency, each on its own mixer channel whose surround panner points at one speaker of a 5.1
 *  master. The LFE source is panned to the centre with its LFE send at 1, and a second track carrying
 *  the same sine negated (panned to the centre, no LFE send) cancels it on the centre speaker - so in
 *  the rendered file every speaker carries exactly one source. The file is read back through libsndfile
 *  (the samples) and by hand (the header fields: format tag, channel count, bits, channel mask), and each
 *  channel's level at each source's frequency is measured over a window holding whole cycles of all of
 *  them, so the sources do not leak into one another's bins. */

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QString>
#include <QTimer>

#include <sndfile.h>

#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"

namespace surroundexporttest
{

constexpr int Rate = 44100;
constexpr int TicksPerBar = 192;
//! The analysis window: 0.1 s, a whole number of cycles of every source frequency (multiples of 10 Hz).
constexpr int Window = 4410;
//! Where the window starts: 0.5 s in, well past the render's start-up and well before the clips end.
constexpr int WindowStart = Rate / 2;
constexpr double Amplitude = 0.25;

//! A source: its sample's frequency, the panner's point, its LFE send, and whether the sine is negated.
struct Source
{
	const char* name;
	double frequency;
	double x;
	double y;
	double lfe;
	bool negated;
	int speaker;  //!< the 5.1 channel it must land on (-1: the canceller, it lands nowhere)
};

//! 5.1's speakers in the WAVE order L R C LFE Ls Rs.
inline const std::array<Source, 7>& sources()
{
	static const std::array<Source, 7> list{{
		{"left", 400.0, -1.0, 1.0, 0.0, false, 0},
		{"right", 600.0, 1.0, 1.0, 0.0, false, 1},
		{"centre", 800.0, 0.0, 1.0, 0.0, false, 2},
		{"lfe", 100.0, 0.0, 1.0, 1.0, false, 3},
		{"lfe-cancel", 100.0, 0.0, 1.0, 0.0, true, -1},
		{"left-surround", 1000.0, -1.0, -1.0, 0.0, false, 4},
		{"right-surround", 1200.0, 1.0, -1.0, 0.0, false, 5},
	}};
	return list;
}

inline void put16(QByteArray& out, quint16 v) { out.append(char(v & 0xff)).append(char((v >> 8) & 0xff)); }
inline void put32(QByteArray& out, quint32 v)
{
	put16(out, quint16(v & 0xffff));
	put16(out, quint16(v >> 16));
}

//! A 2.5 s 16-bit stereo WAV with the same sine on both sides (negated when asked).
inline bool writeSine(const QString& path, double frequency, bool negated)
{
	const int count = Rate * 5 / 2;
	QByteArray data;
	for (int i = 0; i < count; ++i)
	{
		const double v = Amplitude * std::sin(2.0 * M_PI * frequency * i / Rate);
		const auto q = static_cast<qint16>(std::lround(v * 32767.0));
		const auto s = static_cast<quint16>(negated ? -q : q);
		put16(data, s);
		put16(data, s);
	}
	QByteArray header("RIFF");
	put32(header, quint32(36 + data.size()));
	header.append("WAVEfmt ");
	put32(header, 16);
	put16(header, 1);
	put16(header, 2);
	put32(header, quint32(Rate));
	put32(header, quint32(Rate * 4));
	put16(header, 4);
	put16(header, 16);
	header.append("data");
	put32(header, quint32(data.size()));
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(header + data) == header.size() + data.size();
}

//! The project file: one sample track per source, each with one clip of a bar from the start.
inline QString writeProject(const QString& dir)
{
	QString tracks;
	for (const Source& s : sources())
	{
		const QString sample = QDir(dir).filePath(QString::fromLatin1(s.name) + QStringLiteral(".wav"));
		if (!writeSine(sample, s.frequency, s.negated)) { return QString(); }
		tracks += QStringLiteral("      <track muted=\"0\" name=\"%1\" solo=\"0\" type=\"2\">\n"
								 "        <sampletrack vol=\"100\" pan=\"0\" mixch=\"0\"/>\n"
								 "        <sampleclip pos=\"0\" len=\"%2\" muted=\"0\" src=\"%3\" off=\"0\"/>\n"
								 "      </track>\n")
					  .arg(QString::fromLatin1(s.name))
					  .arg(TicksPerBar)
					  .arg(sample);
	}
	const QString xml = QStringLiteral("<?xml version=\"1.0\"?>\n<!DOCTYPE lmms-project>\n"
		"<lmms-project creator=\"LMMS\" version=\"1.0\" type=\"song\" creatorversion=\"1.2.0\">\n"
		"  <head timesig_numerator=\"4\" bpm=\"120\" timesig_denominator=\"4\" mastervol=\"100\" masterpitch=\"0\"/>\n"
		"  <song>\n    <trackcontainer type=\"song\">\n%1    </trackcontainer>\n  </song>\n"
		"</lmms-project>\n").arg(tracks);
	const QString path = QDir(dir).filePath(QStringLiteral("surround.mmp"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return QString(); }
	file.write(xml.toUtf8());
	return path;
}

//! A whole-project render through RenderManager, as the CLI and the dialog do it.
inline void render(const lmms::OutputSettings& settings, lmms::ProjectRenderer::ExportFileFormat format,
	const QString& out)
{
	{
		lmms::RenderManager manager(settings, format, out);
		QEventLoop loop;
		QObject::connect(&manager, &lmms::RenderManager::finished, &loop, &QEventLoop::quit);
		QTimer::singleShot(120000, &loop, &QEventLoop::quit);
		manager.renderProject();
		loop.exec();
	}
	if (auto* device = lmms::Engine::audioEngine()->audioDev()) { device->stopProcessing(); }
}

inline lmms::OutputSettings settings(lmms::OutputSettings::BitDepth depth)
{
	return lmms::OutputSettings(lmms::sample_rate_t(Rate), 160, depth, lmms::OutputSettings::StereoMode::Stereo);
}

inline QByteArray fileBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline QByteArray sha256(const QString& path)
{
	return QCryptographicHash::hash(fileBytes(path), QCryptographicHash::Sha256).toHex();
}

//! The header fields of a WAV, read by hand (not through the writer's library).
struct Header
{
	int tag = 0;        //!< 1 PCM, 3 IEEE float, 0xFFFE WAVE_FORMAT_EXTENSIBLE
	int channels = 0;
	int bits = 0;
	quint32 mask = 0;   //!< dwChannelMask, extensible only
	int subFormat = 0;  //!< the subformat GUID's first two bytes, extensible only (1 PCM, 3 float)
};

inline quint32 le(const QByteArray& b, int at, int bytes)
{
	quint32 v = 0;
	for (int i = bytes - 1; i >= 0; --i) { v = (v << 8) | quint8(b[at + i]); }
	return v;
}

inline Header readHeader(const QString& path)
{
	const QByteArray blob = fileBytes(path);
	Header h;
	for (int pos = 12; pos + 8 <= blob.size();)
	{
		const quint32 size = le(blob, pos + 4, 4);
		if (blob.mid(pos, 4) == "fmt " && size >= 16)
		{
			h.tag = int(le(blob, pos + 8, 2));
			h.channels = int(le(blob, pos + 10, 2));
			h.bits = int(le(blob, pos + 22, 2));
			if (h.tag == 0xFFFE && size >= 40)
			{
				h.mask = le(blob, pos + 28, 4);
				h.subFormat = int(le(blob, pos + 32, 2));
			}
			break;
		}
		pos += 8 + int(size) + int(size & 1);
	}
	return h;
}

//! Every sample of a WAV, interleaved, as floats (libsndfile reads any depth), and its channel count.
inline std::vector<float> readSamples(const QString& path, int* channels)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (file == nullptr) { return {}; }
	std::vector<float> samples(static_cast<std::size_t>(info.frames) * info.channels);
	sf_readf_float(file, samples.data(), info.frames);
	sf_close(file);
	*channels = info.channels;
	return samples;
}

//! Channel @a channel's amplitude at @a frequency over the analysis window.
inline double levelAt(const std::vector<float>& samples, int channels, int channel, double frequency)
{
	std::complex<double> acc;
	for (int n = 0; n < Window; ++n)
	{
		const double x = samples[static_cast<std::size_t>(WindowStart + n) * channels + channel];
		acc += x * std::polar(1.0, -2.0 * M_PI * frequency * n / Rate);
	}
	return 2.0 * std::abs(acc) / Window;
}

//! Channel @a channel's peak over the analysis window.
inline double peakOf(const std::vector<float>& samples, int channels, int channel)
{
	double peak = 0.0;
	for (int n = 0; n < Window; ++n)
	{
		peak = std::max(peak, double(std::abs(samples[static_cast<std::size_t>(WindowStart + n) * channels + channel])));
	}
	return peak;
}

} // namespace surroundexporttest

#endif // LMMS_TESTS_SURROUND_EXPORT_TEST_SUPPORT_H
