/*
 * TripleOscillatorUserWaveTest.cpp - cluster new-p12-275 regression.
 *
 * The TripleOscillator GUI replaced `OscillatorObject::m_sampleBuffer` and
 * `m_userAntiAliasWaveTable` directly on the control thread while the audio
 * thread's `playNoteImpl()` read those same `std::shared_ptr` objects to
 * configure new oscillators. A shared_ptr protects its control block across
 * distinct instances, not a single instance read and written concurrently, so
 * loading a waveform during playback was a data race that could hand a note a
 * half-updated buffer/table pair.
 *
 * The publication now goes through `OscillatorObject::setUserWave()`, which
 * builds the anti-alias table first and then swaps both pointers under
 * AudioEngine's change lock - the lock `renderNextPeriod()` holds for a whole
 * render period and `playNoteImpl()` reads inside.
 *
 * This test drives the real publication on a second thread while the render
 * period is held open on this one; the fix is that the call cannot complete
 * inside the period. It is deterministic on a plain build and needs no
 * sanitizer.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Oscillator.h"
#include "SampleBuffer.h"
#include "TripleOscillator.h"

using namespace lmms;

namespace
{

//! Holds a render period open on the calling thread while `setUserWave()` runs
//! on another, and reports whether the publication got through anyway. The
//! render period owns AudioEngine's change mutex for its whole length, exactly
//! as AudioEngine::renderNextPeriod() does.
bool publicationEscapesTheRenderPeriod(OscillatorObject& osc,
	std::shared_ptr<const SampleBuffer> buffer)
{
	Engine::audioEngine()->requestChangeInModel();

	std::atomic<bool> started{false};
	std::atomic<bool> finished{false};
	std::thread worker([&osc, &buffer, &started, &finished]
	{
		started.store(true, std::memory_order_release);
		osc.setUserWave(buffer);
		finished.store(true, std::memory_order_release);
	});
	while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
	bool escaped = false;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (finished.load(std::memory_order_acquire)) { escaped = true; break; }
		std::this_thread::yield();
	}

	Engine::audioEngine()->doneChangeInModel();
	worker.join();
	return escaped;
}

} // namespace

class TripleOscillatorUserWaveTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void publishingAUserWaveWaitsForTheRenderPeriodToEnd()
	{
		OscillatorObject osc(nullptr, 0);

		const auto rate = Engine::audioEngine()->outputSampleRate();
		std::vector<SampleFrame> data(2048, SampleFrame{0.25f, -0.25f});
		std::shared_ptr<const SampleBuffer> buffer{
			new SampleBuffer(std::move(data), rate)};

		// Warm the FFT plans and the first table build so the measured window is
		// the lock, not first-use cost.
		osc.setUserWave(buffer);

		const bool escaped = publicationEscapesTheRenderPeriod(osc, buffer);
		QVERIFY2(!escaped,
			"OscillatorObject published a user waveform while a render period was in "
			"flight; playNoteImpl() can read a half-updated buffer/table pair "
			"(cluster new-p12-275)");
	}
};

QTEST_GUILESS_MAIN(TripleOscillatorUserWaveTest)

#include "TripleOscillatorUserWaveTest.moc"
