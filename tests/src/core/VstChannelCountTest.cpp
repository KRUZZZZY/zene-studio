/*
 * VstChannelCountTest.cpp - the channel counts a VST plugin reports are only
 *                           converted when the host can hold them
 *
 * The remote client's RemoteVstPlugin.cpp is an out-of-process executable that
 * cannot run inside a test, so the conversion it applies to the plugin's signed
 * numInputs / numOutputs is pinned here on the header it includes
 * (plugins/VstBase/VstChannelCount.h). A count the host cannot hold must be
 * refused, not wrapped: -1 used to become 65535, and 65538 used to become 2 - a
 * valid-looking count the plugin does not agree with.
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

#include <climits>

#include "../../../plugins/VstBase/VstChannelCount.h"

#include "lmms_constants.h"

using lmms::ch_cnt_t;
using lmms::MaxChannelsPerAudioBuffer;
using lmms::vst::toChannelCount;

class VstChannelCountTest : public QObject
{
	Q_OBJECT

private slots:
	void acceptsCountsTheHostCanHold()
	{
		QVERIFY(toChannelCount(0) == ch_cnt_t{0});
		QVERIFY(toChannelCount(1) == ch_cnt_t{1});
		QVERIFY(toChannelCount(2) == ch_cnt_t{2});
		// The host's own upper bound is inclusive.
		QVERIFY(toChannelCount(static_cast<int>(MaxChannelsPerAudioBuffer)) == MaxChannelsPerAudioBuffer);
	}

	void refusesNegativeCounts()
	{
		// -1 used to convert to 65535 and go on to size the host's buffers.
		QVERIFY(!toChannelCount(-1));
		QVERIFY(!toChannelCount(INT_MIN));
	}

	void refusesCountsAboveTheBufferLimit()
	{
		QVERIFY(!toChannelCount(static_cast<int>(MaxChannelsPerAudioBuffer) + 1));
		QVERIFY(!toChannelCount(INT_MAX));
	}

	void refusesCountsThatWrapIntoRange()
	{
		// 65538 used to wrap to 2: a count the plugin does not agree with, accepted
		// as if it were the plugin's own. The same for any multiple of 65536.
		QVERIFY(!toChannelCount(65536));
		QVERIFY(!toChannelCount(65538));
		QVERIFY(!toChannelCount(65535));
	}
};

QTEST_APPLESS_MAIN(VstChannelCountTest)

#include "VstChannelCountTest.moc"
