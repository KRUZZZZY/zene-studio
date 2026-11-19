/*
 * ConvolutionFftwPlanScopeTest.cpp - cluster 409 regression.
 *
 * FFTW's planner API - creating a plan, destroying a plan, allocating and freeing the buffers a
 * plan works on - is not thread-safe: it may not run while any thread executes a plan (only
 * fftwf_execute is thread-safe, and only for different plans). The convolution kernel's plans
 * were made in its constructor and destroyed in its destructor, on the control thread, while the
 * audio thread could be executing another instance's plans - the engine's model-change guard
 * (which the render thread holds for every period it renders) was not taken around them.
 *
 * The plans and their buffers now live inside a ConvolutionFftwScope, which takes that guard,
 * so no FFTW planner call can overlap an audio thread's fftwf_execute. The scope counts the
 * planner calls it covered: this test drives a real kernel (and a real effect, through the
 * plugin's own load path) and asserts that no planner call ran outside a scope.
 *
 * The counters are the test seam - there is no timing and no race in this test: the kernel is
 * built and destroyed on this thread, and the scope's counters say where the planner calls ran.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <cmath>
#include <exception>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConvolutionFftw.h"
#include "ConvolutionKernel.h"
#include "ConvolutionReverb.h"
#include "EffectChain.h"
#include "Engine.h"
#include "SampleBuffer.h"

using namespace lmms;

namespace
{

//! A 16-bit stereo PCM WAV of @a frames nonzero frames, declared at @a rate Hz; false if it cannot be written.
bool writeWav(const QString& path, int rate, int frames)
{
	const auto dataBytes = static_cast<quint32>(frames) * 4u;
	QByteArray out;
	const auto put16 = [&out](quint16 v) {
		out.append(static_cast<char>(v & 0xFFu));
		out.append(static_cast<char>(v >> 8));
	};
	const auto put32 = [&out](quint32 v) {
		for (int shift = 0; shift < 32; shift += 8) { out.append(static_cast<char>((v >> shift) & 0xFFu)); }
	};
	out.append("RIFF", 4);
	put32(36u + dataBytes);
	out.append("WAVEfmt ", 8);
	put32(16u);                                 // fmt chunk size
	put16(1u);                                  // PCM
	put16(2u);                                  // stereo
	put32(static_cast<quint32>(rate));
	put32(static_cast<quint32>(rate) * 4u);     // byte rate
	put16(4u);                                  // block align
	put16(16u);                                 // bits per sample
	out.append("data", 4);
	put32(dataBytes);
	for (int i = 0; i < frames; ++i)
	{
		const double t = 0.3 * (i + 1);
		put16(static_cast<quint16>(static_cast<qint16>(std::lround(12000.0 * std::sin(t)))));
		put16(static_cast<quint16>(static_cast<qint16>(std::lround(12000.0 * std::cos(t)))));
	}
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(out) == out.size();
}

//! Loads @a path into @a effect. An exception comes back as @a error, not out of the test runner.
bool tryLoad(ConvolutionReverbEffect& effect, const QString& path, QString& error)
{
	try
	{
		return effect.loadImpulse(path);
	}
	catch (const std::exception& e)
	{
		error = QStringLiteral("std::exception: %1").arg(QString::fromUtf8(e.what()));
	}
	catch (...)
	{
		error = QStringLiteral("a non-standard exception");
	}
	return false;
}

} // namespace

class ConvolutionFftwPlanScopeTest : public QObject
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

	//! A kernel's plans are made and destroyed inside the scope, never outside it.
	void thePlansLiveInsideTheScope()
	{
		const std::vector<float> impulse(ConvolutionKernel::Partition, 0.f);
		const auto guardedBefore = ConvolutionFftwScope::guardedPlannerCalls();
		const auto unguardedBefore = ConvolutionFftwScope::unguardedPlannerCalls();

		{
			std::unique_ptr<ConvolutionKernel> kernel =
				std::make_unique<ConvolutionKernel>(impulse, impulse);
			QVERIFY2(ConvolutionFftwScope::guardedPlannerCalls() > guardedBefore,
				"the kernel's FFTW plans were created outside the scope that quiesces the audio "
				"thread (cluster 409)");
		}

		QVERIFY2(ConvolutionFftwScope::unguardedPlannerCalls() == unguardedBefore,
			"an FFTW plan was created or destroyed while the audio thread could be executing a "
			"plan (cluster 409)");
		QVERIFY2(ConvolutionFftwScope::guardedPlannerCalls() > guardedBefore,
			"the kernel's FFTW plans were never covered by the scope");
	}

	//! The plugin's own load path (a new impulse response) keeps every planner call in a scope.
	void loadingAnImpulseKeepsThePlannerCallsInAScope()
	{
		EffectChain chain{nullptr};
		auto* effect = new ConvolutionReverbEffect(&chain, nullptr);
		const auto unguardedBefore = ConvolutionFftwScope::unguardedPlannerCalls();
		const auto guardedBefore = ConvolutionFftwScope::guardedPlannerCalls();

		QVERIFY(effect->loadImpulse(QString()));
		QVERIFY2(ConvolutionFftwScope::unguardedPlannerCalls() == unguardedBefore,
			"loading an impulse response ran an FFTW planner call outside the scope that "
			"quiesces the audio thread (cluster 409)");
		QVERIFY2(ConvolutionFftwScope::guardedPlannerCalls() > guardedBefore,
			"loading an impulse response ran no FFTW planner call inside a scope at all");

		delete effect;
		QVERIFY2(ConvolutionFftwScope::unguardedPlannerCalls() == unguardedBefore,
			"removing the effect destroyed an FFTW plan outside the scope that quiesces the audio "
			"thread (cluster 409)");
	}

	//! The kernel still convolves: a unit impulse leaves the block unchanged.
	void theKernelStillConvolves()
	{
		std::vector<float> impulse(ConvolutionKernel::Partition, 0.f);
		impulse[0] = 1.f;
		std::unique_ptr<ConvolutionKernel> kernel =
			std::make_unique<ConvolutionKernel>(impulse, impulse);

		float input[2][ConvolutionKernel::Partition];
		float output[2][ConvolutionKernel::Partition];
		for (int c = 0; c < 2; ++c)
		{
			for (int i = 0; i < ConvolutionKernel::Partition; ++i) { input[c][i] = 0.01f * (i + 1); }
		}
		const float* in[2] = {input[0], input[1]};
		float* out[2] = {output[0], output[1]};
		kernel->process(in, out);

		for (int c = 0; c < 2; ++c)
		{
			for (int i = 0; i < ConvolutionKernel::Partition; ++i)
			{
				QVERIFY2(std::abs(output[c][i] - input[c][i]) < 1e-5f,
					"the convolution with a unit impulse is not the identity any more");
			}
		}
	}

	//! A 1 Hz impulse resamples to nothing at the engine's rate: it is refused, and the state is left as it was.
	void unresamplableImpulseIsRefused()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString wav1Hz = dir.filePath(QStringLiteral("impulse-1hz.wav"));
		QVERIFY(writeWav(wav1Hz, 1, 300));

		const std::shared_ptr<const SampleBuffer> decoded = SampleBuffer::fromFile(wav1Hz);
		QVERIFY2(decoded != nullptr && decoded->size() > 0 && decoded->sampleRate() == 1,
			"precondition failed: the 1 Hz WAV did not decode as a non-empty 1 Hz buffer, so the test would pass vacuously");

		EffectChain chain{nullptr};
		auto effect = std::make_unique<ConvolutionReverbEffect>(&chain, nullptr);
		const QString pathBefore = effect->impulsePath();
		const int tailBefore = effect->tailFrames();

		QString error;
		const bool loaded = tryLoad(*effect, wav1Hz, error);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("loadImpulse threw on a 1 Hz impulse: %1").arg(error)));
		QVERIFY2(!loaded, "loadImpulse accepted a 1 Hz impulse that resamples to nothing at the engine's rate");
		QCOMPARE(effect->impulsePath(), pathBefore);
		QCOMPARE(effect->tailFrames(), tailBefore);
	}

	//! Covers the normal short-resample branch: two frames at twice the engine's rate resample to one frame (expected 1), which loads.
	//! It does not pin the one-frame fallback; that is emptyResampleOfAShortImpulseKeepsTheFallback.
	void veryShortImpulseStillLoads()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const int engineRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const QString shortWav = dir.filePath(QStringLiteral("impulse-short.wav"));
		QVERIFY(writeWav(shortWav, 2 * engineRate, 2));

		EffectChain chain{nullptr};
		auto effect = std::make_unique<ConvolutionReverbEffect>(&chain, nullptr);
		QString error;
		const bool loaded = tryLoad(*effect, shortWav, error);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("loadImpulse threw on a very short impulse: %1").arg(error)));
		QVERIFY2(loaded, "a very short impulse that resamples to at most one frame was refused; the one-frame fallback should load it");
		QCOMPARE(effect->impulsePath(), shortWav);
	}

	//! A one-frame response at four times the engine's rate resamples to no frames (expected 0), so the one-frame fallback keeps it and it loads.
	void emptyResampleOfAShortImpulseKeepsTheFallback()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const int engineRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const QString oneFrame = dir.filePath(QStringLiteral("impulse-one-frame.wav"));
		QVERIFY(writeWav(oneFrame, 4 * engineRate, 1));

		const std::shared_ptr<const SampleBuffer> decoded = SampleBuffer::fromFile(oneFrame);
		QVERIFY2(decoded != nullptr && decoded->size() == 1 && decoded->sampleRate() == static_cast<sample_rate_t>(4 * engineRate),
			"precondition failed: the one-frame WAV did not decode as a 1-frame buffer at four times the engine's rate");

		EffectChain chain{nullptr};
		auto effect = std::make_unique<ConvolutionReverbEffect>(&chain, nullptr);
		QString error;
		const bool loaded = tryLoad(*effect, oneFrame, error);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("loadImpulse threw on a one-frame impulse: %1").arg(error)));
		QVERIFY2(loaded, "a one-frame impulse that resamples to no frames was refused; the one-frame fallback should keep it");
		QCOMPARE(effect->impulsePath(), oneFrame);
	}

	//! Positive control: a normal response loads at the engine's rate, and at another valid rate.
	void validImpulseLoads()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const int engineRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		const int otherRate = engineRate == 48000 ? 44100 : 48000;
		const QString atEngine = dir.filePath(QStringLiteral("impulse-engine.wav"));
		const QString atOther = dir.filePath(QStringLiteral("impulse-other.wav"));
		QVERIFY(writeWav(atEngine, engineRate, 300));
		QVERIFY(writeWav(atOther, otherRate, 300));

		EffectChain chain{nullptr};
		auto effect = std::make_unique<ConvolutionReverbEffect>(&chain, nullptr);

		QString error;
		bool loaded = tryLoad(*effect, atEngine, error);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("loadImpulse threw at the engine's rate: %1").arg(error)));
		QVERIFY2(loaded, "a normal impulse at the engine's rate did not load");
		QCOMPARE(effect->impulsePath(), atEngine);

		error.clear();
		loaded = tryLoad(*effect, atOther, error);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("loadImpulse threw at another rate: %1").arg(error)));
		QVERIFY2(loaded, "a normal impulse at another valid rate did not load");
		QCOMPARE(effect->impulsePath(), atOther);
	}

	//! A path that does not decode is refused, and the state is left as it was.
	void undecodableImpulseIsRefused()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString missing = dir.filePath(QStringLiteral("no-such-impulse.wav"));
		QVERIFY(!QFile::exists(missing));

		EffectChain chain{nullptr};
		auto effect = std::make_unique<ConvolutionReverbEffect>(&chain, nullptr);
		const QString pathBefore = effect->impulsePath();
		const int tailBefore = effect->tailFrames();

		QString error;
		const bool loaded = tryLoad(*effect, missing, error);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("loadImpulse threw on a missing path: %1").arg(error)));
		QVERIFY2(!loaded, "loadImpulse accepted a path that does not decode");
		QCOMPARE(effect->impulsePath(), pathBefore);
		QCOMPARE(effect->tailFrames(), tailBefore);
	}
};

QTEST_GUILESS_MAIN(ConvolutionFftwPlanScopeTest)

#include "ConvolutionFftwPlanScopeTest.moc"
