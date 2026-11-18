/*
 * LinkAudioStreamTest.cpp - the PCM stream between instances (Q050-024,
 *                           include/LinkAudioStream.h, docs/LINK-AUDIO.md).
 *
 * HERMETIC: every socket here is bound to 127.0.0.1 and every peer is 127.0.0.1. The one test that
 * runs Ableton Link confines its discovery to the loopback interface first (see pcmStreamsWhileLinkSyncIsActive).
 * Nothing is multicast or broadcast beyond the loopback interface (the Link test confines Link's discovery
 * to it), there is no name lookup, UPnP or internet.
 *
 * Proof of the acceptance criteria: the codec, the ring and the refusals pin the
 * building blocks; loopbackExchangeIsSampleExact moves a counting ramp from one
 * stream to another over a real UDP socket and compares every sample;
 * audioThreadAllocatesNothing counts operator new on the calling thread through
 * the shared AllocationProbe; realtimeStressReportsBoundedUnderrunsAndLatency runs
 * two threads on a 1 ms cadence (a 48 kHz, 48-frame period each) for 3 s and
 * bounds underruns, buffered latency and ordering; the command tests show the
 * registered surface, its undo, and that link.* (tempo, quantum) is untouched.
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
 */

#include <QtTest>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include <QFile>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUdpSocket>

#include <fstream>
#include <optional>
#include <sstream>

#include "AbletonLinkSession.h"
#include "LinkAudioStreamTestSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "LinkAudioStream.h"
#include "SampleFrame.h"
// Naming the probe is what links it (tests/CMakeLists.txt); its operator new is the whole program's.
#include "AllocationProbe.h"
#include "LinkRealtimeProbe.h"

using namespace lmms;
using namespace lmms::link;
using namespace lmms::test::linkaudio;

namespace
{


ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}


} // namespace

namespace lmms::link
{

//! The receive jitter bounds and RX ring, exposed only to this regression.
struct LinkAudioStreamTestAccess
{
	static bool jitterBoundsAreOrdered(std::size_t period)
	{
		const auto bounds = LinkAudioStream::jitterBounds(period, AudioRingFrames);
		return bounds.minimum <= bounds.maximum;
	}

	static std::size_t jitterMinimum(std::size_t period, std::size_t capacity)
	{
		return LinkAudioStream::jitterBounds(period, capacity).minimum;
	}

	static std::size_t jitterMaximum(std::size_t period, std::size_t capacity)
	{
		return LinkAudioStream::jitterBounds(period, capacity).maximum;
	}

	static void enableReceiver(LinkAudioStream& stream, int latencyMs)
	{
		stream.m_latencyMs.store(latencyMs, std::memory_order_relaxed);
		stream.m_receiveOn.store(true, std::memory_order_relaxed);
		stream.m_epoch.fetch_add(1, std::memory_order_release);
	}

	static std::size_t writeRx(LinkAudioStream& stream, const float* samples, std::size_t frames)
	{
		return stream.m_rx.write(samples, frames);
	}
};

} // namespace lmms::link

class LinkAudioStreamTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	void packetCodecRoundTripsAndRejectsGarbage();

	void nonFiniteSamplesInAValidPacketAreSilencedNotMixed();

	void ipv4ParserAcceptsLiteralsOnly();

	void ringKeepsOrderAcrossWrapAndRefusesOverflow();

	void refusalsChangeNothing();

	void largePeriodsKeepTheJitterTargetInsideItsSupportedRange();

	void loopbackExchangeIsSampleExact();

	void audioThreadAllocatesNothing();

	void realtimeStressReportsBoundedUnderrunsAndLatency();

	//! A sender that never stops must not keep the socket thread from being joined (bounded drain).
	void disablingWhileFloodedJoinsPromptly();

	void packetsFromAStrangerOrAtAnotherRateAreCounted();

	/*! A sequence gap is replaced by silence. When the RX ring is full that silence does not
	 *  fit, and the frames the ring could not take are an overrun like any other dropped frame:
	 *  they must be counted, or the report hides a hole in the audio. */
	void droppedGapSilenceIsCountedAsAnOverrun();

	/*! The master's frames are SampleFrame objects, not floats: an array of them is not a
	 *  float array, and the size of one frame says nothing about the type of its bytes, so
	 *  reading the array through a float* is undefined behaviour. The stream reads the frames
	 *  channel by channel instead - this pins that (the values themselves are pinned by
	 *  loopbackExchangeIsSampleExact, which compares every sample of the round trip). */
	void theFrameArrayIsNotReadAsAFlatFloatArray();

	/*! The engine's sample rate changed (the audio device was switched): the frames queued for
	 *  playback were captured at the old rate and must not be played at the new one. */

	/*! The other half of the same change: the frames queued for sending were captured at the old
	 *  rate and must not go out with the new rate in their header (the peer would play old-rate
	 *  samples at the new rate). The stream must also keep working at the new rate.
	 *
	 *  The I/O thread is held before it sends, so the frames of the TX ring are certainly still
	 *  queued when the rate changes: the test does not depend on how the two threads are
	 *  scheduled. */

	/*! The engine's sample rate changed (the audio device was switched): the frames queued for
	 *  playback were captured at the old rate and must not be played at the new one. */
	void aSampleRateChangeDropsTheOldRateRxFrames();

	/*! The other half of the same change: the frames queued for sending were captured at the old
	 *  rate and must not go out with the new rate in their header (the peer would play old-rate
	 *  samples at the new rate). The stream must also keep working at the new rate.
	 *
	 *  The I/O thread is held before it sends, so the frames of the TX ring are certainly still
	 *  queued when the rate changes: the test does not depend on how the two threads are
	 *  scheduled. */
	void aSampleRateChangeDoesNotSendOldRateFramesAsNewRate();

	/*! A packet that the I/O thread validated before the sample rate changed must not reach the
	 *  mix at the new rate. The I/O thread holds the packet between the validation and the
	 *  publication, the engine switches to the new rate (which drops the frames of the ring),
	 *  and the packet is published afterwards. */
	void anOldRatePacketIsNotPlayedAtTheNewRate();

	/*! Criterion 1, with Link ACTIVE: two in-process Ableton Link participants whose discovery is
	 *  confined to the loopback interface (AbletonLinkSession::setLoopbackOnly - the scanner seam in
	 *  src/core/AbletonLinkSession.cpp) share a session while PCM streams over 127.0.0.1. Tempo, start and
	 *  stop travel through Link during the stream, the phase agrees, and the audio arrives intact. */
	void pcmStreamsWhileLinkSyncIsActive();

	void commandsAreRegisteredAndUndoable();

	void omittedEnabledIsRefusedAndTheRunningStreamKeepsRunning();

	void theStreamLeavesLinkSyncAlone();
};

#include "LinkAudioStreamTestParts1.h"
#include "LinkAudioStreamTestParts2.h"

QTEST_GUILESS_MAIN(LinkAudioStreamTest)
#include "LinkAudioStreamTest.moc"
