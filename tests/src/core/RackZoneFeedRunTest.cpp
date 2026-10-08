/*
 * RackZoneFeedRunTest.cpp - RackZoneFeed::run routes the notes a rack's zones claim to their chains
 *
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

/*! RackZoneRoutingTest covers the claim rules and the rack's own zone inputs. This drives the feed over real
 *  note play handles, the way AudioBusHandle::doProcessing does: what a handle looks like to the zones (noteOf),
 *  that the notes a zone claims are summed into ONE block per chain and delivered to that chain only, that a
 *  claimed note's own buffer is released so the handle's common pass skips it, that a note no zone claims keeps
 *  its buffer, and that a zone with a loaded sample plays the sample for a sounding note instead. */

#include <QtTest>

#include <vector>

#include "BufferManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "NotePlayHandle.h"
#include "Rack.h"
#include "RackTestSupport.h"
#include "RackZoneFeed.h"
#include "RackZones.h"
#include "SampleBuffer.h"
#include "Song.h"
#include "Track.h"

using namespace racktest;

namespace
{

//! The note play handle the track started for @a key (a note-on through the track's own input path).
NotePlayHandle* startNote(InstrumentTrack* track, int key)
{
	track->processInEvent(MidiEvent(MidiNoteOn, 0, key, 100));
	return track->playingNote(key);
}

//! Fills the handle's own buffer with @a value on both channels; false when the handle has none.
bool fillOwnBuffer(PlayHandle* handle, float value)
{
	const std::span<SampleFrame> own = handle->buffer();
	for (SampleFrame& frame : own) { frame = SampleFrame{value, value}; }
	return own.size() == static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
}

//! One period rendered the way the engine does (its buffer is then in use, and the note has played), so the zones
//! see it as sounding.
void playOnePeriod(NotePlayHandle* handle) { handle->doProcessing(); }

RackZone zoneOver(int lowKey, int highKey, int chain)
{
	RackZone zone;
	zone.lowKey = lowKey;
	zone.highKey = highKey;
	zone.chain = chain;
	return zone;
}

} // namespace

class RackZoneFeedRunTest : public QObject
{
	Q_OBJECT

	InstrumentTrack* m_track = nullptr;

private slots:
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
		BufferManager::init(Engine::audioEngine()->framesPerPeriod());
		NotePlayHandleManager::init();
		m_track = qobject_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase() { teardownRackFixture(); }

	void init()
	{
		while (rackUnderTest().zones().zoneCount() > 0) { rackUnderTest().zones().removeZone(0); }
	}

	void aHandleIsSeenAsItsKeyVelocityPitchAndState()
	{
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		// Not a note play handle: an empty note the zones ignore.
		QVERIFY(RackZoneFeed::noteOf(nullptr, fpp).id == nullptr);

		NotePlayHandle* nph = startNote(m_track, 69);
		QVERIFY(nph != nullptr);
		RackZoneNote note = RackZoneFeed::noteOf(nph, fpp);
		QVERIFY(note.id == nph);
		QCOMPARE(note.key, 69);
		QCOMPARE(note.velocity, static_cast<int>(nph->getVolume()));
		QVERIFY(!note.released);
		QVERIFY2(!note.sounding, "a note that has not played a period yet sounds nothing");
		QCOMPARE(note.framesPlayed, f_cnt_t{0});
		// A4 is the base frequency: pitch ratio 1.
		QVERIFY(std::abs(note.pitch - 1.0) < 1e-3);
		QCOMPARE(note.gain, static_cast<float>(nph->getVolume()) / static_cast<float>(DefaultVolume));

		playOnePeriod(nph);
		note = RackZoneFeed::noteOf(nph, fpp);
		QVERIFY(note.framesPlayed > 0);
		QVERIFY(note.sounding);

		nph->noteOff(0);
		QVERIFY(RackZoneFeed::noteOf(nph, fpp).released);
		nph->mute();
		QVERIFY2(!RackZoneFeed::noteOf(nph, fpp).sounding, "a muted note sounds nothing");
		m_track->silenceAllNotes();
	}

	void claimedNotesAreSummedPerChainDeliveredToItAndTheirBuffersReleased()
	{
		Rack& rack = rackUnderTest();
		rack.zones().addZone(zoneOver(50, 70, kDrivenChain));
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

		NotePlayHandle* a = startNote(m_track, 60);
		NotePlayHandle* b = startNote(m_track, 64);
		NotePlayHandle* outside = startNote(m_track, 90);  // no zone claims it
		QVERIFY(a && b && outside);
		for (NotePlayHandle* handle : {a, b, outside}) { playOnePeriod(handle); }
		QVERIFY(fillOwnBuffer(a, 0.125f));
		QVERIFY(fillOwnBuffer(b, 0.25f));
		QVERIFY(fillOwnBuffer(outside, 0.5f));

		PlayHandleList handles{a, b, outside};
		std::vector<SampleFrame> scratch(static_cast<std::size_t>(fpp), SampleFrame{});
		int gains = 0;
		RackZoneFeed feed(&rack);
		QVERIFY(feed.active());
		QVERIFY(feed.run(handles, scratch, [&] { ++gains; }));
		QCOMPARE(gains, 1);  // one chain, one block

		// The block the chain received is the two claimed notes' sum; the rack plays it once.
		auto bus = currentBus();
		rack.processAudioBuffer(bus);
		QCOMPARE(bus.trackChannelPair(0)[0][0], 0.375f);
		QCOMPARE(bus.trackChannelPair(0)[fpp - 1][1], 0.375f);

		// The claimed notes' buffers are gone (their handles' own pass skips them); the unclaimed one keeps its.
		QVERIFY(a->buffer().empty());
		QVERIFY(b->buffer().empty());
		QVERIFY(!outside->buffer().empty());
		QCOMPARE(outside->buffer()[0][0], 0.5f);
		m_track->silenceAllNotes();
	}

