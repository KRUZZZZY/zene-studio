/*
 * ControlSessionRecordLandPlaybackTest.cpp - a landed clip carries the session cell's playback settings
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

/*! CLUSTER new-p12-43. `session.arrangement_record_land` copied a cell's content and set the
 *  span and the name, and nothing else: the clip it wrote played the same audio at UNITY GAIN
 *  and straight through, whatever gain and loop region the cell was launched with. The launch
 *  applies both on top of the audio it publishes (`SessionAudioCells.cpp:261-264`, the `shape`
 *  the renderer reads), so a cell launched at -6 dB and looped over one tick landed a clip that
 *  was twice as loud and did not loop at all.
 *
 *  The two values are compared against THE LAUNCHER'S OWN NUMBERS - `sessionaudio::prepare()`
 *  for the same cell and tempo, which is what the render path played - never against a
 *  hardcoded frame count, so the test says "the landed clip sounds what the cell sounded"
 *  rather than "the fix wrote the constant I expected". The cell is played from the scheduler
 *  (not a hand-written ring), so the pass lands the buffer the launch published.
 *
 *  The MIDI half of the finding ("transpose 12 ... sounds different") is NOT a defect and the
 *  test pins the decision: a MIDI cell's launch publishes only its pattern, its start offset and
 *  legato - no gain, no transpose, no loop length (`SessionPlayback.cpp:224-227`) - so a landed
 *  MIDI clip that invented them would be the one that sounds different.
 *
 *  WHY THIS IS ITS OWN BINARY. ctest pads a two-digit test number (`Test  #45:`), and the repo's
 *  mechanical negative control reads the test names out of `ctest -N` with `Test #\d+: (\S+)`;
 *  it can only see the high-numbered names. `tests/CMakeLists.txt` therefore registers this
 *  source at the END of `LMMS_TESTS`, where the number is four digits.
 */

#include <QtTest>

#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BrowserTestSupport.h"
#include "Clip.h"
#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SessionArrangementRecorder.h"
#include "SessionAudioCells.h"
#include "SessionModel.h"
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

//! -6.0206 dB is 0.5 linear, the pair ClipEditsTest's gain conversions assert.
constexpr double kHalfGainDb = -6.0206;

} // namespace

class ControlSessionRecordLandPlaybackTest : public QObject
{
	Q_OBJECT

