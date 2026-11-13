#include <QtTest>

#include <cmath>
#include <memory>

#include "AudioBus.h"
#include "BufferManager.h"
#include "Effect.h"
#include "Engine.h"
#include "PeakControllerEffect.h"

using namespace lmms;

// Cluster new-p10-318: the RMS measurement divided the summed energy by the
// block's frame count. A zero-frame block made that 0.0 / 0, so the controller
// output was computed from NaN instead of holding the last measured value.
// A block with no frames measures nothing, so the output must not move.
class PeakControllerEffectZeroFrameTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		BufferManager::init(kFrames);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	// Positive control: a non-empty block moves the output to base + RMS.
	void nonEmptyBlockMeasuresSignal()
	{
		auto effect = std::make_unique<PeakControllerEffect>(nullptr, nullptr);
		processBlock(*effect, kFrames);

		// Base 0.5 (default) + RMS of constant 0.25 on both channels = sqrt(0.125).
		QCOMPARE(effect->lastSample(), 0.5f + std::sqrt(0.125f));
	}

	// The regression: a zero-frame block must leave the output where the last
	// measured block put it. Before the fix this produced NaN in the RMS and
	// the qBound clamp replaced it with a bound, so the output jumped.
	void zeroFrameBlockHoldsLastOutput()
	{
		auto effect = std::make_unique<PeakControllerEffect>(nullptr, nullptr);
		processBlock(*effect, kFrames);
		const float measured = effect->lastSample();
		QVERIFY(std::isfinite(measured));

		processBlock(*effect, 0);

		QVERIFY2(std::isfinite(effect->lastSample()),
			qPrintable(QStringLiteral("lastSample %1").arg(effect->lastSample())));
		QCOMPARE(effect->lastSample(), measured);
	}

	// The next measured block after an empty one must match what the same block
	// gives without the empty block in between: no state is left behind.
	void nonEmptyBlockAfterZeroFrameBlockMatchesFirst()
	{
		auto effect = std::make_unique<PeakControllerEffect>(nullptr, nullptr);
		processBlock(*effect, kFrames);
		const float first = effect->lastSample();

		processBlock(*effect, 0);
		processBlock(*effect, kFrames);

		QCOMPARE(effect->lastSample(), first);
	}

private:
	static constexpr f_cnt_t kFrames = 64;

	// Feeds `frames` frames of constant 0.25 on both channels through the effect's
	// bus path. Frames is 0 for an empty block; the storage is still valid.
	static void processBlock(Effect& effect, f_cnt_t frames)
	{
		SampleFrame storage[kFrames];
		for (f_cnt_t f = 0; f < kFrames; ++f)
		{
			storage[f][0] = 0.25f;
			storage[f][1] = 0.25f;
		}
		SampleFrame* busData[1] = { storage };
		AudioBus bus{busData, 1, frames};
		effect.processAudioBuffer(bus);
	}
};

QTEST_MAIN(PeakControllerEffectZeroFrameTest)

#include "PeakControllerEffectZeroFrameTest.moc"
