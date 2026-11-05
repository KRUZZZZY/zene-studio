/*
 * SessionAudioRecordTest.cpp - record AUDIO into a launcher cell, on the real engine (0.5.0 L02, WF-055)
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

/*! Q050-277 adds the external-synth capture: 'steps' beside 'bars' (a step is MIDI Clock's beat, a sixteenth note), and
 *  'clock_sync', which makes the MIDI clock master send START when the take starts and STOP at its end with exactly the
 *  span's pulses between. The fixture is the master's own emission monitor on the Dummy MIDI client (what the engine
 *  handed its MIDI client - the bound docs/KNOWN-LIMITATIONS.md states, not bytes a device received), read against the
 *  take's own start tick; the take's WAV is then saved, the project reloaded and the WAV re-read.
 *
 *  THE PROOF for WF-055's audio half, driven through the agent surface on the Dummy device. The test drives
 *  the periods itself (the device is stopped) and feeds the engine's input a KNOWN signal - frame n of the
 *  input stream is a value only n has - so a take can be checked frame by frame against what was fed:
 *   - session.record_slot arms an empty cell of a sample track's column and the take starts on the launch
 *     quantisation's grid line (a bar line), in the period that line is reached;
 *   - its first frame is the input frame one recording round trip after that period began, and every frame
 *     after it follows on, for exactly the span recorded (one bar of frames for 'bars': 1; the bars between the
 *     start and the quantised stop for session.stop_slot);
 *   - session.record_slot_land writes it as the cell's audio clip, and that WAV holds the same PCM (to the 24-bit
 *     file's own quantisation); one control.undo puts the cell back, control.redo lands it again.
 *  Nothing here uses the engine class directly, so the same binary built on the base answers every command
 *  unknown and fails each case: the red half of the proof. */

#include <QtTest>

#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <vector>

#include <sndfile.h>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClock.h"
#include "ProjectJournal.h"
#include "SampleFrame.h"
#include "SessionModel.h"
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

//! Input frame @a n: a value no other frame of the first 2^24 has, exact in a float and in a 24-bit file.
SampleFrame inputFrame(std::uint64_t n)
{
	return SampleFrame(static_cast<float>(n % 4096) / 4096.0f - 0.5f,
		static_cast<float>((n / 4096) % 4096) / 4096.0f - 0.5f);
}

//! Two steps of a 24-bit file: libsndfile writes with 2^23 - 1 and reads with 2^23.
constexpr float Pcm24Tolerance = 2.0f / 8388608.0f;

//! The frames of the WAV at @a path; empty when it cannot be read.
std::vector<SampleFrame> readWav(const QString& path, int* rate)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
	if (file == nullptr || info.channels != 2) { if (file) { sf_close(file); } return {}; }
	std::vector<float> interleaved(static_cast<std::size_t>(info.frames) * 2);
	const sf_count_t read = sf_readf_float(file, interleaved.data(), info.frames);
	sf_close(file);
	*rate = info.samplerate;
	std::vector<SampleFrame> frames(static_cast<std::size_t>(read));
	for (std::size_t i = 0; i < frames.size(); ++i) { frames[i] = SampleFrame(interleaved[2 * i], interleaved[2 * i + 1]); }
	return frames;
}

//! The MIDI clock master on for one scope (the Dummy client takes the messages), off again after.
struct MasterOn
{
	MasterOn()
	{
		QString error;
		on = MidiClock::instance()->setMasterEnabled(true, QString(), &error);
	}
	~MasterOn() { MidiClock::instance()->setMasterEnabled(false, QString(), nullptr); }
	bool on = false;
};

//! True once the sender thread has delivered everything the audio thread queued for the MIDI client (Q050-277).
bool delivered()
{
	for (int i = 0; i < 3000 && MidiClock::instance()->captureOutputPending(); ++i) { QTest::qWait(1); }
	return !MidiClock::instance()->captureOutputPending();
}

//! The monitor's last @a count emissions' names, oldest first.
QStringList lastEmitted(const MidiClock& clock, quint32 count)
{
	QStringList out;
	for (quint32 i = count; i-- > 0;) { out.append(midiClockMessageName(clock.emittedAt(static_cast<int>(i)))); }
	return out;
}

} // namespace

class SessionAudioRecordTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(Track::create(Track::Type::Instrument, song) != nullptr);  // column 0: not an audio track
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		m_column = static_cast<int>(song->tracks().size()) - 1;
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), m_column + 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("bar")}}).ok);
		m_wasPlaying = false;
		for (int i = 0; i < 4; ++i) { period(); }
	}

	//! 'bars': 1 - the take starts on a bar line and is exactly one bar of the input, one round trip on.
	void aCellRecordsOneBarOfItsInputFromTheBarLine()
	{
		const ControlResult armed = run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column},
			{QStringLiteral("scene"), 0}, {QStringLiteral("bars"), 1}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("state")).toString(), QStringLiteral("armed"));
		QVERIFY(recordUntilDone());
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const tick_t bar = Engine::getSong()->ticksPerBar();
		const auto oneBar = static_cast<std::uint64_t>(std::llround(bar * static_cast<double>(Engine::framesPerTick())));
		QCOMPARE(static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble()), oneBar);
		QCOMPARE(static_cast<std::uint64_t>(status.value(QStringLiteral("frames")).toDouble()), oneBar);
		checkQuantisedStart(status);
		landAndCompare(status, oneBar, QStringLiteral("one-bar.wav"));
	}

	//! No length: session.stop_slot ends the take on its own bar line, and the take is that span exactly.
	void aQuantisedStopEndsTheTakeOnItsBarLine()
	{
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 1}}).ok);
		for (int i = 0; i < 4000 && state() != QStringLiteral("recording"); ++i) { period(); }
		QCOMPARE(state(), QStringLiteral("recording"));
		for (int i = 0; i < 40; ++i) { period(); }  // into the first bar
		QVERIFY(run(QStringLiteral("session.stop_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 1}}).ok);
		QVERIFY(recordUntilDone());
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const double barFrames = Engine::getSong()->ticksPerBar() * static_cast<double>(Engine::framesPerTick());
		const auto target = static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble());
		const auto bars = static_cast<std::uint64_t>(std::llround(target / barFrames));
		QVERIFY2(bars >= 1, "the stop ended the take before its first bar line");
		QCOMPARE(target, static_cast<std::uint64_t>(std::llround(bars * barFrames)));  // whole bars: the stop was quantised
		checkQuantisedStart(status);
		landAndCompare(status, target, QStringLiteral("stopped.wav"));
	}

	//! Save, then undo, then redo a landed take, through the journal the way the GUI's Ctrl+Z does (MainWindow::undo):
	//! each of the undo and the redo changes the song, so each leaves it marked modified. The step's own callbacks do
	//! not call setModified(); ProjectJournal::undo()/redo() do. control.undo is not used here: it refuses after a save
	//! (project.save is a non-reversible transaction, so the agent path has nothing to undo past it).
	void undoAndRedoOfALandedTakeMarkTheSongModified()
	{
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column},
			{QStringLiteral("scene"), 0}, {QStringLiteral("bars"), 1}}).ok);
		QVERIFY(recordUntilDone());
		const QString path = m_dir.filePath(QStringLiteral("saved-then-undone.wav"));
		const ControlResult landed = run(QStringLiteral("session.record_slot_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QVERIFY(Engine::getSong()->isModified());

		const QString first = m_dir.filePath(QStringLiteral("landed.mmp"));
		QVERIFY2(run(QStringLiteral("project.save"), {{QStringLiteral("path"), first}}).ok, "the save failed");
		QVERIFY(!Engine::getSong()->isModified());

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr && journal->canUndo());
		journal->undo();
		QVERIFY(Engine::getSong()->sessionModel().slot(m_column, 0).isEmpty());
		QVERIFY2(Engine::getSong()->isModified(), "the undo changed the cell but left the song marked clean");

		const QString second = m_dir.filePath(QStringLiteral("undone.mmp"));
		QVERIFY2(run(QStringLiteral("project.save"), {{QStringLiteral("path"), second}}).ok, "the second save failed");
		QVERIFY(!Engine::getSong()->isModified());

		QVERIFY(journal->canRedo());
		journal->redo();
		QCOMPARE(Engine::getSong()->sessionModel().slot(m_column, 0).audioSource(), path);
		QVERIFY2(Engine::getSong()->isModified(), "the redo changed the cell but left the song marked clean");
	}

	void theSurfaceRefusesWhatItCannotRecord()
	{
		const auto kind = [](const QJsonObject& args) { return run(QStringLiteral("session.record_slot"), args).errorKind; };
		QCOMPARE(kind({{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}}), ControlErrorKind::InvalidArgs);  // an instrument
		QCOMPARE(kind({{QStringLiteral("track"), m_column + 1}, {QStringLiteral("scene"), 0}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("session.record_slot_land")).errorKind, ControlErrorKind::Refused);  // nothing finished
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0}}).ok);
		QCOMPARE(kind({{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 1}}), ControlErrorKind::Busy);  // one take at a time
		const ControlResult cancelled = run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column},
			{QStringLiteral("scene"), 0}, {QStringLiteral("cancel"), true}});
		QVERIFY(cancelled.ok && cancelled.result.value(QStringLiteral("cancelled")).toBool());
		QCOMPARE(state(), QStringLiteral("idle"));
		Engine::getSong()->sessionModel().slot(m_column, 1).setAudioReference(QStringLiteral("/tmp/x.wav"));
		QCOMPARE(kind({{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 1}}), ControlErrorKind::InvalidArgs);  // filled
	}

	//! A cell filled while its take records keeps what was put there: the landing is refused before the take is written,
	//! the take stays in hand, and once the cell is cleared the same take lands.
	void aLandingNeverOverwritesACellFilledWhileItRecorded()
	{
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("bars"), 1}}).ok);
		for (int i = 0; i < 4000 && state() != QStringLiteral("recording"); ++i) { period(); }
		QCOMPARE(state(), QStringLiteral("recording"));
		const QString filled = m_dir.filePath(QStringLiteral("filled.wav"));
		QVERIFY2(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), filled}}).ok, "the cell was not filled");
		QVERIFY(recordUntilDone());

		const QString path = m_dir.filePath(QStringLiteral("overwrite.wav"));
		const ControlResult landed = run(QStringLiteral("session.record_slot_land"), {{QStringLiteral("file"), path}});
		QCOMPARE(landed.errorKind, ControlErrorKind::Refused);
		QVERIFY(!QFileInfo::exists(path));  // refused before the take was written
		QCOMPARE(Engine::getSong()->sessionModel().slot(m_column, 0).audioSource(), filled);
		QCOMPARE(state(), QStringLiteral("done"));  // the take is still in hand

		QVERIFY(run(QStringLiteral("session.clear_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0}}).ok);
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const auto frames = static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble());
		checkQuantisedStart(status);
		landAndCompare(status, frames, QStringLiteral("cleared.wav"));
	}

	//! Q050-277: one bar, clock-synced. START carries the take's own start tick, STOP exactly one bar later, the span's
	//! 96 pulses (a bar is 192 ticks, a pulse 2) are sent between, and nothing goes out after the STOP.
	void aClockSyncedBarTakeSendsStartAndStopAtItsBoundaries()
	{
		MasterOn master;
		QVERIFY(master.on);
		MidiClock* clock = MidiClock::instance();
		const quint32 total = clock->emittedTotal();
		const quint32 starts = clock->emittedCount(MidiClockMessage::Start);
		const quint32 stops = clock->emittedCount(MidiClockMessage::Stop);
		const quint32 pulses = clock->emittedCount(MidiClockMessage::Clock);
		const ControlResult armed = run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column},
			{QStringLiteral("scene"), 0}, {QStringLiteral("bars"), 1}, {QStringLiteral("clock_sync"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(armed.result.value(QStringLiteral("clock_sync")).toBool());
		QCOMPARE(armed.result.value(QStringLiteral("length_unit")).toString(), QStringLiteral("bars"));
		QCOMPARE(clock->emittedTotal(), total);  // armed: the device is not started before the take is
		QVERIFY(recordUntilDone());
		QVERIFY(delivered());

		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const tick_t bar = Engine::getSong()->ticksPerBar();
		const auto startTick = static_cast<tick_t>(status.value(QStringLiteral("start_tick")).toDouble());
		const QJsonObject span = status.value(QStringLiteral("clock")).toObject();
		QVERIFY(!span.value(QStringLiteral("active")).toBool());
		QCOMPARE(static_cast<tick_t>(span.value(QStringLiteral("start_tick")).toDouble()), startTick);  // START: where the take began
		QCOMPARE(static_cast<tick_t>(span.value(QStringLiteral("stop_tick")).toDouble()), startTick + bar);  // STOP: one bar on
		const auto expectedPulses = static_cast<quint32>(bar / TicksPerMidiClockPulse);
		QCOMPARE(static_cast<quint32>(span.value(QStringLiteral("pulses")).toDouble()), expectedPulses);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Start) - starts, 1u);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stops, 1u);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Clock) - pulses, expectedPulses);
		QCOMPARE(clock->emittedTotal() - total, expectedPulses + 2);  // and no SPP, no CONTINUE
		QCOMPARE(clock->emittedAt(0), MidiClockMessage::Stop);
		QCOMPARE(static_cast<tick_t>(clock->emittedTickAt(0)), startTick + bar);
		QCOMPARE(clock->emittedAt(1), MidiClockMessage::Clock);

		const quint32 after = clock->emittedTotal();
		for (int i = 0; i < 40; ++i) { period(); }
		QCOMPARE(clock->emittedTotal(), after);  // the span is closed: the clock is silent again
		landAndCompare(status, static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble()), QStringLiteral("clocked-bar.wav"));
	}

	//! Q050-277: four steps (a sixteenth each, 12 ticks) - the take is 48 ticks of frames, and the whole sequence the
	//! device saw is START, 24 pulses, STOP, in that order.
	void aClockSyncedStepTakeIsExactlyThatManyStepsAndTheDeviceSeesTheOrder()
	{
		MasterOn master;
		QVERIFY(master.on);
		MidiClock* clock = MidiClock::instance();
		const quint32 total = clock->emittedTotal();
		const ControlResult armed = run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column},
			{QStringLiteral("scene"), 0}, {QStringLiteral("steps"), 4}, {QStringLiteral("clock_sync"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("length_unit")).toString(), QStringLiteral("steps"));
		QCOMPARE(armed.result.value(QStringLiteral("length_ticks")).toInt(), 4 * TicksPerMidiBeat);
		QVERIFY(recordUntilDone());
		QVERIFY(delivered());
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const auto frames = static_cast<std::uint64_t>(std::llround(4 * TicksPerMidiBeat * static_cast<double>(Engine::framesPerTick())));
		QCOMPARE(static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble()), frames);
		QCOMPARE(static_cast<std::uint64_t>(status.value(QStringLiteral("frames")).toDouble()), frames);

		QStringList expected{QStringLiteral("start")};
		for (int i = 0; i < 4 * TicksPerMidiBeat / TicksPerMidiClockPulse; ++i) { expected.append(QStringLiteral("clock")); }
		expected.append(QStringLiteral("stop"));
		QCOMPARE(clock->emittedTotal() - total, static_cast<quint32>(expected.size()));
		QCOMPARE(lastEmitted(*clock, static_cast<quint32>(expected.size())), expected);
		const auto startTick = static_cast<tick_t>(status.value(QStringLiteral("start_tick")).toDouble());
		QCOMPARE(static_cast<tick_t>(clock->emittedTickAt(0)), startTick + 4 * TicksPerMidiBeat);
		QCOMPARE(static_cast<tick_t>(clock->emittedTickAt(static_cast<int>(expected.size()) - 1)), startTick);
		landAndCompare(status, frames, QStringLiteral("clocked-steps.wav"));
	}

	//! Q050-277: a take that does not ask for the clock sends nothing, even with the master on; and a cancelled synced
	//! take stops the device it started.
	void anUnsyncedTakeSendsNothingAndACancelledSyncedTakeSendsStop()
	{
		MasterOn master;
		QVERIFY(master.on);
		MidiClock* clock = MidiClock::instance();
		quint32 total = clock->emittedTotal();
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("steps"), 2}}).ok);
		QVERIFY(recordUntilDone());
		QVERIFY(delivered());
		QCOMPARE(clock->emittedTotal(), total);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("cancel"), true}}).ok);

		total = clock->emittedTotal();
		const quint32 stops = clock->emittedCount(MidiClockMessage::Stop);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("bars"), 2}, {QStringLiteral("clock_sync"), true}}).ok);
		for (int i = 0; i < 6000 && state() != QStringLiteral("recording"); ++i) { period(); }
		QCOMPARE(state(), QStringLiteral("recording"));
		for (int i = 0; i < 20; ++i) { period(); }
		QVERIFY(clock->captureReport().active);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("cancel"), true}}).ok);
		QVERIFY(!clock->captureReport().active);
		QVERIFY(delivered());
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stops, 1u);
		QCOMPARE(clock->emittedAt(0), MidiClockMessage::Stop);
		QVERIFY(clock->emittedTotal() > total);
	}

	//! Q050-277: what the surface refuses - both units, or the clock while its master is off - leaves nothing armed.
	void theSurfaceRefusesBothUnitsAndAClockThatIsOff()
	{
		QCOMPARE(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("bars"), 1}, {QStringLiteral("steps"), 4}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(state(), QStringLiteral("idle"));
		QVERIFY(!MidiClock::instance()->masterEnabled());
		const ControlResult off = run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column},
			{QStringLiteral("scene"), 0}, {QStringLiteral("steps"), 4}, {QStringLiteral("clock_sync"), true}});
		QCOMPARE(off.errorKind, ControlErrorKind::Refused);
		QVERIFY2(off.errorMessage.contains(QStringLiteral("clock.master_set")), qPrintable(off.errorMessage));
		QCOMPARE(state(), QStringLiteral("idle"));
	}

	//! Q050-277 (review round 1): the audio thread - this test's own thread, driving the periods - never hands a message to
	//! the MIDI client. With the sender held, a whole synced capture runs and NOTHING is delivered; it is all queued, and
	//! the sender delivers it, in order, once released.
	void theCallbacksOnlyQueueAndTheSenderThreadDelivers()
	{
		MasterOn master;
		QVERIFY(master.on);
		MidiClock* clock = MidiClock::instance();
		const quint32 total = clock->emittedTotal();
		clock->holdCaptureOutput(true);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("steps"), 4}, {QStringLiteral("clock_sync"), true}}).ok);
		QVERIFY(recordUntilDone());
		const bool queued = clock->captureOutputPending();
		const quint32 deliveredByAudioThread = clock->emittedTotal() - total;
		clock->holdCaptureOutput(false);
		QVERIFY(queued);
		QCOMPARE(deliveredByAudioThread, 0u);  // START, the pulses and STOP were all still waiting for the sender
		QVERIFY(delivered());
		QCOMPARE(clock->emittedTotal() - total, static_cast<quint32>(4 * TicksPerMidiBeat / TicksPerMidiClockPulse + 2));
		QCOMPARE(clock->emittedAt(0), MidiClockMessage::Stop);
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		landAndCompare(status, static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble()), QStringLiteral("held.wav"));
	}

	//! Q050-277 (review round 1): the work one callback does does not scale with the capture. A capture longer than the
	//! ring, with the sender held, queues what fits and counts what it refused - no backlog, no hang - and START and STOP
	//! (which have the whole ring) are still delivered once the sender runs.
	void aFullRingRefusesPulsesButStillDeliversStartAndStop()
	{
		MasterOn master;
		QVERIFY(master.on);
		MidiClock* clock = MidiClock::instance();
		const quint32 starts = clock->emittedCount(MidiClockMessage::Start);
		const quint32 stops = clock->emittedCount(MidiClockMessage::Stop);
		const quint32 pulses = clock->emittedCount(MidiClockMessage::Clock);
		const quint32 dropped = clock->captureOutputDropped();
		clock->holdCaptureOutput(true);
		const int bars = 6;  // 6 bars = 576 pulses, more than the ring holds
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("bars"), bars}, {QStringLiteral("clock_sync"), true}}).ok);
		QVERIFY(recordUntilDone());
		clock->holdCaptureOutput(false);
		QVERIFY(clock->captureOutputDropped() > dropped);
		QVERIFY(delivered());
		const quint32 sent = clock->emittedCount(MidiClockMessage::Clock) - pulses;
		QVERIFY2(sent <= MidiClock::RingCapacity - MidiClock::ReservedSlots, qPrintable(QString::number(sent)));
		QCOMPARE(clock->emittedCount(MidiClockMessage::Start) - starts, 1u);
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stops, 1u);
		QCOMPARE(clock->emittedAt(0), MidiClockMessage::Stop);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("cancel"), true}}).ok);
	}

	//! Q050-277 (review round 1): the master turned off mid-capture ends the clock span THERE - STOP once, at the last pulse's
	//! tick, no pulses after it and no catch-up when the cell's own stop comes - and the take goes on to its length.
	void theMasterTurnedOffMidCaptureEndsTheSpanWithoutCatchUp()
	{
		MasterOn master;
		QVERIFY(master.on);
		MidiClock* clock = MidiClock::instance();
		const quint32 stops = clock->emittedCount(MidiClockMessage::Stop);
		const quint32 pulses = clock->emittedCount(MidiClockMessage::Clock);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("bars"), 2}, {QStringLiteral("clock_sync"), true}}).ok);
		for (int i = 0; i < 6000 && state() != QStringLiteral("recording"); ++i) { period(); }
		QCOMPARE(state(), QStringLiteral("recording"));
		for (int i = 0; i < 60; ++i) { period(); }
		QVERIFY(clock->captureReport().active);
		QVERIFY(clock->setMasterEnabled(false, QString(), nullptr));
		period();  // the next period sees the master off and ends the span
		const MidiClock::CaptureReport ended = clock->captureReport();
		QVERIFY(!ended.active);
		QVERIFY(delivered());
		const quint32 pulsesAtOff = clock->emittedCount(MidiClockMessage::Clock) - pulses;
		QCOMPARE(pulsesAtOff, ended.pulses);
		QVERIFY(pulsesAtOff > 0 && pulsesAtOff < static_cast<quint32>(2 * Engine::getSong()->ticksPerBar() / TicksPerMidiClockPulse));
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stops, 1u);
		QCOMPARE(static_cast<tick_t>(ended.stopTick), static_cast<tick_t>(ended.startTick) + static_cast<tick_t>(ended.pulses) * TicksPerMidiClockPulse);

		QVERIFY(recordUntilDone());  // the take goes on to its two bars; the cell's stop finds the span closed
		QVERIFY(delivered());
		QCOMPARE(clock->emittedCount(MidiClockMessage::Clock) - pulses, pulsesAtOff);  // no catch-up
		QCOMPARE(clock->emittedCount(MidiClockMessage::Stop) - stops, 1u);
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const auto frames = static_cast<std::uint64_t>(std::llround(2 * Engine::getSong()->ticksPerBar() * static_cast<double>(Engine::framesPerTick())));
		QCOMPARE(static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble()), frames);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("cancel"), true}}).ok);
	}

	//! Q050-277: the synced take saved with the project and reloaded keeps its length and its place - the cell still names
	//! the WAV, the WAV is still exactly the requested steps of frames, and every frame is still the input one round trip
	//! after the start.
	void aSyncedTakeKeepsItsLengthAndAlignmentAfterSaveAndReload()
	{
		MasterOn master;
		QVERIFY(master.on);
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), m_column}, {QStringLiteral("scene"), 0},
			{QStringLiteral("steps"), 8}, {QStringLiteral("clock_sync"), true}}).ok);
		QVERIFY(recordUntilDone());
		QVERIFY(delivered());
		const QJsonObject status = run(QStringLiteral("session.record_slot_status")).result;
		const auto frames = static_cast<std::uint64_t>(std::llround(8 * TicksPerMidiBeat * static_cast<double>(Engine::framesPerTick())));
		const auto latency = static_cast<std::uint64_t>(status.value(QStringLiteral("latency_frames")).toInt());
		const std::uint64_t first = m_startPeriodFirstFrame + latency;
		const QString wav = m_dir.filePath(QStringLiteral("reloaded.wav"));
		QVERIFY(run(QStringLiteral("session.record_slot_land"), {{QStringLiteral("file"), wav}}).ok);

		const QString project = m_dir.filePath(QStringLiteral("clocked.mmp"));
		QVERIFY2(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok, "the save failed");
		QVERIFY2(run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}}).ok, "the reload failed");
		const ClipSlot& slot = Engine::getSong()->sessionModel().slot(m_column, 0);
		QVERIFY(slot.type() == ClipSlot::Type::Audio);
		QCOMPARE(slot.audioSource(), wav);

		int rate = 0;
		const std::vector<SampleFrame> take = readWav(wav, &rate);
		QCOMPARE(static_cast<std::uint64_t>(take.size()), frames);  // still exactly 8 steps
		for (std::size_t i = 0; i < take.size(); ++i)
		{
			const SampleFrame expected = inputFrame(first + i);
			if (std::fabs(take[i].left() - expected.left()) > Pcm24Tolerance
				|| std::fabs(take[i].right() - expected.right()) > Pcm24Tolerance)
			{
				QFAIL(qPrintable(QStringLiteral("reloaded take frame %1 is (%2, %3), the input fed was (%4, %5)").arg(i)
					.arg(take[i].left()).arg(take[i].right()).arg(expected.left()).arg(expected.right())));
			}
		}
	}

