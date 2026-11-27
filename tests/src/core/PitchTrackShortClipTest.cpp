/*
 * PitchTrackShortClipTest.cpp - cluster new-p17-125: a clip shorter than one analysis window
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
/*! Cluster new-p17-125: detectNotes() only analysed complete 1024-sample windows of the decimated
 *  signal, so audio shorter than one window (a 50 ms clip decimates to 551 samples) detected no
 *  notes at all, however much longer than minFrames the note was. The analysis now ends with a
 *  window that may be shorter than one window, bounded by the buffer. */

#include <QtTest>

#include <cmath>
#include <cstddef>
#include <vector>

#include "PitchTrack.h"

using namespace lmms;

namespace
{
//! A @a hz sine at @a rate for @a seconds, half scale - above the tracker's 0.02 gate.
std::vector<float> tone(double hz, int rate, double seconds)
{
	std::vector<float> out(static_cast<std::size_t>(rate * seconds));
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		out[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / rate));
	}
	return out;
}
} // namespace

class PitchTrackShortClipTest : public QObject
{
	Q_OBJECT

private slots:
	void aClipShorterThanOneAnalysisWindowStillYieldsItsNote()
	{
		constexpr int rate = 44100;
		// 50 ms decimates to 551 samples, fewer than the 1024-sample analysis window, while the
		// note itself lasts 2205 frames, longer than the default 2048-frame minimum.
		const std::vector<float> audio = tone(440.0, rate, 0.05);
		QCOMPARE(audio.size(), std::size_t{2205});
		const std::vector<pitchtrack::PitchNote> notes = pitchtrack::detectNotes(audio.data(), audio.size(), rate);
		QCOMPARE(notes.size(), std::size_t{1});
		QCOMPARE(notes.front().key, 69);
		QCOMPARE(notes.front().start, f_cnt_t{0});
		QCOMPARE(notes.front().end, static_cast<f_cnt_t>(audio.size()));
	}
};

QTEST_APPLESS_MAIN(PitchTrackShortClipTest)

#include "PitchTrackShortClipTest.moc"
