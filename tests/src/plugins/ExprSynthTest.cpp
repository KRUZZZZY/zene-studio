/*
 * ExprSynthTest.cpp - long-note expression time and wave-table copy regressions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <QTest>
#include <QLoggingCategory>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
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

namespace
{

QStringList g_capturedMessages;

void captureMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	g_capturedMessages.append(message);
}

//! An expression function that throws when the expression is evaluated.
struct ThrowingExpressionFunction : public exprtk::ifunction<float>
{
	using exprtk::ifunction<float>::operator();

	ThrowingExpressionFunction() : exprtk::ifunction<float>(1) {}

	float operator()(const float& value) override
	{
		throw std::runtime_error("boom");
	}
};

//! Reads the render error a synth recorded for a later report. The accessor only
//! exists after the fix, so this helper keeps the test compilable against the
//! unfixed product code as well.
template <typename Synth>
bool readPendingError(Synth& synth, std::string& message)
{
	if constexpr (requires { synth.takePendingError(); synth.pendingError(); })
	{
		if (!synth.takePendingError())
		{
			return false;
		}
		message.assign(synth.pendingError());
		return true;
	}
	else
	{
		return false;
	}
}

//! Takes the render error a second time. The error must be reported once, even
//! though the broken expression throws in every period.
template <typename Synth>
bool takePendingErrorAgain(Synth& synth)
{
	if constexpr (requires { synth.takePendingError(); })
	{
		return synth.takePendingError();
	}
	else
	{
		return false;
	}
}

}

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

	void releaseTimeCountsOnFromTheReleasePosition()
	{
		// A note that is released before its first render period has its release
		// position at the very first sample. The position must not be confused with
		// the "not set yet" marker, or the release time restarts at the next period.
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Note note(TimePos(1), TimePos(0));
		NotePlayHandle noteHandle(track, 0, 64, note);
		noteHandle.noteOff(0);

		WaveSample wave(1);
		wave.setInterpolate(false);
		FloatModel pan1(0.f, -1.f, 1.f, 0.01f);
		FloatModel pan2(0.f, -1.f, 1.f, 0.01f);
		constexpr sample_rate_t sampleRate = 48000;
		// "trel" is the time since the release, so the rendered value is the
		// release time of the last rendered frame.
		ExprSynth synth(&wave, &wave, &wave,
			new ExprFront("trel", 8),
			new ExprFront("trel", 8),
			&noteHandle, sampleRate, &pan1, &pan2, 10.f);

		SampleFrame output[64];
		synth.renderOutput(64, output);
		const float trelAfterFirstPeriod = output[63].left();
		synth.renderOutput(64, output);
		const float trelAfterSecondPeriod = output[63].left();

		QVERIFY2(trelAfterFirstPeriod > 0.f,
			"the release time did not start counting after the release");
		QVERIFY2(trelAfterSecondPeriod > trelAfterFirstPeriod,
			"the release time restarted instead of counting on from the release position");
		QVERIFY2(synth.m_note_rel_sample == 0,
			"the release position was moved to the start of the second render period");
	}

	void expressionRenderErrorIsNotLoggedOnTheAudioThread()
	{
		// The render path runs on the audio thread. Logging there allocates and can
		// lock, so a failing expression must not log; the error is recorded and
		// reported later from the main thread.
		ThrowingExpressionFunction boom;
		auto* front = new ExprFront("boom(0)", 8);
		QVERIFY(front->getData() != nullptr);
		QVERIFY(front->getData()->m_symbol_table.add_function("boom", boom));
		auto* front2 = new ExprFront("boom(0)", 8);
		QVERIFY(front2->getData()->m_symbol_table.add_function("boom", boom));

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
		ExprSynth synth(&wave, &wave, &wave, front, front2,
			&noteHandle, sampleRate, &pan1, &pan2, 10.f);
		QVERIFY(synth.m_exprO1->isValid());
		QVERIFY(synth.m_exprO2->isValid());

		g_capturedMessages.clear();
		qInstallMessageHandler(captureMessage);
		SampleFrame output[64];
		synth.renderOutput(64, output);
		qInstallMessageHandler(nullptr);

		std::string message;
		const bool hasPendingError = readPendingError(synth, message);
		const QStringList captured = g_capturedMessages;
		g_capturedMessages.clear();

		QVERIFY2(captured.isEmpty(),
			qPrintable("the render path logged on the audio thread: " + captured.join(", ")));
		QVERIFY2(hasPendingError, "the render error was not recorded for a later report");
		QVERIFY2(message.find("boom") != std::string::npos,
			"the recorded render error did not carry the exception message");
		QVERIFY2(!takePendingErrorAgain(synth),
			"the render error was offered for reporting a second time");
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

	void integrateWithoutFrameCounterDoesNotDivideByZero()
	{
		// With no frame counter no integrate call is ever counted, so the callback has no
		// cycle to advance. It must return zero rather than take its cycle position modulo
		// that empty count, which traps as an integer division by zero.
		ExprFront expr("integrate(1)", 8);
		expr.setIntegrate(nullptr, 48000);
		QVERIFY(expr.compile());
		QCOMPARE(expr.evaluate(), 0.f);
		QCOMPARE(expr.evaluate(), 0.f);
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::ExprSynthTimeTest)
#include "ExprSynthTest.moc"
