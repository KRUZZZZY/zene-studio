/*
 * ExprSynthTest.cpp - long-note expression time and wave-table copy regressions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <QTest>

#include <atomic>
#include <chrono>
#include <thread>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Graph.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

#define private public
#include "ExprSynth.h"
#undef private

// Keep the implementation in this translation unit so only this test's view
// of ExprSynth relaxes access to its frame counter.
#include "../../../plugins/Xpressive/ExprSynth.cpp"

namespace lmms
{

class ExprSynthTimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	void expressionTimeContinuesPast32BitFrameCount()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Note note(TimePos(1), TimePos(0));
		NotePlayHandle noteHandle(track, 0, 64, note);

		WaveSample wave(1);
		wave.setInterpolate(false);
		FloatModel pan1(0.f, -1.f, 1.f, 0.01f);
		FloatModel pan2(0.f, -1.f, 1.f, 0.01f);
		constexpr sample_rate_t sampleRate = 48000;
		ExprSynth synth(&wave, &wave, &wave,
			new ExprFront("t + integrate(1)", 8),
			new ExprFront("t + integrate(1)", 8),
			&noteHandle, sampleRate, &pan1, &pan2, 10.f);

		synth.m_note_sample = (f_cnt_t{1} << 32) - 1;
		SampleFrame output[1];
		synth.renderOutput(1, output);
		synth.renderOutput(1, output);
		QVERIFY2(output[0].left() > 80000.f,
			"the expression's t variable wrapped after the 32-bit frame boundary");
		QVERIFY(synth.m_note_sample == (f_cnt_t{1} << 32) + 1);
	}

	void waveTableCopyWaitsForRenderPeriod()
	{
		// Playing notes read their wave tables every frame, and a render period holds the
		// audio change lock for its whole length. A table overwrite from the GUI or a
		// project load must wait for that lock instead of writing mid-period.
		WaveSample wave(4);
		graphModel graph(-1.f, 1.f, 4, nullptr);
		const float newSamples[4] = {0.5f, 0.5f, 0.5f, 0.5f};
		graph.setSamples(newSamples);

		std::atomic<bool> copied{false};
		bool copiedWhileHeld = false;
		std::thread editor;
		{
			const auto renderPeriod = Engine::audioEngine()->requestChangesGuard();
			editor = std::thread{[&wave, &graph, &copied] {
				wave.copyFrom(&graph);
				copied = true;
			}};
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
			while (!copied && std::chrono::steady_clock::now() < deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds{1});
			}
			copiedWhileHeld = copied;
		}
		editor.join();

		QVERIFY2(!copiedWhileHeld,
			"the wave table was overwritten while a render period held the audio change lock");
		QVERIFY(copied);
		QCOMPARE(wave.m_samples[0], 0.5f);
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::ExprSynthTimeTest)
#include "ExprSynthTest.moc"