private:
	//! One period: the next input frames are staged, then rendered; notes the period the column took over in.
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		for (f_cnt_t f = 0; f < fpp; ++f) { block[static_cast<std::size_t>(f)] = inputFrame(m_fed + static_cast<std::uint64_t>(f)); }
		engine->pushInputFrames(block.data(), fpp);
		const std::uint64_t first = m_fed;
		m_fed += static_cast<std::uint64_t>(fpp);
		engine->renderNextPeriod();
		const SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		const bool playing = scheduler.trackIsSessionActive(m_column);
		if (playing && !m_wasPlaying)
		{
			m_startPeriodFirstFrame = first;
			m_startPeriodTick = scheduler.positionTicks();
			m_previousTick = m_lastTick;
		}
		m_wasPlaying = playing;
		m_lastTick = scheduler.positionTicks();
	}

	QString state() const
	{
		return run(QStringLiteral("session.record_slot_status")).result.value(QStringLiteral("state")).toString();
	}

	bool recordUntilDone()
	{
		for (int i = 0; i < 6000 && state() != QStringLiteral("done"); ++i) { period(); }
		return state() == QStringLiteral("done");
	}

	//! The take started on a bar line, in the period that first reached it.
	void checkQuantisedStart(const QJsonObject& status)
	{
		const tick_t start = static_cast<tick_t>(status.value(QStringLiteral("start_tick")).toDouble());
		QCOMPARE(start % Engine::getSong()->ticksPerBar(), tick_t{0});
		QVERIFY2(start > 0, "the take started at the clock's origin rather than on the next bar line");
		QVERIFY(m_startPeriodTick >= start);
		QVERIFY(m_previousTick < start);
	}

	//! Lands the take into @a name and checks the cell, the file's PCM and the one undo step.
	void landAndCompare(const QJsonObject& status, std::uint64_t frames, const QString& name)
	{
		const int track = status.value(QStringLiteral("track")).toInt();
		const int scene = status.value(QStringLiteral("scene")).toInt();
		const auto latency = static_cast<std::uint64_t>(status.value(QStringLiteral("latency_frames")).toInt());
		const QString path = m_dir.filePath(name);
		const ControlResult landed = run(QStringLiteral("session.record_slot_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(static_cast<std::uint64_t>(landed.result.value(QStringLiteral("frames")).toDouble()), frames);
		const ClipSlot& slot = Engine::getSong()->sessionModel().slot(track, scene);
		QVERIFY(slot.type() == ClipSlot::Type::Audio);
		QCOMPARE(slot.audioSource(), path);

		int rate = 0;
		const std::vector<SampleFrame> take = readWav(path, &rate);
		QCOMPARE(static_cast<std::uint64_t>(take.size()), frames);
		QCOMPARE(rate, static_cast<int>(Engine::audioEngine()->inputSampleRate()));
		const std::uint64_t first = m_startPeriodFirstFrame + latency;  // one round trip after the start period
		for (std::size_t i = 0; i < take.size(); ++i)
		{
			const SampleFrame expected = inputFrame(first + i);
			if (std::fabs(take[i].left() - expected.left()) > Pcm24Tolerance
				|| std::fabs(take[i].right() - expected.right()) > Pcm24Tolerance)
			{
				QFAIL(qPrintable(QStringLiteral("take frame %1 is (%2, %3), the input fed was (%4, %5)").arg(i)
					.arg(take[i].left()).arg(take[i].right()).arg(expected.left()).arg(expected.right())));
			}
		}

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(Engine::getSong()->sessionModel().slot(track, scene).isEmpty());  // one step puts the cell back
		QCOMPARE(Engine::getSong()->sessionModel().trackCount(), m_column + 1);    // and only the cell
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(Engine::getSong()->sessionModel().slot(track, scene).audioSource(), path);
		QVERIFY(QFileInfo::exists(path));
	}

	QTemporaryDir m_dir;
	int m_column = 0;
	std::uint64_t m_fed = 0;
	bool m_wasPlaying = false;
	std::uint64_t m_startPeriodFirstFrame = 0;
	tick_t m_startPeriodTick = 0;
	tick_t m_previousTick = 0;
	tick_t m_lastTick = 0;
};

QTEST_GUILESS_MAIN(SessionAudioRecordTest)
#include "SessionAudioRecordTest.moc"
