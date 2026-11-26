/*
 * SamplerStutterContinuationTest.cpp - the sampler's stutter continuation is one value, read once per note start.
 *
 * WHAT THIS DRIVES
 *
 * AudioFileProcessor keeps ONE stutter continuation per instrument: the frame the next note starts from and the
 * direction it plays in. The notes of one instrument render on the engine's worker threads at once, so another note
 * job can publish its own position at any moment. The note start in playNoteImpl() tests that frame against the
 * sample end and then starts from it, and the test in the base had those two steps as two separate reads: a note
 * start could test one publication's frame and then play another publication's frame AND direction, so a note could
 * begin at one note job's playhead while playing the other's direction (cluster new-p15-260-port).
 *
 * The test compiles the plug-in's own AudioFileProcessor.cpp (the ZynAddSubFxInstrumentTest / TripleOscillatorUserWaveTest
 * shape) and drives the production note-start path - NotePlayHandle::play -> Instrument::playNote -> playNoteImpl - with
 * an instrument installed on a track by InstrumentTrack::setInstrumentForTest(). The instrument publishes its
 * continuation at the one point the fix makes indivisible: the LMMS_TESTING note-start hook runs between the read and
 * the voice state write, which is exactly where a concurrent note job's publication used to land.
 *
 * Cases:
 *   aNoteStartReadsTheContinuationOnce()                  (a) the continuation is (midFrame, forwards); the hook
 *                                                             publishes (end-1, backwards) between the read and the
 *                                                             write; the note must render the ramp rising FROM midFrame.
 *   pingPongPlaybackContinuesBackwardsAtThePublishedFrame() (b) a ping-pong note is rendered past the loop end, so the
 *                                                             published continuation plays backwards; the second note
 *                                                             must continue backwards at that frame even though the
 *                                                             hook publishes (0, forwards) in between.
 *   aNoteThatReachedTheSampleEndPublishesItsOwnVoiceState() (c) the pair a note publishes is its own voice state,
 *                                                             read back once.
 *
 * HOW THE CASES ARE DETERMINISTIC
 *
 * There is no thread, no sleep and no clock. Every value the code under test reads is injected (the continuation, the
 * ramp), every input is a rendered period of a fixed length, and the hook runs synchronously inside the note start.
 * The sample is a rising ramp, so the first frames of a note show the frame it started from and the direction it
 * plays: a forwards note rises, a backwards note falls.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>
#include <vector>

#include "AudioEngine.h"
#include "AudioFileProcessor.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Sample.h"
#include "SampleFrame.h"
#include "../core/SamplerTestSupport.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::sampler_test;

namespace
{

//! The sampler with the note-start seams the regression drives (LMMS_TESTING only; absent from a shipping build).
class TestSampler : public AudioFileProcessor
{
public:
	using AudioFileProcessor::AudioFileProcessor;

	using AudioFileProcessor::m_noteStartHook;
	using AudioFileProcessor::testPublishStutterContinuation;
	using AudioFileProcessor::testStutterContinuation;
	using AudioFileProcessor::testVoiceState;
};

} // namespace

class SamplerStutterContinuationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_fpp = Engine::audioEngine()->framesPerPeriod();
		m_rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		// One rising ramp of two seconds: frame i of the sample is 0.9 * i / frames.
		m_sampleFrames = m_rate * 2;
		m_wav = writeWav(m_dir.filePath(QStringLiteral("ramp.wav")), true);
		QVERIFY(!m_wav.isEmpty());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! A fresh project with one sampler track whose instrument is this test's own, compiled-in sampler.
	void init()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		m_sampler = new TestSampler(m_track);
		m_track->setInstrumentForTest(m_sampler);
		QVERIFY2(m_track->instrument() == m_sampler, "the track did not take the instrument");
		m_sampler->setAudioFile(m_wav, false);
		QVERIFY(m_sampler->sample().endFrame() > 0);
	}

	//! (a) The note starts from the one publication it read, frame and direction together - never from the one that
	//! a concurrent note job publishes between the read and the voice state write.
	void aNoteStartReadsTheContinuationOnce()
	{
		m_sampler->stutterModel().setValue(true);
		m_sampler->loopModel().setValue(static_cast<int>(Sample::Loop::Off));

		const f_cnt_t startFrame = m_sampler->sample().startFrame();
		const f_cnt_t endFrame = m_sampler->sample().endFrame();
		QVERIFY(endFrame > startFrame + 4 * m_fpp);
		const f_cnt_t midFrame = startFrame + (endFrame - startFrame) / 2;

		// What the note start finds: halfway through the sample, playing forwards.
		m_sampler->testPublishStutterContinuation(AudioFileProcessor::StutterStart{midFrame, false});

		// What another note job publishes between that read and the voice state write: the end of the sample,
		// playing backwards.
		int hookRuns = 0;
		m_sampler->m_noteStartHook = [this, endFrame, &hookRuns]
			{
				++hookRuns;
				m_sampler->testPublishStutterContinuation(
					AudioFileProcessor::StutterStart{endFrame - 1, true});
			};
		Played note = start();
		renderPeriod(note);
		m_sampler->m_noteStartHook = nullptr;

		QCOMPARE(hookRuns, 1);
		QVERIFY2(note.left.size() == static_cast<std::size_t>(m_fpp), "the note rendered one period");
		QVERIFY2(std::abs(note.left[0] - rampAt(midFrame)) < 0.05f,
			qPrintable(QStringLiteral("the note started at the ramp value %1 instead of %2")
				.arg(note.left[0]).arg(rampAt(midFrame))));
		// The note plays forwards: the last frame of the period is further up the ramp than the first. One
		// frame apart is too fine (the 16-bit ramp repeats a level for a couple of frames), so the whole
		// period is compared.
		QVERIFY2(note.left.back() > note.left.front(),
			"the note started backwards: its frame and its direction came from different publications");
		NotePlayHandleManager::release(note.nph);
	}

	//! (b) A ping-pong note that has turned around publishes a backwards continuation; the next note continues
	//! backwards at that frame, even though a concurrent note job publishes a forwards continuation in between.
	void pingPongPlaybackContinuesBackwardsAtThePublishedFrame()
	{
		m_sampler->stutterModel().setValue(true);
		m_sampler->loopModel().setValue(static_cast<int>(Sample::Loop::PingPong));
		const f_cnt_t endFrame = m_sampler->sample().endFrame();
		QVERIFY(endFrame > 0);

		// Render the first note past the loop end, so the ping-pong turnaround is in the continuation it publishes.
		Played first = start();
		const int periods = static_cast<int>((endFrame + 4 * m_fpp) / m_fpp) + 1;
		for (int i = 0; i < periods; ++i) { renderPeriod(first); }
		const auto paused = m_sampler->testStutterContinuation();
		QVERIFY2(paused.backwards, "the ping-pong note did not turn around, so the case cannot be checked");
		QVERIFY(paused.frame < static_cast<f_cnt_t>(endFrame));
		NotePlayHandleManager::release(first.nph);

		// A concurrent note job publishes the sample start, playing forwards, between the second note start's read
		// and its voice state write.
		int hookRuns = 0;
		const f_cnt_t startFrame = m_sampler->sample().startFrame();
		m_sampler->m_noteStartHook = [this, startFrame, &hookRuns]
			{
				++hookRuns;
				m_sampler->testPublishStutterContinuation(
					AudioFileProcessor::StutterStart{startFrame, false});
			};
		Played second = start();
		renderPeriod(second);
		m_sampler->m_noteStartHook = nullptr;

		QCOMPARE(hookRuns, 1);
		QVERIFY2(second.left.size() == static_cast<std::size_t>(m_fpp), "the note rendered one period");
		QVERIFY2(std::abs(second.left[0] - rampAt(paused.frame)) < 0.05f,
			qPrintable(QStringLiteral("the note started at the ramp value %1 instead of %2")
				.arg(second.left[0]).arg(rampAt(paused.frame))));
		QVERIFY2(second.left.back() < second.left.front(),
			"the note played forwards: it took the publication that arrived in between instead of the "
			"ping-pong continuation it read");
		NotePlayHandleManager::release(second.nph);
	}

	//! (c) The pair a note publishes is its own voice state, read back once - the frame and the direction of the
	//! publication a note leaves behind are the ones its voice holds.
	void aNoteThatReachedTheSampleEndPublishesItsOwnVoiceState()
	{
		m_sampler->stutterModel().setValue(true);
		m_sampler->loopModel().setValue(static_cast<int>(Sample::Loop::Off));
		// A short end point, so the note reaches the end of the sample inside its first periods.
		m_sampler->endPointModel().setValue(0.01f);
		const f_cnt_t endFrame = m_sampler->sample().endFrame();
		QVERIFY2(endFrame > 0 && endFrame < 4 * m_fpp, "the end point is inside the first periods");

		Played note = start();
		const int periods = static_cast<int>(endFrame / m_fpp) + 2;
		for (int i = 0; i < periods; ++i) { renderPeriod(note); }

		const auto published = m_sampler->testStutterContinuation();
		const auto voice = m_sampler->testVoiceState(note.nph);
		QCOMPARE(published.frame, voice.frame);
		QCOMPARE(published.backwards, voice.backwards);
		QCOMPARE(published.frame, endFrame);
		NotePlayHandleManager::release(note.nph);
	}

private:
	//! A held note of key 69, which plays the sample at its own pitch: one output frame per sample frame.
	Played start() const
	{
		const Note note(TimePos(384), TimePos(0), 69);
		Played p;
		p.nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * 4000, note);
		return p;
	}

	void renderPeriod(Played& p) const
	{
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		p.nph->play(std::span<SampleFrame>{buffer});
		for (const SampleFrame& frame : buffer) { p.left.push_back(frame[0]); }
	}

	//! The ramp's value at a sample frame (writeWav() writes 0.9 * i / frames).
	float rampAt(f_cnt_t frame) const
	{
		return 0.9f * static_cast<float>(frame) / static_cast<float>(m_sampleFrames);
	}

	QTemporaryDir m_dir;
	QString m_wav;
	InstrumentTrack* m_track = nullptr;
	TestSampler* m_sampler = nullptr;
	f_cnt_t m_fpp = 0;
	int m_rate = 0;
	int m_sampleFrames = 0;
};

QTEST_MAIN(SamplerStutterContinuationTest)
#include "SamplerStutterContinuationTest.moc"
