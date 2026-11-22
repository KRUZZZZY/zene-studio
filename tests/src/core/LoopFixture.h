/*
 * LoopFixture.h - a synthesised loop with a known tempo and key (Q050-093 tests)
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
/*! The answer is known by construction: one note per beat at `bpm`, walking the major scale up from `tonicHz`
 *  over a drone an octave below, exactly four bars (16 beats) long, 16-bit mono at 44.1 kHz. The same synthesis as
 *  tests/control-loop-import-warp.py, written here with nothing from the engine (its decoder reads it back). */
#ifndef LMMS_TESTS_LOOP_FIXTURE_H
#define LMMS_TESTS_LOOP_FIXTURE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QString>

namespace loopfixture
{

constexpr int SampleRate = 44100;
constexpr int Beats = 16;
constexpr double Pi = 3.14159265358979323846;

inline std::vector<double> loop(double bpm, double tonicHz)
{
	const double beat = 60.0 / bpm;
	const auto total = static_cast<std::size_t>(Beats * beat * SampleRate);
	std::vector<double> samples(total, 0.0);
	const std::array<double, 8> ratios{1.0, 9.0 / 8.0, 5.0 / 4.0, 4.0 / 3.0, 3.0 / 2.0, 5.0 / 3.0, 15.0 / 8.0, 2.0};
	for (int n = 0; n < Beats; ++n)
	{
		const auto start = static_cast<std::size_t>(n * beat * SampleRate);
		const auto length = static_cast<std::size_t>(beat * 0.9 * SampleRate);
		const double hz = tonicHz * ratios[static_cast<std::size_t>(n % 8)];
		for (std::size_t i = 0; i < length && start + i < total; ++i)
		{
			const double envelope = std::exp(-3.0 * static_cast<double>(i) / static_cast<double>(length))
				* std::min(1.0, static_cast<double>(i) / (0.002 * SampleRate));
			samples[start + i] += 0.35 * envelope * std::sin(2.0 * Pi * hz * static_cast<double>(i) / SampleRate);
		}
	}
	for (std::size_t i = 0; i < total; ++i)
	{
		samples[i] += 0.3 * std::sin(2.0 * Pi * (tonicHz / 2.0) * static_cast<double>(i) / SampleRate);
	}
	return samples;
}

//! A steady tone with no onset to measure a tempo from.
inline std::vector<double> steadyTone(double seconds = 3.0)
{
	std::vector<double> samples(static_cast<std::size_t>(seconds * SampleRate));
	for (std::size_t i = 0; i < samples.size(); ++i)
	{
		samples[i] = 0.4 * std::sin(2.0 * Pi * 220.0 * static_cast<double>(i) / SampleRate);
	}
	return samples;
}

inline bool writeWave(const QString& path, const std::vector<double>& samples)
{
	QByteArray out("RIFF", 4);
	auto le32 = [&out](quint32 v) { for (int i = 0; i < 4; ++i) { out.append(static_cast<char>((v >> (8 * i)) & 0xFF)); } };
	auto le16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>(v >> 8)); };
	const auto dataBytes = static_cast<quint32>(samples.size() * 2);
	le32(36 + dataBytes);
	out.append("WAVEfmt ", 8);
	le32(16); le16(1); le16(1); le32(SampleRate); le32(SampleRate * 2); le16(2); le16(16);
	out.append("data", 4);
	le32(dataBytes);
	for (const double value : samples)
	{
		le16(static_cast<quint16>(static_cast<qint16>(std::lround(std::clamp(value, -1.0, 1.0) * 32767.0))));
	}
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(out) == out.size();
}

} // namespace loopfixture

#endif // LMMS_TESTS_LOOP_FIXTURE_H
