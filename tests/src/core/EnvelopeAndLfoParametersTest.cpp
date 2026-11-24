/*
 * EnvelopeAndLfoParametersTest.cpp - loading settings clears a prior user LFO waveform
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

#include <QDataStream>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "SampleBuffer.h"

using namespace lmms;

namespace
{

bool writeWaveFile(const QString& path, bool rising = true)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	constexpr quint32 sampleCount = 8;
	constexpr quint32 dataBytes = sampleCount * sizeof(qint16);
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVEfmt ", 8);
	out << quint32(16) << quint16(1) << quint16(1) << quint32(44100)
		<< quint32(88200) << quint16(2) << quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;
	for (quint32 i = 0; i < sampleCount; ++i)
	{
		const qint16 value = static_cast<qint16>(rising ? i * 1000 : (sampleCount - i) * 1000);
		out << value;
	}
	return out.status() == QDataStream::Ok;
}

void loadSettings(EnvelopeAndLfoParameters& parameters, bool hasWaveAttribute, const QString& path = {},
			  int lfoWaveShape = -1, float lfoAmount = -1.0f)
{
	QDomDocument document;
	QDomElement settings = document.createElement(QStringLiteral("elvol"));
	if (hasWaveAttribute) { settings.setAttribute(QStringLiteral("userwavefile"), path); }
	if (lfoWaveShape >= 0) { settings.setAttribute(QStringLiteral("lshp"), QString::number(lfoWaveShape)); }
	if (lfoAmount >= 0.0f) { settings.setAttribute(QStringLiteral("lamt"), QString::number(lfoAmount)); }
	parameters.loadSettings(settings);
}

} // namespace

class EnvelopeAndLfoParametersTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(m_tempDir.isValid());
		m_wavePath = m_tempDir.filePath(QStringLiteral("custom-lfo.wav"));
		QVERIFY(writeWaveFile(m_wavePath));
		m_otherWavePath = m_tempDir.filePath(QStringLiteral("custom-lfo-2.wav"));
		QVERIFY(writeWaveFile(m_otherWavePath, false));
	}

	void cleanupTestCase() { Engine::destroy(); }

	void missingWaveAttributeClearsPreviousWave()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		loadSettings(parameters, true, m_wavePath);
		QVERIFY(parameters.getLfoUserWave()->size() > 0);

		loadSettings(parameters, false);
		QVERIFY(parameters.getLfoUserWave() == SampleBuffer::emptyBuffer());
	}

	void emptyWaveAttributeClearsPreviousWave()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		loadSettings(parameters, true, m_wavePath);
		QVERIFY(parameters.getLfoUserWave()->size() > 0);

		loadSettings(parameters, true);
		QVERIFY(parameters.getLfoUserWave() == SampleBuffer::emptyBuffer());
	}

	/*! The audio path must not wait for the publisher. The publisher is held
	 *  at the publication point (the test seam setPublicationHook()) while the
	 *  audio path claims the published snapshot: the claim must go through
	 *  without the publisher's help. */
	void theAudioPathDoesNotWaitForAPublisher()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		std::atomic<bool> publisherHeld{false};
		std::atomic<bool> readerEntered{false};
		std::atomic<bool> readerDone{false};
		std::atomic<bool> claimedWhileHeld{false};

		EnvelopeAndLfoParameters::setPublicationHook([&]() {
			publisherHeld.store(true, std::memory_order_release);
			// wait for the audio path to reach the claim
			while (!readerEntered.load(std::memory_order_acquire)) { std::this_thread::yield(); }
			// the claim must happen while the publisher is held
			for (int i = 0; i < 1000000; ++i)
			{
				if (parameters.audioStateReaders() > 0)
				{
					claimedWhileHeld.store(true, std::memory_order_release);
					break;
				}
				if ((i % 64) == 0) { std::this_thread::yield(); }
			}
		});

		// the publisher: a publication that is held at the publication point
		std::thread publisher([&]() { parameters.updateSampleVars(); });
		// the audio path: it claims the published snapshot and holds it
		std::thread reader([&]() {
			while (!publisherHeld.load(std::memory_order_acquire)) { std::this_thread::yield(); }
			readerEntered.store(true, std::memory_order_release);
			parameters.holdAudioState();
			readerDone.store(true, std::memory_order_release);
		});

		publisher.join();
		reader.join();
		EnvelopeAndLfoParameters::setPublicationHook(nullptr);
		parameters.releaseHeldAudioState();

		QVERIFY(readerDone.load(std::memory_order_acquire));
		QVERIFY2(claimedWhileHeld.load(std::memory_order_acquire),
			"the audio path did not claim the published snapshot while the "
			"publisher was held: it waited for the publisher");
	}

	/*! The LFO shape cache must not be answered with data that was built from
	 *  an older snapshot: a publication that happens while a reader holds the
	 *  previous snapshot must not be lost by that reader's rebuild. */
	void theLfoShapeCacheIsNotStaleAfterAPublication()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		const int userWave =
			static_cast<int>(EnvelopeAndLfoParameters::LfoShape::UserDefinedWave);
		const auto frames = Engine::audioEngine()->framesPerPeriod();

		// the amount is loaded, not set afterwards: setting it would publish
		// again and hide the publication this test is about
		loadSettings(parameters, true, m_wavePath, userWave, 1.0f);

		// the audio path renders the first wave
		std::vector<float> first(frames);
		parameters.fillLevel(first.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		// The writer publishes another wave. While the publication starts, the
		// audio path fills a level: it holds the previous snapshot, and the
		// publication must not be lost by what that reader does with the shape
		// cache.
		std::vector<float> during(frames);
		EnvelopeAndLfoParameters::setPublicationStartHook([&]() {
			std::thread reader([&]() {
				parameters.fillLevel(during.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);
			});
			reader.join();
		});
		loadSettings(parameters, true, m_otherWavePath, userWave, 1.0f);
		EnvelopeAndLfoParameters::setPublicationStartHook(nullptr);

		// the audio path must render the new wave now, not a shape cache that
		// was built from the previous snapshot
		std::vector<float> after(frames);
		parameters.fillLevel(after.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		QVERIFY(after != first);
	}

	/*! The wave the audio path samples must not be freed by a wave that
	 *  replaces it: the audio thread may be sampling it right now. The test
	 *  stands in for the audio thread by holding the published snapshot (the
	 *  test seam holdAudioState()), which is what the audio path holds while
	 *  it fills a level. */
	void replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		loadSettings(parameters, true, m_wavePath);
		const std::weak_ptr<const SampleBuffer> weakWave = parameters.getLfoUserWave();
		QVERIFY(!weakWave.expired());

		// the audio path holds the published snapshot
		QVERIFY(parameters.holdAudioState());

		// a wave that replaces it must not free the wave that is in use
		loadSettings(parameters, true, m_otherWavePath);

		QVERIFY(!weakWave.expired());
		QVERIFY(parameters.getLfoUserWave() != weakWave.lock());
		parameters.releaseHeldAudioState();
	}

	/*! The audio path samples the wave that was published with the
	 *  parameters, so replacing the wave changes what it renders. */
	void theAudioPathSamplesThePublishedUserWave()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		const int userWave =
			static_cast<int>(EnvelopeAndLfoParameters::LfoShape::UserDefinedWave);
		const auto frames = Engine::audioEngine()->framesPerPeriod();

		loadSettings(parameters, true, m_wavePath, userWave);
		parameters.getLfoAmountModel().setValue(1.0f);
		std::vector<float> first(frames);
		parameters.fillLevel(first.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		loadSettings(parameters, true, m_otherWavePath, userWave);
		parameters.getLfoAmountModel().setValue(1.0f);
		std::vector<float> second(frames);
		parameters.fillLevel(second.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		QVERIFY(first != second);
	}

	/*! Cluster new-p10-118: the audio thread advances the LFOs once per period
	 *  (AudioEngine STAGE 3 -> LfoInstances::trigger()). The advance must not
	 *  lock and must not walk the registry of LFO instances: the phase is a
	 *  property of the LFO and of the frames that were rendered, so it advances
	 *  the same way whether or not the registry holds the instance. The
	 *  registry's own list is the control thread's to mutate; the audio thread
	 *  reads a period counter that no list edit can block.
	 *
	 *  The instance is taken out of the registry before the trigger, so this
	 *  test states the contract rather than the walk: before the fix the phase
	 *  moved only for the instances the walk met, and this LFO stayed frozen. */
	void theLfoPhaseAdvancesWithoutARegistryWalk()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		parameters.getLfoAmountModel().setValue(1.0f);
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);

		const auto render = [&]() {
			std::vector<float> level(frames, 0.0f);
			parameters.fillLevel(level.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);
			return level;
		};

		EnvelopeAndLfoParameters::instances()->remove(&parameters);
		const std::vector<float> before = render();
		EnvelopeAndLfoParameters::instances()->trigger();
		const std::vector<float> after = render();

		float step = 0.0f;
		for (std::size_t i = 0; i < before.size(); ++i)
		{
			step = std::max(step, std::fabs(after[i] - before[i]));
		}
		QVERIFY2(step > 1.0e-6f,
			"the LFO phase did not advance by one period: the advance is a walk "
			"over the LFO registry, not a period counter the audio thread reads");
	}

	/*! The reset of the LFOs runs on the audio thread as well (Song's per-period
	 *  scheduling at the start of the song), so it must not lock or walk either.
	 *  It puts every LFO back to the same phase: two LFOs that were at different
	 *  phases render the same level after it. */
	void theLfoResetPutsEveryLfoBackToTheSamePhase()
	{
		EnvelopeAndLfoParameters first(0.0f, nullptr);
		first.getLfoAmountModel().setValue(1.0f);
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		// the phase step of a few periods is a fraction of one oscillation, so
		// "the two levels differ" is a phase difference and not a wrap
		QVERIFY(first.getLfoOscillationFrames() > 16 * frames);

		const auto render = [&](EnvelopeAndLfoParameters& parameters) {
			std::vector<float> level(frames, 0.0f);
			parameters.fillLevel(level.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);
			return level;
		};

		// one LFO runs for a few periods; the other starts later, at its own
		// phase origin
		for (int i = 0; i < 4; ++i) { EnvelopeAndLfoParameters::instances()->trigger(); }
		EnvelopeAndLfoParameters second(0.0f, nullptr);
		second.getLfoAmountModel().setValue(1.0f);
		QVERIFY(render(first) != render(second));

		EnvelopeAndLfoParameters::instances()->reset();
		const std::vector<float> firstAfter = render(first);
		const std::vector<float> secondAfter = render(second);
		QVERIFY(firstAfter == secondAfter);
	}

	/*! A late audio-path call may find no published snapshot: the destructor
	 *  takes the snapshot away before the object is gone. fillLevel() must not
	 *  dereference the missing snapshot; it fills the period with a defined
	 *  level, gives the claim back, and works again once a snapshot is
	 *  published. */
	void fillLevelWithoutAPublishedSnapshotFillsSilence()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<float> level(frames, 123.0f);

		parameters.detachAudioState();
		parameters.fillLevel(level.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		for (const float value : level) { QCOMPARE(value, 0.0f); }
		QCOMPARE(parameters.audioStateReaders(), 0);

		// the empty period: nothing to fill, and still no leaked claim
		parameters.fillLevel(level.data(), 0, std::numeric_limits<f_cnt_t>::max(), 0);
		QCOMPARE(parameters.audioStateReaders(), 0);

		// a publication brings the snapshot back and the audio path follows it
		parameters.updateSampleVars();
		QVERIFY(parameters.holdAudioState());
		parameters.releaseHeldAudioState();
		QCOMPARE(parameters.audioStateReaders(), 0);
	}

private:
	QTemporaryDir m_tempDir;
	QString m_wavePath;
	QString m_otherWavePath;
};

QTEST_GUILESS_MAIN(EnvelopeAndLfoParametersTest)
#include "EnvelopeAndLfoParametersTest.moc"