	Song* m_song = nullptr;
	QTemporaryDir m_dir;
	QString m_source;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy render thread would be a SECOND producer on the session scheduler, and the
		// periods below are the only ones this test wants it to see (the repo's own idiom -
		// ControlSessionRecordLandTest).
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		m_song = Engine::getSong();
		QVERIFY(m_dir.isValid());
		// The fixture at the ENGINE's rate, so the clip the pass copies needs no resampling and the
		// launcher's loop frames and the landed clip's are the same numbers.
		browserfixture::WaveSpec spec;
		spec.sampleRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		m_source = m_dir.filePath(QStringLiteral("playback-settings.wav"));
		QVERIFY(browserfixture::writeWave(m_source, spec));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		m_song->clearProject();
		QVERIFY(run(QStringLiteral("session.set_grid"),
			{{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 1}}).ok);
		SessionArrangementRecorder::Event ignored;
		while (recorder().pop(ignored)) {}
		recorder().setArmed(true);
	}

	void cleanup() { recorder().setArmed(false); }

	//! An audio cell on a one-track project; the caller's settings are the cell's own.
	bool setAudioCell(const QJsonObject& settings)
	{
		QJsonObject args{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), m_source}};
		for (auto it = settings.begin(); it != settings.end(); ++it) { args.insert(it.key(), it.value()); }
		return run(QStringLiteral("session.set_slot"), args).ok;
	}

	/*! The cell plays from the session for one span: published, launched on the line, stopped
	 *  @a stopTick ticks later. The recorder taped the launch with the buffer the launch
	 *  published, which is what the pass copies. False when the engine refused a step (the caller
	 *  fails on it: a QVERIFY in a helper would only leave the helper). */
	bool playOneSpan(int stopTick)
	{
		SessionScheduler& scheduler = m_song->sessionScheduler();
		if (publishSessionSlotContent(scheduler, m_song->sessionModel()) <= 0) { return false; }
		if (!scheduler.requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None)) { return false; }
		SessionClockContext ctx;
		ctx.ticksPerBar = 192;
		ctx.framesPerTick = 1.0f;
		ctx.transportRunning = true;
		scheduler.processAudio(ctx, 16);
		ctx.positionTicks = stopTick;
		if (!scheduler.requestStop(0, 0, LaunchMode::Trigger, LaunchQuantisation::None)) { return false; }
		scheduler.processAudio(ctx, 16);
		return true;
	}

	//! What the launcher renders for the cell as it stands now - the numbers the clip must carry.
	sessionaudio::Prepared playedShape()
	{
		const ClipSlot& slot = m_song->sessionModel().slot(0, 0);
		return sessionaudio::prepare(*m_song, 0, slot, m_song->getTempo());
	}

	void anAudioCellsGainAndLoopReachTheLandedClip()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(setAudioCell({{QStringLiteral("gain_db"), kHalfGainDb}, {QStringLiteral("loop_start"), 2},
			{QStringLiteral("loop_length"), 1}, {QStringLiteral("name"), QStringLiteral("Looped")}}));
		QVERIFY(playOneSpan(192));

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(track->getClips().size(), size_t{1});
		auto* copied = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);

		const sessionaudio::Prepared played = playedShape();
		QVERIFY(played.clip != nullptr);
		QCOMPARE(copied->clipEdits().gain, played.shape.gain);
		QVERIFY2(std::fabs(copied->clipEdits().gain - 0.5f) < 1e-4f,
			qPrintable(QStringLiteral("the landed clip plays at gain %1, not the cell's 0.5")
				.arg(copied->clipEdits().gain)));
		QVERIFY2(copied->clipEdits().hasAudioLoop(), "the landed clip does not loop the region the cell looped");
		QCOMPARE(copied->clipEdits().audioLoopStart, played.shape.loopStartFrames);
		QCOMPARE(copied->clipEdits().audioLoopFrames, played.shape.loopLengthFrames);
	}

	//! The other half of the same rule: a cell with nothing set lands a clip with nothing set, so a
	//! project that never used these settings keeps writing clips with no new attributes.
	void aCellWithNoPlaybackSettingsLandsANeutralClip()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(setAudioCell({{QStringLiteral("name"), QStringLiteral("Plain")}}));
		QVERIFY(playOneSpan(192));

		QVERIFY2(run(QStringLiteral("session.arrangement_record_land")).ok, "land");
		QCOMPARE(track->getClips().size(), size_t{1});
		auto* copied = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);
		QVERIFY2(copied->clipEdits().isNeutral(),
			qPrintable(QStringLiteral("a plain cell landed gain %1 and a loop of %2 frames")
				.arg(copied->clipEdits().gain).arg(copied->clipEdits().audioLoopFrames)));
		QCOMPARE(copied->clipEdits().gain, 1.0f);
		QVERIFY(!copied->clipEdits().hasAudioLoop());
	}

	//! A loop longer than the audio the clip holds: the launcher clamps it to what is left of the
	//! buffer (SessionAudioCells.cpp loopRegion()), and the landed clip loops exactly that, so the
	//! pass still lands a clip instead of refusing one.
	void aLoopLongerThanTheAudioKeepsTheLaunchersClampedRegion()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(setAudioCell({{QStringLiteral("loop_start"), 1}, {QStringLiteral("loop_length"), 100000}}));
		QVERIFY(playOneSpan(192));

		QVERIFY2(run(QStringLiteral("session.arrangement_record_land")).ok, "land");
		QCOMPARE(track->getClips().size(), size_t{1});
		auto* copied = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);
		const sessionaudio::Prepared played = playedShape();
		QVERIFY(played.clip != nullptr);
		QVERIFY2(copied->clipEdits().hasAudioLoop(), "the landed clip lost the cell's loop");
		QCOMPARE(copied->clipEdits().audioLoopStart, played.shape.loopStartFrames);
		QCOMPARE(copied->clipEdits().audioLoopStart + copied->clipEdits().audioLoopFrames,
			static_cast<std::int64_t>(copied->sample().sampleSize()));
	}

	//! The settings are the CELL's when the pass runs - the same rule the name and the fallback
	//! content already follow - so a cell retuned during the performance lands what it holds.
	void theSettingsAreTheCellsWhenThePassRuns()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(setAudioCell({{QStringLiteral("name"), QStringLiteral("Retuned")}}));
		QVERIFY(playOneSpan(192));
		QVERIFY(setAudioCell({{QStringLiteral("gain_db"), kHalfGainDb}, {QStringLiteral("loop_start"), 2},
			{QStringLiteral("loop_length"), 1}}));

		QVERIFY2(run(QStringLiteral("session.arrangement_record_land")).ok, "land");
		QCOMPARE(track->getClips().size(), size_t{1});
		auto* copied = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);
		const sessionaudio::Prepared played = playedShape();
		QVERIFY(played.clip != nullptr);
		QCOMPARE(copied->clipEdits().gain, played.shape.gain);
		QCOMPARE(copied->clipEdits().audioLoopStart, played.shape.loopStartFrames);
		QCOMPARE(copied->clipEdits().audioLoopFrames, played.shape.loopLengthFrames);
	}

	//! The finding's transpose/detune/warp half needs no transfer of its own: the launch bakes them
	//! into the buffer it publishes (SessionAudioBake), and the pass copies THAT buffer - so the
	//! landed clip holds the repitched audio itself instead of a raw file that plays an octave lower.
	void aTransposedAudioCellsPitchSurvivesOnTheLandedClip()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(setAudioCell({{QStringLiteral("transpose"), 12}}));
		QVERIFY(playOneSpan(192));

		const sessionaudio::Prepared played = playedShape();
		QVERIFY(played.clip != nullptr);
		QVERIFY2(played.clip->size() < 1536, "the launcher did not repitch the cell (the source has 2048 frames)");

		QVERIFY2(run(QStringLiteral("session.arrangement_record_land")).ok, "land");
		QCOMPARE(track->getClips().size(), size_t{1});
		auto* copied = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);
		QCOMPARE(copied->sample().sampleSize(), static_cast<std::size_t>(played.clip->size()));
	}

	//! The finding's MIDI half: a MIDI cell's launch renders none of gain, transpose or a loop
	//! length, so the landed MIDI clip keeps its untouched edits and its notes keep their keys.
	void aMidiCellsPlaybackSettingsAreNotInventedOnTheClip()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, m_song));
		QVERIFY(track != nullptr);
		QVERIFY(Track::create(Track::Type::Pattern, m_song) != nullptr);  // pattern 0: set_slot refuses an absent id
		auto* sourceTrack = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(sourceTrack != nullptr);
		Engine::patternStore()->createClipsForPattern(0);
		auto* source = dynamic_cast<MidiClip*>(sourceTrack->getClip(0));
		QVERIFY(source != nullptr);
		source->addNote(Note(TimePos(48), TimePos(0), 67, 100), false);
		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
				{QStringLiteral("gain_db"), kHalfGainDb}, {QStringLiteral("transpose"), 12},
				{QStringLiteral("loop_start"), 2}, {QStringLiteral("loop_length"), 1}}).ok);
		QVERIFY(playOneSpan(192));

		const ControlResult landed = run(QStringLiteral("session.arrangement_record_land"));
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(track->getClips().size(), size_t{1});
		auto* copied = dynamic_cast<MidiClip*>(track->getClips().front());
		QVERIFY(copied != nullptr);
		QCOMPARE(copied->notes().size(), size_t{1});
		QCOMPARE(copied->notes().front()->key(), 67);  // the launch transposed nothing either
		QVERIFY2(copied->clipEdits().isNeutral(),
			qPrintable(QStringLiteral("a MIDI cell's gain/transpose/loop were written onto its landed clip "
				"(gain %1, transpose %2, loop %3)")
				.arg(copied->clipEdits().gain).arg(copied->clipEdits().transpose).arg(copied->clipEdits().loopTicks)));
	}
};

QTEST_MAIN(ControlSessionRecordLandPlaybackTest)
#include "ControlSessionRecordLandPlaybackTest.moc"
