/*
 * AudioBusTest.cpp - tests for the AudioBus channel handling
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
#include "SampleFrame.h"

using namespace lmms;

class AudioBusTest : public QObject
{
	Q_OBJECT

private slots:
	//! silenceAllChannels() must zero EVERY channel pair of the bus.
	//!
	//! Regression test: m_channelPairs is a count of pairs, not of track
	//! channels, so a loop that stepped by 2 zeroed pair 0, 2, ... and left
	//! every second pair carrying signal. Three pairs make both the "pairs
	//! are skipped" failure and an off-by-one fix visible.
	void SilenceAllChannels()
	{
		constexpr f_cnt_t frames = 48;
		constexpr ch_cnt_t pairs = 3;

		SampleFrame storage[pairs][frames];
		SampleFrame* busData[pairs];
		for (ch_cnt_t p = 0; p < pairs; ++p)
		{
			busData[p] = storage[p];
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				storage[p][f][0] = 0.5f;
				storage[p][f][1] = -0.25f;
			}
		}

		AudioBus bus{busData, pairs, frames};
		QCOMPARE(bus.channelPairs(), pairs);
		QCOMPARE(bus.channels(), static_cast<ch_cnt_t>(pairs * 2));
		QCOMPARE(bus.frames(), frames);

		// every track channel starts with a signal and is assumed non-quiet
		for (ch_cnt_t c = 0; c < pairs * 2; ++c)
		{
			QVERIFY2(!bus.quietChannels()[c],
				qPrintable(QString{"channel %1 unexpectedly quiet before silenceAllChannels()"}
					.arg(c)));
		}

		bus.silenceAllChannels();

		// every pair must be zeroed, not just pair 0, 2, ...
		for (ch_cnt_t p = 0; p < pairs; ++p)
		{
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				QCOMPARE(storage[p][f][0], 0.0f);
				QCOMPARE(storage[p][f][1], 0.0f);
			}
		}

		// and the silence status must cover every used track channel
		for (ch_cnt_t c = 0; c < pairs * 2; ++c)
		{
			QVERIFY2(bus.quietChannels()[c],
				qPrintable(QString{"channel %1 not flagged quiet after silenceAllChannels()"}
					.arg(c)));
		}
	}

	//! Single-pair control: the simple case has always worked and must keep
	//! working after the pair-iteration fix.
	void SilenceAllChannelsSinglePair()
	{
		constexpr f_cnt_t frames = 16;

		SampleFrame storage[frames];
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			storage[f][0] = 0.5f;
			storage[f][1] = -0.25f;
		}
		SampleFrame* busData[1] = { storage };

		AudioBus bus{busData, 1, frames};
		bus.silenceAllChannels();

		for (f_cnt_t f = 0; f < frames; ++f)
		{
			QCOMPARE(storage[f][0], 0.0f);
			QCOMPARE(storage[f][1], 0.0f);
		}
		QVERIFY(bus.quietChannels()[0]);
		QVERIFY(bus.quietChannels()[1]);
	}
};

QTEST_GUILESS_MAIN(AudioBusTest)
#include "AudioBusTest.moc"
