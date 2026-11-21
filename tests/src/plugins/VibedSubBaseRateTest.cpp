/*
 * VibedSubBaseRateTest.cpp - a Vibed note must render when the output device
 * runs below the engine's base sample rate.
 *
 * cluster new-p15-238. plugins/Vibed/VibratingString.cpp derived its oversampling
 * factor from an integer quotient, 2 * oversample / (outputRate / baseRate). An
 * output rate below the base rate makes that quotient zero, so constructing a
 * string divided by zero. JACK and the SoundIO backend both hand the engine the
 * rate the driver actually runs at, which can sit below the configured base.
 *
 * The proof is the product path: a note played through Instrument::playNote on a
 * Vibed instrument, with the device pinned to a rate below, at, and above the
 * base rate. Every note must render finite, non-silent audio at the rate it was
 * asked for.
 *
 * The plug-in's own sources are compiled into this test (as the
 * VibedWaveformHandoffTest does), so the audio path under test is the shipping one.
 */

#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "Vibed.h"

using namespace lmms;

namespace
{

//! One period of one note, both channels.
using Block = std::vector<SampleFrame>;

constexpr int kKey = 62; //!< ~294 Hz, the key the handoff test renders

//! Whether every sample of the block is a finite number.
bool allFinite(const Block& block)
{
	return std::all_of(block.begin(), block.end(), [](const SampleFrame& frame) {
		return std::isfinite(frame.left()) && std::isfinite(frame.right());
	});
}

//! Whether any sample of the block is non-zero.
bool anySound(const Block& block)
{
	return std::any_of(block.begin(), block.end(), [](const SampleFrame& frame) {
		return frame.left() != 0.f || frame.right() != 0.f;
	});
}

} // namespace

class VibedSubBaseRateTest : public QObject
{
	Q_OBJECT

	//! One fresh note, one period, through the audio engine's own entry point
	//! into the plug-in (Instrument::playNote).
	Block renderNote()
	{
		const Note note(TimePos::fromFrames(m_frames * 4, Engine::framesPerTick()),
			TimePos(0), kKey);
		NotePlayHandle handle(m_track, 0, m_frames * 4, note, nullptr, -1,
			NotePlayHandle::Origin::MidiClip);
		Block block(static_cast<std::size_t>(m_frames), SampleFrame(0.f, 0.f));
		m_instrument->playNote(&handle, std::span<SampleFrame>(block));
		// The handle's destructor asks the TRACK's instrument for the note data;
		// the instrument under test is not installed on that track, so the test
		// makes that hand-over itself.
		m_instrument->deleteNotePluginData(&handle);
		handle.m_pluginData = nullptr;
		return block;
	}

	//! Pins the device rate, then checks the pin took effect, so a passing render
	//! cannot come from a rate the test never set.
	void setDeviceRate(sample_rate_t rate)
	{
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(rate);
		QCOMPARE(Engine::audioEngine()->audioDev()->sampleRate(), rate);
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// This test renders its blocks itself; nothing may pull on them in the
		// background.
		Engine::audioEngine()->audioDev()->stopProcessing();
		BufferManager::init(Engine::audioEngine()->framesPerPeriod());
		NotePlayHandleManager::init();
		m_engineUp = true;
		m_frames = Engine::audioEngine()->framesPerPeriod();

		m_savedRate = Engine::audioEngine()->audioDev()->sampleRate();

		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);

		m_instrument = std::make_unique<Vibed>(m_track);
	}

	void cleanupTestCase()
	{
		if (!m_engineUp) { return; }
		m_instrument.reset();
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(m_savedRate);
		Engine::getSong()->clearProject();
		Engine::destroy();
		m_engineUp = false;
	}

	//! The bug: the device rate is half the base rate, the case that used to
	//! divide by zero. The base and the double rate render as before; the
	//! sequence goes below, at, above, and back below, so a string built at one
	//! rate is never reused at another.
	void noteRendersAtEveryDeviceRate()
	{
		const sample_rate_t base = Engine::audioEngine()->baseSampleRate();
		const std::vector<sample_rate_t> rates{base / 2, base, base * 2, base / 2};

		for (const sample_rate_t rate : rates)
		{
			setDeviceRate(rate);
			const auto block = renderNote();
			QVERIFY2(allFinite(block),
				qPrintable(QStringLiteral("a note at device rate %1 rendered a non-finite sample").arg(rate)));
			QVERIFY2(anySound(block),
				qPrintable(QStringLiteral("a note at device rate %1 rendered silence").arg(rate)));
		}
	}

private:
	bool m_engineUp = false;
	InstrumentTrack* m_track = nullptr;
	std::unique_ptr<Vibed> m_instrument;
	f_cnt_t m_frames = 0;
	sample_rate_t m_savedRate = 0;
};

QTEST_GUILESS_MAIN(VibedSubBaseRateTest)
#include "VibedSubBaseRateTest.moc"
