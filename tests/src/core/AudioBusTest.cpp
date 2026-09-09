/*
 * AudioBusTest.cpp - unit tests for AudioBus (Phase F hardening, task #592)
 *
 * Copyright (c) 2026 LMMS developers
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

#include <QtTest>

#include "AudioBus.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"

using namespace lmms;

class AudioBusTest : public QObject
{
	Q_OBJECT

private slots:
	//! Regression test for the pair-iteration bug in silenceAllChannels():
	//! m_channelPairs is the number of channel *pairs*, so the loop must
	//! advance one pair at a time. Stepping by 2 (as track channels) only
	//! zeroed every second pair and left the rest of the bus carrying signal.
	//! Multi-pair buses are what multi-channel plugin I/O uses, so a bus that
	//! silently keeps signal is exactly the fixed-count artefact SPEC 8.2
	//! asks us to rule out.
	void SilenceAllChannels()
	{
		constexpr ch_cnt_t pairs = 4;
		constexpr f_cnt_t frames = 8;

		SampleFrame data[pairs][frames];
		SampleFrame* bus[pairs];
		for (ch_cnt_t p = 0; p < pairs; ++p)
		{
			bus[p] = data[p];
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				// distinct non-zero value per pair so a missed pair is named
				data[p][f][0] = 1.0f + static_cast<float>(p);
				data[p][f][1] = -1.0f - static_cast<float>(p);
			}
		}

		AudioBus audioBus{bus, pairs, frames};
		QCOMPARE(int(audioBus.channelPairs()), int(pairs));
		QCOMPARE(int(audioBus.channels()), int(pairs) * 2);

		audioBus.silenceAllChannels();

		for (ch_cnt_t p = 0; p < pairs; ++p)
		{
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				QVERIFY2(data[p][f][0] == 0.0f,
					qPrintable(QString("pair %1 frame %2 left channel not silenced: %3")
						.arg(int(p)).arg(f).arg(double(data[p][f][0]))));
				QVERIFY2(data[p][f][1] == 0.0f,
					qPrintable(QString("pair %1 frame %2 right channel not silenced: %3")
						.arg(int(p)).arg(f).arg(double(data[p][f][1]))));
			}
		}

		// The single-pair bus used by every MixerChannel/AudioBusHandle must
		// keep working (this is the shape the mixer actually creates).
		SampleFrame single[1][frames];
		SampleFrame* singleBus[1] = {single[0]};
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			single[0][f][0] = 0.5f;
			single[0][f][1] = 0.5f;
		}
		AudioBus singlePair{singleBus, 1, frames};
		singlePair.silenceAllChannels();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			QCOMPARE(single[0][f][0], 0.0f);
			QCOMPARE(single[0][f][1], 0.0f);
		}
	}
};

QTEST_GUILESS_MAIN(AudioBusTest)
#include "AudioBusTest.moc"