	void nothingClaimedLeavesEverythingToTheCommonPath()
	{
		Rack& rack = rackUnderTest();
		rack.zones().addZone(zoneOver(100, 110, kDrivenChain));
		NotePlayHandle* low = startNote(m_track, 40);
		QVERIFY(low != nullptr);
		playOnePeriod(low);
		QVERIFY(fillOwnBuffer(low, 0.5f));
		PlayHandleList handles{low};
		std::vector<SampleFrame> scratch(static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()), SampleFrame{});
		int gains = 0;
		QVERIFY(!RackZoneFeed(&rack).run(handles, scratch, [&] { ++gains; }));
		QCOMPARE(gains, 0);
		QVERIFY(!low->buffer().empty());

		// A feed that is not active (no rack) does nothing at all.
		QVERIFY(!RackZoneFeed(static_cast<Rack*>(nullptr)).active());
		QVERIFY(!RackZoneFeed(static_cast<Rack*>(nullptr)).run(handles, scratch, [&] { ++gains; }));
		QCOMPARE(gains, 0);
		m_track->silenceAllNotes();
	}

	void aBufferOfTheWrongSizeIsNotAddedButStillReleased()
	{
		Rack& rack = rackUnderTest();
		rack.zones().addZone(zoneOver(50, 70, kDrivenChain));
		NotePlayHandle* a = startNote(m_track, 60);
		QVERIFY(a != nullptr);
		playOnePeriod(a);
		QVERIFY(fillOwnBuffer(a, 0.5f));
		PlayHandleList handles{a};
		// A scratch block of another length than the handle's buffer: nothing can be added from it, so the
		// chain gets no block at all (the zone claimed the note, but there was nothing to render).
		std::vector<SampleFrame> scratch(8, SampleFrame{});
		int gains = 0;
		QVERIFY(RackZoneFeed(&rack).run(handles, scratch, [&] { ++gains; }));
		QCOMPARE(gains, 0);
		QVERIFY(a->buffer().empty());
		m_track->silenceAllNotes();
	}

	void aZonesSamplePlaysForASoundingNoteInsteadOfTheInstrument()
	{
		Rack& rack = rackUnderTest();
		RackZone zone = zoneOver(50, 70, kDrivenChain);
		zone.loaded = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(4096, SampleFrame{0.25f, 0.25f}),
			Engine::audioEngine()->outputSampleRate());
		rack.zones().addZone(zone);

		NotePlayHandle* sounding = startNote(m_track, 69);
		NotePlayHandle* notYet = startNote(m_track, 65);
		QVERIFY(sounding && notYet);
		playOnePeriod(sounding);
		// notYet starts in a later period: it renders one (its buffer is in use) but has played nothing.
		notYet->setOffset(Engine::audioEngine()->framesPerPeriod() * 2);
		playOnePeriod(notYet);
		QVERIFY(RackZoneFeed::noteOf(notYet, Engine::audioEngine()->framesPerPeriod()).framesPlayed == 0);
		QVERIFY(fillOwnBuffer(sounding, 0.9f));
		QVERIFY(fillOwnBuffer(notYet, 0.9f));
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> scratch(static_cast<std::size_t>(fpp), SampleFrame{});
		int gains = 0;
		PlayHandleList handles{sounding, notYet};
		QVERIFY(RackZoneFeed(&rack).run(handles, scratch, [&] { ++gains; }));
		QCOMPARE(gains, 1);
		auto bus = currentBus();
		rack.processAudioBuffer(bus);
		// The sample at the note's gain (a velocity of 100 is a volume above the default 100), not the instrument's 0.9.
		const float expected = 0.25f * RackZoneFeed::noteOf(sounding, fpp).gain;
		QVERIFY2(std::abs(bus.trackChannelPair(0)[1][0] - expected) < 5e-3f,
			qPrintable(QStringLiteral("%1 expected %2").arg(bus.trackChannelPair(0)[1][0]).arg(expected)));
		m_track->silenceAllNotes();
		rack.zoneInputs().endPeriod();
		rack.zoneInputs().endPeriod();
	}
};

QTEST_GUILESS_MAIN(RackZoneFeedRunTest)
#include "RackZoneFeedRunTest.moc"
