/*
 * VibratingStringTest.cpp - the string model's oversampling count is never zero.
 *
 * cluster new-p9-503. VibratingString derives its oversampling count by integer
 * division: 2 * setting / (outputRate / baseRate). At an output rate above twice
 * the setting's base (4x at setting 1, e.g. 192 kHz output on a 44.1 kHz base) the
 * count is 0, so m_outsamp is empty while nextSample() still reads m_outsamp[m_choice].
 * An output rate below the base rate divides by zero. The constructor runs on the
 * audio thread (Vibed::playNoteImpl), so the count is fixed in the constructor and
 * the test drives the constructor directly.
 */

#include <QTest>

#include <array>
#include <cmath>
#include <memory>

#include "AudioEngine.h"
#include "Engine.h"
#include "LmmsTypes.h"
#include "VibratingString.h"

using namespace lmms;

namespace
{

constexpr auto kImpulseLength = 128; //!< Vibed's waveform length (Vibed::s_sampleLength)
constexpr auto kPitch = 440.f;
constexpr auto kPickup = 0.25f;
constexpr auto kPick = 0.1f;
constexpr auto kSteps = 64;

//! A full-scale impulse: every string built from it has energy at the pickup.
std::array<float, kImpulseLength> unitImpulse()
{
	std::array<float, kImpulseLength> impulse;
	impulse.fill(1.f);
	return impulse;
}

//! A string at `sampleRate` (the output rate) and `setting` (the Vibed length model).
std::unique_ptr<VibratingString> makeString(sample_rate_t sampleRate, int setting)
{
	const auto impulse = unitImpulse();
	return std::make_unique<VibratingString>(kPitch, kPick, kPickup, impulse.data(), kImpulseLength,
		sampleRate, setting, 0.f, 0.f, 0.f, true);
}

} // namespace

class VibratingStringTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The reported case: four times the base rate at setting 1 used to give a count of 0.
	void oneStepWhenTheRateIsFourTimesTheBase()
	{
		const auto base = Engine::audioEngine()->baseSampleRate();
		const auto string = makeString(base * 4, 1);

		QCOMPARE(string->oversampleCount(), 1);

		// The string must produce audio: the step loop runs and the buffer is read.
		bool audible = false;
		for (int i = 0; i < kSteps; ++i)
		{
			const auto sample = string->nextSample();
			QVERIFY2(std::isfinite(sample), "a string step produced a non-finite sample");
			audible = audible || sample != 0.f;
		}
		QVERIFY2(audible, "a string built from a full-scale impulse stayed silent");
	}

	//! Counts the integer division already gets right must not move.
	void unchangedWhereTheRateFits()
	{
		const auto base = Engine::audioEngine()->baseSampleRate();
		QCOMPARE(makeString(base, 1)->oversampleCount(), 2);
		QCOMPARE(makeString(base * 2, 1)->oversampleCount(), 1);
		QCOMPARE(makeString(base * 2, 3)->oversampleCount(), 3);
		QCOMPARE(makeString(base * 4, 4)->oversampleCount(), 2);
	}

	//! An output rate below the base rate used to divide by zero.
	void aRateBelowTheBaseIsTreatedAsTheBase()
	{
		const auto base = Engine::audioEngine()->baseSampleRate();
		const auto string = makeString(base / 2, 1);

		QCOMPARE(string->oversampleCount(), 2);
	}
};

QTEST_GUILESS_MAIN(VibratingStringTest)
#include "VibratingStringTest.moc"
