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

/*! THE PROOF for WF-055's audio half, driven through the agent surface on the Dummy device. The test drives
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
