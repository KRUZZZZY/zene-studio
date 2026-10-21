/*
 * ControlSessionRecordLandTest.cpp - session.arrangement_record_land turns a recorded performance into arrangement clips
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

/*! The recorder's ring is written by the audio thread (a slot started, a slot stopped, each with the tick it fired
 *  on); session.arrangement_record_land pairs every start with the stop that ended it and creates ONE arrangement
 *  clip per pair on that column's song track. Here the ring is filled directly, as the scheduler does, and the
 *  command is invoked through the registry: the pairing (including a retriggered cell and an unmatched stop), the
 *  clip's place, length and name, the columns with no song track, the empty spans, the refusal while a launch is
 *  still open (which must consume nothing), the empty ring, and the single undo step that takes the whole pass back. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <tuple>
#include <vector>

#include "BrowserTestSupport.h"
#include "Clip.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SessionArrangementRecorder.h"
#include "SessionPlayback.h"
#include "SessionScheduler.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

SessionArrangementRecorder& recorder() { return Engine::getSong()->sessionScheduler().arrangementRecorder(); }

//! Events waiting in the ring.
size_t pending()
{
	SessionArrangementRecorder::Event events[SessionArrangementRecorder::Capacity];
	return recorder().snapshot(events, SessionArrangementRecorder::Capacity);
}

QJsonObject landedEntry(const ControlResult& result, int index)
{
	return result.result.value(QStringLiteral("landed")).toArray().at(index).toObject();
}

} // namespace

class ControlSessionRecordLandTest : public QObject
{
	Q_OBJECT

	Song* m_song = nullptr;
	Track* m_first = nullptr;
	Track* m_second = nullptr;
	QTemporaryDir m_dir;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_song = Engine::getSong();
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Two song tracks, a 3x2 session grid (so column 2 has no song track) and an armed, empty ring.
	void init()
	{
		m_song->clearProject();
		m_first = Track::create(Track::Type::Instrument, m_song);
		m_second = Track::create(Track::Type::Instrument, m_song);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 3}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0}, {QStringLiteral("name"), QStringLiteral("Verse")}}).ok);
		SessionArrangementRecorder::Event ignored;
		while (recorder().pop(ignored)) {}
		recorder().setArmed(true);
	}

	void cleanup() { recorder().setArmed(false); }

	void anEmptyRingIsRefusedWithAHint()
	{
		const ControlResult empty = run(QStringLiteral("session.arrangement_record_land"));
		QCOMPARE(empty.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(empty.errorMessage.contains(QStringLiteral("nothing has been recorded")));
	}

	void everyPairBecomesOneClipAtItsRecordedTicks()
	{
		QVERIFY(recorder().recordStart(0, 0, 96));
		QVERIFY(recorder().recordStart(1, 1, 120));
		QVERIFY(recorder().recordStop(0, 0, 288));
		QVERIFY(recorder().recordStop(1, 1, 216));

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("pairs")).toInt(), 2);
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 2);
		QCOMPARE(landed.result.value(QStringLiteral("events")).toInt(), 4);
		QCOMPARE(landed.result.value(QStringLiteral("skipped_columns")).toInt(), 0);
		QCOMPARE(landed.result.value(QStringLiteral("collapsed")).toInt(), 0);
		QCOMPARE(landed.result.value(QStringLiteral("unmatched_stops")).toInt(), 0);
		QVERIFY(landed.result.value(QStringLiteral("armed")).toBool());

		// Pairs are reported in the order their STOPs arrived: (0,0) first.
		const QJsonObject one = landedEntry(landed, 0);
		QVERIFY(one.value(QStringLiteral("landed")).toBool());
		QCOMPARE(one.value(QStringLiteral("track_index")).toInt(), 0);
		QCOMPARE(one.value(QStringLiteral("position")).toInt(), 96);
		QCOMPARE(one.value(QStringLiteral("length")).toInt(), 192);
		QCOMPARE(one.value(QStringLiteral("name")).toString(), QStringLiteral("Verse"));  // the slot's name
		QCOMPARE(one.value(QStringLiteral("pattern")).toInt(), 0);
		QCOMPARE(one.value(QStringLiteral("content")).toString(), QStringLiteral("midi_pattern_copied"));
		const QJsonObject two = landedEntry(landed, 1);
		QCOMPARE(two.value(QStringLiteral("position")).toInt(), 120);
		QCOMPARE(two.value(QStringLiteral("length")).toInt(), 96);
		QCOMPARE(two.value(QStringLiteral("content")).toString(), QStringLiteral("empty"));  // an empty cell carries no pattern

		// The clips exist on the right tracks, with the recorded place and length.
		QCOMPARE(m_first->getClips().size(), size_t{1});
		QCOMPARE(static_cast<int>(m_first->getClips().front()->startPosition().getTicks()), 96);
		QCOMPARE(static_cast<int>(m_first->getClips().front()->length().getTicks()), 192);
		QCOMPARE(m_second->getClips().size(), size_t{1});
		QCOMPARE(static_cast<int>(m_second->getClips().front()->startPosition().getTicks()), 120);

		// The pass consumed the ring, and ONE undo takes every clip back.
		QCOMPARE(pending(), size_t{0});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_first->getClips().size(), size_t{0});
		QCOMPARE(m_second->getClips().size(), size_t{0});
	}

	void aLandedMidiClipCarriesTheSlotPatternsNotes()
	{
		auto* sourceTrack = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(sourceTrack != nullptr);
		Engine::patternStore()->createClipsForPattern(0);
		auto* source = dynamic_cast<MidiClip*>(sourceTrack->getClip(0));
		QVERIFY(source != nullptr);
		source->addNote(Note(TimePos(48), TimePos(24), 67, 101), false);

		QVERIFY(recorder().recordStart(0, 0, 96));
		QVERIFY(recorder().recordStop(0, 0, 288));
		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));

		auto* copied = dynamic_cast<MidiClip*>(m_first->getClips().front());
		QVERIFY(copied != nullptr);
		QCOMPARE(copied->notes().size(), size_t{1});
		QCOMPARE(copied->notes().front()->key(), 67);
		QCOMPARE(copied->notes().front()->pos().getTicks(), tick_t{24});
		QCOMPARE(copied->notes().front()->length().getTicks(), tick_t{48});
	}

	void aLandedMidiClipUsesThePatternThatWasLaunched()
	{
		auto* sourceTrack = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(sourceTrack != nullptr);
		Engine::patternStore()->createClipsForPattern(0);
		Engine::patternStore()->createClipsForPattern(1);
		auto* original = dynamic_cast<MidiClip*>(sourceTrack->getClip(0));
		auto* replacement = dynamic_cast<MidiClip*>(sourceTrack->getClip(1));
		QVERIFY(original != nullptr);
		QVERIFY(replacement != nullptr);
		original->addNote(Note(TimePos(24), TimePos(0), 67, 100), false);
		replacement->addNote(Note(TimePos(24), TimePos(0), 74, 100), false);

		SessionScheduler& scheduler = m_song->sessionScheduler();
		publishSessionSlotContent(scheduler, m_song->sessionModel());
		QVERIFY(scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		SessionClockContext ctx;
		ctx.ticksPerBar = 192;
		ctx.framesPerTick = 1.0f;
		ctx.transportRunning = true;
		scheduler.processAudio(ctx, 16);

		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 1}}).ok);
		ctx.positionTicks = 96;
		QVERIFY(scheduler.requestStop(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));
		scheduler.processAudio(ctx, 16);

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landedEntry(landed, 0).value(QStringLiteral("pattern")).toInt(), 0);
		auto* copied = dynamic_cast<MidiClip*>(m_first->getClips().front());
		QVERIFY(copied != nullptr);
		QCOMPARE(copied->notes().size(), size_t{1});
		QCOMPARE(copied->notes().front()->key(), 67);
	}

	void aLandedAudioCellCarriesItsSample()
	{
		m_song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(run(QStringLiteral("session.set_grid"),
			{{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 1}}).ok);
		const QString source = m_dir.filePath(QStringLiteral("landed-audio.wav"));
		QVERIFY(browserfixture::writeWave(source, browserfixture::WaveSpec{}));
		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("audio")},
				{QStringLiteral("source"), source}, {QStringLiteral("name"), QStringLiteral("Audio take")}}).ok);

		QVERIFY(recorder().recordStart(0, 0, 0));
		QVERIFY(recorder().recordStop(0, 0, 192));
		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));

		auto* copied = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);
		QCOMPARE(copied->name(), QStringLiteral("Audio take"));
		QCOMPARE(copied->sample().sampleSize(), std::size_t{2048});
		QVERIFY(!copied->sampleFile().isEmpty());
	}

	//! Q050-034: undo takes a landed MIDI clip with its notes away, redo brings back the SAME content - place, length,
	//! and every note's key, start, length and velocity.
	void redoRestoresALandedMidiClipExactly()
	{
		auto* sourceTrack = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(sourceTrack != nullptr);
		Engine::patternStore()->createClipsForPattern(0);
		auto* source = dynamic_cast<MidiClip*>(sourceTrack->getClip(0));
		QVERIFY(source != nullptr);
		source->addNote(Note(TimePos(48), TimePos(24), 67, 101), false);
		source->addNote(Note(TimePos(12), TimePos(96), 60, 55), false);

		QVERIFY(recorder().recordStart(0, 0, 96));
		QVERIFY(recorder().recordStop(0, 0, 288));
		QVERIFY2(run(QStringLiteral("session.arrangement_record_land")).ok, "land");

		const auto snapshot = [this] {
			QList<QList<int>> rows;
			auto* clip = dynamic_cast<MidiClip*>(m_first->getClips().front());
			rows.append({static_cast<int>(clip->startPosition().getTicks()), static_cast<int>(clip->length().getTicks())});
			for (const Note* note : clip->notes())
			{
				rows.append({note->key(), static_cast<int>(note->pos().getTicks()),
					static_cast<int>(note->length().getTicks()), static_cast<int>(note->getVolume())});
			}
			std::sort(rows.begin() + 1, rows.end());
			return rows;
		};
		QCOMPARE(m_first->getClips().size(), size_t{1});
		const auto landedRows = snapshot();
		QCOMPARE(landedRows.size(), 3);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_first->getClips().size(), size_t{0});
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(m_first->getClips().size(), size_t{1});
		QCOMPARE(snapshot(), landedRows);
		// and a second round trip is stable too
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(snapshot(), landedRows);
	}

	//! Q050-034: the same for an audio cell: after undo and redo the clip references the same file and holds the same
	//! rendered frames (the sample buffer's size and every frame).
	void redoRestoresALandedAudioClipAndItsRender()
	{
		m_song->clearProject();
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(run(QStringLiteral("session.set_grid"),
			{{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 1}}).ok);
		const QString source = m_dir.filePath(QStringLiteral("redo-audio.wav"));
		QVERIFY(browserfixture::writeWave(source, browserfixture::WaveSpec{}));
		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("audio")},
				{QStringLiteral("source"), source}, {QStringLiteral("name"), QStringLiteral("Audio take")}}).ok);
		QVERIFY(recorder().recordStart(0, 0, 48));
		QVERIFY(recorder().recordStop(0, 0, 240));
		QVERIFY(run(QStringLiteral("session.arrangement_record_land")).ok);

		const auto render = [track] {
			auto* clip = dynamic_cast<SampleClip*>(track->getClips().front());
			std::vector<float> frames;
			for (std::size_t i = 0; i < clip->sample().sampleSize(); ++i)
			{
				frames.push_back(clip->sample().data()[i].left());
				frames.push_back(clip->sample().data()[i].right());
			}
			return std::make_tuple(clip->sampleFile(), static_cast<int>(clip->startPosition().getTicks()),
				static_cast<int>(clip->length().getTicks()), frames);
		};
		QCOMPARE(track->getClips().size(), size_t{1});
		const auto landed = render();
		QVERIFY(!std::get<0>(landed).isEmpty());
		QVERIFY(!std::get<3>(landed).empty());
		bool audible = false;
		for (const float f : std::get<3>(landed)) { audible = audible || f != 0.0f; }
		QVERIFY2(audible, "the landed clip renders silence");

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->getClips().size(), size_t{0});
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(track->getClips().size(), size_t{1});
		QVERIFY(render() == landed);
	}

	void aRetriggeredCellLandsAsTwoClips()
	{
		QVERIFY(recorder().recordStart(0, 0, 0));
		QVERIFY(recorder().recordStop(0, 0, 48));
		QVERIFY(recorder().recordStart(0, 0, 96));
		QVERIFY(recorder().recordStop(0, 0, 192));
		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 2);
		QCOMPARE(m_first->getClips().size(), size_t{2});
	}

	//! A tap switched off while a cell is still playing must not strand the
	//! start it has already recorded: the stop that ends the cell closes it,
	//! and the performance lands. Without that, the start stays open, the pass
	//! refuses it every time, and nothing consumes it - the performance can
	//! never be landed at all.
	void aDisarmedTapStillLandsThePerformanceItRecorded()
	{
		QVERIFY( recorder().recordStart( 0, 0, 0 ) );
		// The tap is switched off while the cell is still playing.
		recorder().setArmed( false );
		// The cell ends after the disarm: the stop that closes the recorded
		// start is taped anyway.
		QVERIFY( recorder().recordStop( 0, 0, 192 ) );

		const ControlResult landed = run( QStringLiteral("session.arrangement_record_land") );
		QVERIFY2( landed.ok, qPrintable( landed.errorMessage ) );
		QCOMPARE( landed.result.value( QStringLiteral("pairs") ).toInt(), 1 );
		QCOMPARE( landed.result.value( QStringLiteral("clips") ).toInt(), 1 );
		QCOMPARE( m_first->getClips().size(), size_t{ 1 } );
		QCOMPARE( static_cast<int>( m_first->getClips().front()->startPosition().getTicks() ), 0 );
		QCOMPARE( static_cast<int>( m_first->getClips().front()->length().getTicks() ), 192 );
		// The pass consumed the ring, so a second pass has nothing to do.
		QCOMPARE( pending(), size_t{ 0 } );
	}

	void aColumnWithoutASongTrackAndAnEmptySpanAreReportedNotLanded()
	{
		QVERIFY(recorder().recordStart(2, 0, 0));    // column 2: the grid is wider than the song's track list
		QVERIFY(recorder().recordStop(2, 0, 96));
		QVERIFY(recorder().recordStart(1, 0, 48));   // stopped on the tick it started: nothing to make
		QVERIFY(recorder().recordStop(1, 0, 48));
		QVERIFY(recorder().recordStop(0, 1, 10));    // a stop whose start was never recorded
		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("pairs")).toInt(), 2);
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 0);
		QCOMPARE(landed.result.value(QStringLiteral("skipped_columns")).toInt(), 1);
		QCOMPARE(landed.result.value(QStringLiteral("collapsed")).toInt(), 1);
		QCOMPARE(landed.result.value(QStringLiteral("unmatched_stops")).toInt(), 1);
		QCOMPARE(landedEntry(landed, 0).value(QStringLiteral("reason")).toString(), QStringLiteral("column_has_no_song_track"));
		QCOMPARE(landedEntry(landed, 1).value(QStringLiteral("reason")).toString(), QStringLiteral("empty_span"));
		QCOMPARE(m_first->getClips().size() + m_second->getClips().size(), size_t{0});
		QCOMPARE(pending(), size_t{0});
	}

	//! A start with no stop yet: the pass is refused, names what still plays, and consumes NOTHING.
	void aStillOpenLaunchRefusesAndConsumesNothing()
	{
		QVERIFY(recorder().recordStart(0, 0, 0));
		QVERIFY(recorder().recordStop(0, 0, 96));
		QVERIFY(recorder().recordStart(1, 1, 120));
		const size_t before = pending();
		const ControlResult refused = run(QStringLiteral("session.arrangement_record_land"));
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("track 1, scene 1")), qPrintable(refused.errorMessage));
		QVERIFY(refused.errorMessage.contains(QStringLiteral("tick 120")));
		QCOMPARE(pending(), before);
		QCOMPARE(m_first->getClips().size() + m_second->getClips().size(), size_t{0});

		// Once the performance ends, the same events land.
		QVERIFY(recorder().recordStop(1, 1, 216));
		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 2);
	}

	//! A full ring still lands: the ring holds two events for each of the scheduler's 64 slots.
	void aWorstCasePerformanceLandsWholly()
	{
		for (int i = 0; i < 64; ++i) { QVERIFY(recorder().recordStart(i % 2, 0, i * 10)); }
		for (int i = 0; i < 64; ++i) { QVERIFY(recorder().recordStop(i % 2, 0, i * 10 + 5)); }
		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("events")).toInt(), 128);
		// Starts and stops were paired by cell, first start with the first stop: 64 pairs.
		QCOMPARE(landed.result.value(QStringLiteral("pairs")).toInt(), 64);
		QCOMPARE(landed.result.value(QStringLiteral("clips")).toInt(), 64);
	}
};

QTEST_MAIN(ControlSessionRecordLandTest)
#include "ControlSessionRecordLandTest.moc"
