/*
 * SfxrPitchBoundTest.cpp - one Sfxr period renders a bounded amount of synth
 *
 * SfxrSynth generates one frame per 44100 Hz sample, and a note plays it
 * frequency/baseFreq times faster, so one output frame takes that many synth
 * frames. Nothing bounded that ratio: a tuning far below the note's own
 * frequency asked one audio period to render hundreds of thousands of synth
 * frames before the audio callback could return, and the discard call at the
 * end of the period wrote into a null buffer once the sound had ended
 * (new-p16-228). The render path now takes at most
 * SfxrInstrument's SfxrMaxSynthFramesPerOutputFrame synth frames per output
 * frame, so one period's synthesis has a fixed ceiling.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QtTest>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SessionTuning.h"
#include "Sfxr.h"
#include "Song.h"

namespace lmms
{

namespace
{

//! plugins/Sfxr/Sfxr.cpp's SfxrMaxSynthFramesPerOutputFrame: the most synth frames one output
//! frame may take. Pinned here on purpose - moving the source constant has to move this test.
constexpr f_cnt_t MaxSynthFramesPerOutputFrame = 64;

//! Periods a note in this test is given, so the note itself never ends inside the period under
//! test (applyRelease() then stays out of it).
constexpr f_cnt_t HeldPeriods = 4;

//! 84 semitones above base note 0: ratio 2^7 = 128, twice the bound, and an exact multiple of a
//! synth block so the period needs no discard call.
constexpr int AboveTheBoundKey = 84;
constexpr int AboveTheBoundBaseNote = 0;

//! Key 127 against base note 0: ratio 2^(127/12) = 1530.6 against the 440 Hz the track reports -
//! the extreme the finding describes, where the unbounded period renders 391k synth frames.
constexpr int ExtremeKey = NumKeys - 1;
constexpr int ExtremeBaseNote = 0;

//! Two octaves above the base note: ratio 4, far inside the bound.
constexpr int NormalKey = DefaultKey + 24;
constexpr int NormalBaseNote = DefaultKey;

bool sameFrames(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].left() != b[i].left() || a[i].right() != b[i].right()) { return false; }
	}
	return true;
}

bool silent(const std::vector<SampleFrame>& frames)
{
	return std::all_of(frames.begin(), frames.end(),
		[](const SampleFrame& frame) { return frame.left() == 0.0f && frame.right() == 0.0f; });
}

//! The synth's own frames, resampled the way the render path resamples a period: output frame i
//! takes synth frame i * pitched / frames.
std::vector<SampleFrame> resampled(const std::vector<SampleFrame>& synthFrames, f_cnt_t frames)
{
	const f_cnt_t pitched = static_cast<f_cnt_t>(synthFrames.size());
	std::vector<SampleFrame> out(static_cast<std::size_t>(frames));
	for (f_cnt_t i = 0; i < frames; ++i) { out[i] = synthFrames[i * pitched / frames]; }
	return out;
}

//! One Sfxr note on its own track, rendered one period at a time.
class HeldNote
{
public:
	HeldNote(int key, int baseNote, f_cnt_t frames)
		: m_track{std::make_unique<InstrumentTrack>(Engine::getSong())}
		, m_instrument{std::make_unique<SfxrInstrument>(m_track.get())}
		, m_frames{frames}
		, m_buffer(static_cast<std::size_t>(frames))
	{
		// The note takes its frequency when it is constructed, so the track carries the base note
		// first.
		m_track->baseNoteModel()->setValue(baseNote);
		// NotePlayHandle only has a placement operator new (its instances come from the engine's
		// note pool), so the handle sits in this object rather than on the heap.
		m_handle.emplace(m_track.get(), 0, frames * HeldPeriods,
			Note{TimePos{static_cast<tick_t>(frames * HeldPeriods)}, TimePos{0}, key},
			nullptr, -1, NotePlayHandle::Origin::MidiClip);
	}

	~HeldNote()
	{
		// Hand the plugin's note state back while the instrument is alive. The handle's own
		// destructor would otherwise reach the instrument through the track.
		if (m_handle.has_value() && m_handle->m_pluginData != nullptr)
		{
			m_instrument->deleteNotePluginData(&*m_handle);
			m_handle->m_pluginData = nullptr;
		}
	}

	HeldNote(const HeldNote&) = delete;
	HeldNote& operator=(const HeldNote&) = delete;

	//! Gives the sound a long sustain and decay, so a period at an extreme pitch still renders
	//! inside the sound rather than after its end.
	void holdTheSound()
	{
		QDomDocument doc;
		QDomElement settings = doc.createElement("sfxr");
		m_instrument->saveSettings(doc, settings);
		settings.setAttribute("hold", QStringLiteral("1.0"));
		settings.setAttribute("dec", QStringLiteral("1.0"));
		m_instrument->loadSettings(settings);
	}

	//! Renders the next period; the frames are then in buffer().
	void playPeriod()
	{
		std::fill(m_buffer.begin(), m_buffer.end(), SampleFrame{0.0f, 0.0f});
		m_instrument->playNote(&*m_handle, std::span<SampleFrame>{m_buffer});
	}

	const std::vector<SampleFrame>& buffer() const { return m_buffer; }
	SfxrInstrument& instrument() { return *m_instrument; }
	NotePlayHandle& handle() { return *m_handle; }
	f_cnt_t frames() const { return m_frames; }

	//! How much faster than the track's base frequency this note plays the synth.
	float pitchRatio() const { return m_handle->frequency() / m_track->baseFreq(); }

	//! The synth frames one period renders: the render path's own arithmetic, with the pitch bound.
	//! Without the bound this is the whole pitch ratio times the period, which is what made one
	//! period render hundreds of thousands of frames.
	f_cnt_t synthFramesForPeriod() const
	{
		const float rateScale = Engine::audioEngine()->outputSampleRate() / 44100.f;
		const float truncatedProduct = std::trunc(pitchRatio() * static_cast<float>(m_frames));
		const double pitchedFrames = rateScale != 0.f ? std::trunc(truncatedProduct / rateScale) : 0.0;
		// The render path clamps the count in double before converting it: a pitch past f_cnt_t's range
		// cannot be converted at all, and a non-finite one is clamped to the ceiling too.
		const f_cnt_t ceiling = m_frames * MaxSynthFramesPerOutputFrame;
		if (!(pitchedFrames < static_cast<double>(ceiling))) { return ceiling; }
		return pitchedFrames > 0.0 ? static_cast<f_cnt_t>(pitchedFrames) : 0;
	}

private:
	std::unique_ptr<InstrumentTrack> m_track;
	std::unique_ptr<SfxrInstrument> m_instrument;
	std::optional<NotePlayHandle> m_handle;
	f_cnt_t m_frames;
	std::vector<SampleFrame> m_buffer;
};

} // namespace

class SfxrPitchBoundTest : public QObject
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
		SessionTuning::instance()->reset();
		Engine::getSong()->clearProject();
		Engine::destroy();
		NotePlayHandleManager::free();
	}

	//! A note twice above the pitch bound must render the bounded pitch, not the 128 synth frames
	//! per output frame its ratio asks for.
	void pitchedNoteAboveTheBoundIsClampedToTheBoundedPitch()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		HeldNote note{AboveTheBoundKey, AboveTheBoundBaseNote, frames};
		note.holdTheSound();

		const double unbounded = static_cast<double>(note.pitchRatio()) * frames;
		QVERIFY2(unbounded > static_cast<double>(note.synthFramesForPeriod()),
			qPrintable(QStringLiteral("pitch ratio %1 is not above the bound").arg(note.pitchRatio())));

		note.playPeriod();

		const auto pitched = note.synthFramesForPeriod();
		SfxrSynth reference{&note.instrument()};
		std::vector<SampleFrame> raw(static_cast<std::size_t>(pitched));
		reference.update(raw.data(), static_cast<int32_t>(pitched));

		QVERIFY2(sameFrames(note.buffer(), resampled(raw, frames)),
			"a note above the bound must render the bounded pitch");
	}

	//! The finding's own case: 1530.6 synth frames per output frame, 391k of them for one period.
	void extremePitchedNoteRendersABoundedPeriod()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		HeldNote note{ExtremeKey, ExtremeBaseNote, frames};
		note.holdTheSound();

		const double unbounded = static_cast<double>(note.pitchRatio()) * frames;
		QVERIFY2(unbounded > 1000.0 * frames,
			qPrintable(QStringLiteral("pitch ratio %1 is not extreme").arg(note.pitchRatio())));
		QVERIFY2(note.synthFramesForPeriod() < static_cast<f_cnt_t>(unbounded),
			"the period must render less than the whole pitch ratio");

		note.playPeriod();

		const auto pitched = note.synthFramesForPeriod();
		SfxrSynth reference{&note.instrument()};
		std::vector<SampleFrame> raw(static_cast<std::size_t>(pitched));
		reference.update(raw.data(), static_cast<int32_t>(pitched));

		QVERIFY2(sameFrames(note.buffer(), resampled(raw, frames)),
			"an extreme-pitch period must render the bounded pitch");
	}

	//! The bound must not reach a normal note: two octaves up still renders every synth frame it
	//! always did.
	void normalPitchedNoteIsRenderedUnchanged()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		HeldNote note{NormalKey, NormalBaseNote, frames};

		QVERIFY2(note.synthFramesForPeriod() == frames * 4,
			qPrintable(QStringLiteral("a 4x note must take 4 synth frames per output frame, took %1")
				.arg(note.synthFramesForPeriod() / frames)));

		note.playPeriod();

		const auto pitched = note.synthFramesForPeriod();
		SfxrSynth reference{&note.instrument()};
		std::vector<SampleFrame> raw(static_cast<std::size_t>(pitched));
		reference.update(raw.data(), static_cast<int32_t>(pitched));

		QVERIFY2(sameFrames(note.buffer(), resampled(raw, frames)),
			"a note inside the bound must render exactly the synth frames it always did");
	}

	//! The sound is shorter than the note: once the synth has run out, the rest of the note is
	//! silent, and every period still lines up with the synth's own frames.
	void periodsPastTheEndOfTheSoundStaySilent()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		HeldNote note{NormalKey, NormalBaseNote, frames};

		const auto pitched = note.synthFramesForPeriod();
		SfxrSynth reference{&note.instrument()};
		std::vector<SampleFrame> raw(static_cast<std::size_t>(pitched));

		// The sound lasts about 25000 synth frames, 24 periods of 4x, so 40 periods cross its end.
		bool audible = false;
		for (int period = 0; period < 40; ++period)
		{
			note.playPeriod();
			reference.update(raw.data(), static_cast<int32_t>(pitched));

			QVERIFY2(sameFrames(note.buffer(), resampled(raw, frames)),
				qPrintable(QStringLiteral("period %1 must render the synth's own frames").arg(period)));
			audible = audible || !silent(note.buffer());
		}

		QVERIFY2(audible, "the note must have sounded before it went quiet");
		QVERIFY2(silent(note.buffer()), "a period after the sound has ended must be silent");
	}

	//! MTS-ESP accepts any finite positive frequency a float holds (`src/core/SessionTuning.cpp:373-381`),
	//! so a note can be tuned to 1e30 Hz and its pitch count is far past the range of f_cnt_t. The count
	//! must be clamped in double, BEFORE that conversion: converting an out-of-range value is undefined,
	//! and the conversion this platform emits for one yields zero - a period of silence.
	void sessionPitchBeyondFrameCountRangeIsClampedBeforeConversion()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		constexpr double ExtremeSessionFrequency = 1e30;
		SessionTuning* const tuning = SessionTuning::instance();
		QVERIFY2(tuning->setNoteFrequency(NormalKey, ExtremeSessionFrequency, QStringLiteral("test"), nullptr),
			"the session table must accept a frequency a float can hold");

		HeldNote note{NormalKey, NormalBaseNote, frames};
		note.holdTheSound();

		const auto pitched = note.synthFramesForPeriod();
		QVERIFY2(note.handle().frequency() > 1e29f,
			qPrintable(QStringLiteral("the note must carry the tuned frequency, took %1 Hz")
				.arg(note.handle().frequency())));
		QVERIFY2(pitched == frames * MaxSynthFramesPerOutputFrame,
			qPrintable(QStringLiteral("the count must be the ceiling, took %1").arg(pitched)));

		note.playPeriod();
		tuning->reset();

		SfxrSynth reference{&note.instrument()};
		std::vector<SampleFrame> raw(static_cast<std::size_t>(pitched));
		reference.update(raw.data(), static_cast<int32_t>(pitched));

		QVERIFY2(sameFrames(note.buffer(), resampled(raw, frames)),
			"an accepted 1e30 Hz tuning must render the bounded pitch, not an undefined conversion");
		QVERIFY2(!silent(note.buffer()), "that period must sound");
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::SfxrPitchBoundTest)

#include "SfxrPitchBoundTest.moc"
