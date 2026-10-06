/*
 * RackZoneRoutingTest.cpp - 0.5.0 E20/E38: a rack's key/velocity zones ROUTE notes
 *                           to their chains, and a zone's sample loads and plays.
 *
 * What is proved here, on a real Engine and Mixer with the rack fixture's two
 * pass-through chains (RackTestSupport.h):
 *  - a zoned block reaches exactly one chain (it is summed once, not per chain),
 *    and is spent after the block it was delivered for;
 *  - the claim rules: first match on both ranges, the common path for a note no
 *    zone claims, nothing claimed for a chain a selected rack does not render,
 *    and the channel conditions (a handle with effects of its own is not fed);
 *  - a zone's sample loads (rack.zone_add reports sample_loaded and its frames),
 *    a missing file is kept and reported rather than refused, a voice plays the
 *    file from its start at the note's pitch, and frees itself when its note is
 *    gone; a released voice fades out;
 *  - delivering, voicing and rendering allocate nothing (AllocationProbe.h).
 *
 * Not proved here: a full InstrumentTrack -> AudioBusHandle period. It needs an
 * instrument plug-in, and the plug-in loader's teardown race docs/RACKS.md
 * section 6 records keeps instruments out of this fixture; the handle's side is
 * RackZoneFeed::run over the same pieces these slots drive.
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
 */

#include <QtTest>

#include <cmath>
#include <cstdint>
#include <vector>

#include <QDataStream>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include "AllocationProbe.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackTestSupport.h"
#include "RackZoneFeed.h"
#include "RackZoneInputs.h"
#include "RackZones.h"
#include "ReversibilityTestSupport.h"
#include "SampleBuffer.h"

using namespace racktest;

namespace
{

//! The keys the routing slots' zones cover.
constexpr int kRoutedLowKey = 100;
constexpr int kWavFrames = 100;
constexpr int kWavRate = 44100;

//! A 16-bit stereo PCM WAV of kWavFrames frames: left = frame / 128, right = -left (exact in 16 bits).
bool writeRampWav(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint32 dataBytes = kWavFrames * 4;
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVEfmt ", 8);
	out << quint32(16) << quint16(1) << quint16(2) << quint32(kWavRate) << quint32(kWavRate * 4) << quint16(4)
		<< quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;
	for (int frame = 0; frame < kWavFrames; ++frame) { out << qint16(frame * 256) << qint16(-frame * 256); }
	return out.status() == QDataStream::Ok;
}

//! A zone over the routed keys, to @a chain.
RackZone routedZone(int chain, int lowKey = kRoutedLowKey, int highKey = RackZoneMaxKey)
{
	RackZone zone;
	zone.lowKey = lowKey;
	zone.highKey = highKey;
	zone.chain = chain;
	return zone;
}

//! A note the zones see, with @a id standing for its play handle.
RackZoneNote noteAt(const void* id, int key, int velocity)
{
	RackZoneNote note;
	note.id = id;
	note.key = key;
	note.velocity = velocity;
	note.framesPlayed = Engine::audioEngine()->framesPerPeriod();
	return note;
}

//! Drops the zones a slot added, back to @a count.
void dropZonesFrom(int count)
{
	while (rackUnderTest().zones().zoneCount() > count) { rackUnderTest().zones().removeZone(count); }
}

} // namespace

class RackZoneRoutingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
		QCOMPARE(rackUnderTest().zones().zoneCount(), 0);
	}

	void cleanupTestCase() { teardownRackFixture(); }

	//! E20: a zoned block arrives at ONE chain. Both chains pass their input through and the rack sums them,
	//! so a block delivered to chain 1 comes out once (0.25) - fed to the common input it would come out twice.
	void aZonedBlockReachesOnlyItsChain()
	{
		Rack& rack = rackUnderTest();
		const int before = rack.zones().zoneCount();
		QCOMPARE(rack.selectedChain(), Rack::Parallel);
		rack.zones().addZone(routedZone(kDrivenChain));
		QVERIFY(rack.zoneInputs().active());
		QVERIFY(rack.zoneInputs().routes(0) && rack.zoneInputs().routes(kDrivenChain));

		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		const std::vector<SampleFrame> block(static_cast<std::size_t>(fpp), SampleFrame{0.25f, -0.25f});
		auto bus = currentBus();  // a silent common input
		rack.zoneInputs().deliver(kDrivenChain, block.data());
		rack.processAudioBuffer(bus);
		QCOMPARE(bus.trackChannelPair(0)[0][0], 0.25f);
		QCOMPARE(bus.trackChannelPair(0)[fpp - 1][1], -0.25f);
		QVERIFY(!bus.quietChannels()[0]);

		// Spent: the next block, with nothing delivered, is silent again.
		auto next = currentBus();
		rack.processAudioBuffer(next);
		QCOMPARE(next.trackChannelPair(0)[0][0], 0.0f);
		printEvidence("zone-block-one-chain", QStringLiteral("delivered 0.25 to chain 1 -> rack output 0.25 (not 0.5)"));
		dropZonesFrom(before);
	}

	//! E20: the claim rules - first match, both ranges, a chain the graph does not render claims nothing, and
	//! the channel conditions (a handle with effects of its own is not fed).
	void theFeedClaimsNotesByKeyAndVelocity()
	{
		Rack& rack = rackUnderTest();
		const int before = rack.zones().zoneCount();
		rack.zones().addZone(routedZone(kDrivenChain, 100, 110));
		RackZone loud = routedZone(0, 111, RackZoneMaxKey);
		loud.lowVelocity = 150;
		rack.zones().addZone(loud);

		const int marker = 0;
		const RackZoneFeed feed(&rack);
		QVERIFY(feed.active());
		QCOMPARE(feed.chainFor(noteAt(&marker, 105, 50)), kDrivenChain);
		QCOMPARE(feed.chainFor(noteAt(&marker, 120, 180)), 0);
		QCOMPARE(feed.chainFor(noteAt(&marker, 120, 100)), -1);  // no zone: the common path
		QCOMPARE(feed.chainFor(noteAt(nullptr, 105, 50)), -1);   // not a note

		// The channel's own conditions: fed as it is; not fed for a handle with effects of its own.
		QVERIFY(RackZoneFeed(static_cast<mix_ch_t>(kChannel), nullptr).active());
		QVERIFY(!RackZoneFeed(static_cast<mix_ch_t>(kChannel), rack.chain(0)).active());

		// A selected rack renders one chain: a zone to another chain claims nothing (heard through the selected).
		rack.setSelectedChain(0);
		const RackZoneFeed selected(&rack);
		QCOMPARE(selected.chainFor(noteAt(&marker, 105, 50)), -1);
		QCOMPARE(selected.chainFor(noteAt(&marker, 120, 180)), 0);
		rack.setSelectedChain(Rack::Parallel);
		printEvidence("zone-claims", QStringLiteral("(105,50)->1 (120,180)->0 (120,100)->-1; selected=0: (105,50)->-1"));
		dropZonesFrom(before);
	}

	//! E38: a zone's sample loads (the command reports it), a missing one is kept and reported, and a voice
	//! plays the file from its start at the note's pitch, then frees itself when its note is gone.
	void aZoneSampleLoadsAndPlays()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("ramp.wav"));
		QVERIFY(writeRampWav(path));
		Rack& rack = rackUnderTest();
		const int before = rack.zones().zoneCount();
		const QString channel = underTestChannel();

		const ControlResult added = revtest::run(QStringLiteral("rack.zone_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("low_key"), kRoutedLowKey},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("sample"), path}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QJsonObject state = added.result.value(QStringLiteral("zone_state")).toObject();
		QVERIFY(state.value(QStringLiteral("sample_loaded")).toBool());
		QCOMPARE(state.value(QStringLiteral("sample_frames")).toInt(), kWavFrames);
		const ControlResult missing = revtest::run(QStringLiteral("rack.zone_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("low_key"), kRoutedLowKey},
				{QStringLiteral("chain"), 0}, {QStringLiteral("sample"), dir.filePath(QStringLiteral("gone.wav"))}});
		QVERIFY2(missing.ok, qPrintable(missing.errorMessage));  // kept, not refused
		QVERIFY(!missing.result.value(QStringLiteral("zone_state")).toObject().value(QStringLiteral("sample_loaded")).toBool());

		const RackZone* zone = rack.zones().zone(before);
		QVERIFY(zone != nullptr && zone->loaded != nullptr);
		const int marker = 0;
		RackZoneNote note = noteAt(&marker, 110, 100);
		// The pitch that plays the file at its own rate whatever the engine's output rate is.
		note.pitch = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / kWavRate;
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(static_cast<std::size_t>(fpp), SampleFrame{});
		QVERIFY(rack.zoneInputs().playVoice(note, *zone->loaded, out.data()));
		for (f_cnt_t f = 0; f < std::min<f_cnt_t>(fpp, kWavFrames - 2); ++f)
		{
			QVERIFY2(std::abs(out[f][0] - f / 128.0f) < 1e-4f && std::abs(out[f][1] + f / 128.0f) < 1e-4f,
				qPrintable(QStringLiteral("frame %1: %2").arg(f).arg(out[f][0])));
		}
		QCOMPARE(rack.zoneInputs().busyVoices(), 1);
		rack.zoneInputs().endPeriod();  // its note played this period: kept
		QCOMPARE(rack.zoneInputs().busyVoices(), 1);
		rack.zoneInputs().endPeriod();  // a period without its note: freed
		QCOMPARE(rack.zoneInputs().busyVoices(), 0);
		printEvidence("zone-sample", QStringLiteral("ramp.wav loaded=%1 frames; voice reproduces it; freed after its note").arg(kWavFrames));
		dropZonesFrom(before);
	}

	//! E38: a released note's voice fades out over FadeFrames and then ends.
	void aReleasedVoiceFadesOut()
	{
		const SampleBuffer flat(std::vector<SampleFrame>(4096, SampleFrame{1.0f, 1.0f}), kWavRate);
		RackZoneVoice voice;
		voice.restart();
		std::vector<SampleFrame> out(2 * RackZoneVoice::FadeFrames, SampleFrame{});
		QVERIFY(!voice.render(flat, out.data(), static_cast<f_cnt_t>(out.size()), 1.0, 1.0f, true));
		QVERIFY(out[0][0] > 0.9f);
		QVERIFY(out[RackZoneVoice::FadeFrames / 2][0] < out[0][0]);
		QCOMPARE(out[RackZoneVoice::FadeFrames + 8][0], 0.0f);
	}

	//! E38: a voice renders the sample's LAST source frame (a one-frame sample sounds, it is not silence).
	void aVoiceRendersTheLastSourceFrame()
	{
		const SampleBuffer one(std::vector<SampleFrame>{{0.5f, -0.5f}}, kWavRate);
		RackZoneVoice voice;
		voice.restart();
		std::vector<SampleFrame> out(4, SampleFrame{});
		QVERIFY(!voice.render(one, out.data(), static_cast<f_cnt_t>(out.size()), 1.0, 1.0f, false));
		QCOMPARE(out[0][0], 0.5f);
		QCOMPARE(out[0][1], -0.5f);
		QCOMPARE(out[1][0], 0.0f);

		const SampleBuffer three(std::vector<SampleFrame>{{1.0f, 1.0f}, {2.0f, 2.0f}, {3.0f, 3.0f}}, kWavRate);
		RackZoneVoice tail;
		tail.restart();
		std::vector<SampleFrame> tailOut(8, SampleFrame{});
		QVERIFY(!tail.render(three, tailOut.data(), static_cast<f_cnt_t>(tailOut.size()), 1.0, 1.0f, false));
		QCOMPARE(tailOut[0][0], 1.0f);
		QCOMPARE(tailOut[1][0], 2.0f);
		QCOMPARE(tailOut[2][0], 3.0f);  // the last source frame, not dropped
		QCOMPARE(tailOut[3][0], 0.0f);
	}

	//! E20/E38 on the audio path: delivering zoned notes, playing a zone's sample and rendering the rack
	//! allocate nothing.
	void zonedNotesAllocateNothing()
	{
		Rack& rack = rackUnderTest();
		const int before = rack.zones().zoneCount();
		rack.zones().addZone(routedZone(kDrivenChain));
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		const std::vector<SampleFrame> block(static_cast<std::size_t>(fpp), SampleFrame{0.1f, 0.1f});
		std::vector<SampleFrame> scratch(static_cast<std::size_t>(fpp), SampleFrame{});
		const SampleBuffer flat(std::vector<SampleFrame>(4096, SampleFrame{0.5f, 0.5f}), kWavRate);
		const int marker = 0;
		auto bus = currentBus();
		rack.processAudioBuffer(bus);  // the first block may touch lazy internals

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		rack.zoneInputs().playVoice(noteAt(&marker, 110, 100), flat, scratch.data());
		rack.zoneInputs().deliver(kDrivenChain, block.data());
		rack.processAudioBuffer(bus);
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;

		printEvidence("zone-routing-allocations", QStringLiteral("allocations=%1").arg(static_cast<qulonglong>(allocations)));
		QCOMPARE(allocations, std::uint64_t{0});
		rack.zoneInputs().endPeriod();
		dropZonesFrom(before);
	}
};

QTEST_GUILESS_MAIN(RackZoneRoutingTest)
#include "RackZoneRoutingTest.moc"
