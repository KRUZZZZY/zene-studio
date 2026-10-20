/*
 * VibedWaveformHandoffTest.cpp - the audio thread must read an immutable,
 * already published waveform, never the wave graph model the GUI is editing.
 *
 * cluster new-p12-278. plugins/Vibed/Vibed.cpp handed graphModel::samples()
 * straight to VibratingString's constructor, so a note that started while the
 * impulse editor was writing the model read that storage without any
 * synchronisation. The proof is a product-level contract, not a source check:
 * two notes rendered from the same waveform must be bit-identical, a note
 * started after an edit that was never handed off must render the last handed
 * off waveform, and a note started while the editor really is editing must
 * render one of the states that editor passes through - never a mixture.
 *
 * The plug-in's own sources are compiled into this test (as the SlicerT plug-in
 * tests do), so the audio path under test is the shipping one.
 */

#include <QTest>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "Engine.h"
#include "Graph.h"
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

constexpr int kWaveLength = 128;    //!< graphModel size: Vibed::s_sampleLength
constexpr int kHalf = kWaveLength / 2;
constexpr int kKey = 62;            //!< ~294 Hz: a string shorter than one period,
//!                                  so a note's block reflects the WHOLE waveform
constexpr int kMaxReads = 2000;

bool identical(const Block& lhs, const Block& rhs)
{
	return lhs.size() == rhs.size()
		&& std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(SampleFrame)) == 0;
}

bool knownBlock(const Block& block, const std::vector<Block>& states)
{
	for (const auto& state : states)
	{
		if (identical(block, state)) { return true; }
	}
	return false;
}

} // namespace

class VibedWaveformHandoffTest : public QObject
{
	Q_OBJECT

	//! Fills the wave graph model with one value - a whole-array write, the
	//! "load a project" shape.
	void fillWith(float value)
	{
		const std::array<float, kWaveLength> flat{value};
		m_wave->setSamples(flat.data());
	}

	/*! One step of an edit that grows inwards from both ends of the waveform:
	 *  pair @a pair becomes @a value. A user dragging back and forth across the
	 *  graph produces element writes in any order, and these are the states that
	 *  order leaves behind: @a value on both outer shells, -@a value in the middle.
	 */
	void editOuterPair(int pair, float value)
	{
		m_wave->setSampleAt(pair, value);
		m_wave->setSampleAt(kWaveLength - 1 - pair, value);
	}

	//! The editor loop: run it until @a stop, never pausing long enough for the
	//! audio thread to be anything but concurrent with it.
	void editForever(std::atomic<bool>& stop)
	{
		while (!stop.load(std::memory_order_acquire))
		{
			for (const float edge : {1.f, -1.f})
			{
				fillWith(-edge);
				for (int pair = 0; pair < kHalf; ++pair)
				{
					editOuterPair(pair, edge);
				}
			}
		}
	}

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

		// VibratingString derives its oversampling factor from the output rate
		// (2 * length / (rate / baseRate)); at twice the base rate that is a
		// single oversample step per sample, which makes a note's render a pure
		// function of the waveform the string was given.
		m_savedRate = Engine::audioEngine()->audioDev()->sampleRate();
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(
			Engine::audioEngine()->baseSampleRate() * 2);

		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);

		m_instrument = std::make_unique<Vibed>(m_track);
		// The wave graph models are the plug-in's own children, in string order.
		const auto waves = m_instrument->findChildren<graphModel*>();
		QVERIFY(!waves.empty());
		m_wave = waves.front();
		QCOMPARE(m_wave->length(), kWaveLength);
	}

	void cleanupTestCase()
	{
		if (!m_engineUp) { return; }
		m_wave = nullptr;
		m_instrument.reset();
		Engine::audioEngine()->audioDev()->setSampleRateForTesting(m_savedRate);
		Engine::getSong()->clearProject();
		Engine::destroy();
		m_engineUp = false;
	}

	//! The premise every check below rests on: an unchanged waveform renders an
	//! unchanged note, so any difference between two blocks is the waveform.
	void oneWaveformRendersTheSameNoteEveryTime()
	{
		fillWith(1.f);
		const auto first = renderNote();
		const auto second = renderNote();
		QVERIFY2(identical(first, second),
			"two notes from an unedited waveform rendered different blocks, so a "
			"block cannot be read as a waveform");
	}

	//! Every waveform action the impulse editor offers must reach the NEXT note:
	//! the hand-off may not drop, delay or mangle one.
	void everyWaveformActionReachesTheNextNote()
	{
		fillWith(0.5f);
		const auto half = renderNote();
		m_wave->normalize();
		const auto normalized = renderNote();
		m_wave->setWaveToSquare();
		const auto square = renderNote();
		m_wave->smooth();
		const auto smoothed = renderNote();
		m_wave->setWaveToNoise();
		const auto noise = renderNote();

		QVERIFY2(!identical(half, normalized), "normalize did not reach the next note");
		QVERIFY2(!identical(normalized, square), "the square wave action did not reach the next note");
		QVERIFY2(!identical(square, smoothed), "smooth did not reach the next note");
		QVERIFY2(!identical(smoothed, noise), "the noise wave action did not reach the next note");
	}

	//! The handoff itself: a waveform the GUI thread changed in the model but
	//! never handed off must not reach a note that starts after the change.
	void anUnpublishedWaveformEditNeverReachesANewNote()
	{
		fillWith(1.f);
		const auto handedOff = renderNote();

		// The element-wise write a waveform drag performs, applied to the
		// model's own storage: the model now holds another waveform and nothing
		// has handed that waveform to the audio thread.
		auto* storage = const_cast<float*>(m_wave->samples());
		for (int i = 0; i < kWaveLength; ++i) { storage[i] = -1.f; }
		const auto edited = renderNote();

		// The control: handed off through the model the same edit DOES change a
		// note, so the check above has the power to fail.
		fillWith(-1.f);
		const auto publishedEdit = renderNote();
		QVERIFY2(!identical(handedOff, publishedEdit),
			"the handed-off and the edited waveform render the same block, so "
			"this check cannot tell them apart");

		QVERIFY2(identical(handedOff, edited),
			"a note started after an in-place waveform edit rendered a waveform "
			"the GUI thread never handed off");
	}

	//! The reported race: notes keep starting while the impulse editor is being
	//! edited, and every one of them must render a state that editor passes
	//! through - a half-applied waveform is a state it never had.
	void aWaveformEditedWhileNotesStartIsNeverReadHalfApplied()
	{
		std::vector<Block> states;
		states.reserve(2 * (kHalf + 1));
		for (int depth = 0; depth <= kHalf; ++depth)
		{
			for (const float edge : {1.f, -1.f})
			{
				fillWith(-edge);
				for (int pair = 0; pair < depth; ++pair) { editOuterPair(pair, edge); }
				states.push_back(renderNote());
			}
		}

		std::atomic<bool> stop{false};
		std::atomic<int> reads{0};
		std::atomic<int> foreign{0};
		std::thread editor([this, &stop] { editForever(stop); });

		while (reads.load(std::memory_order_relaxed) < kMaxReads
			&& foreign.load(std::memory_order_relaxed) == 0)
		{
			const auto block = renderNote();
			reads.fetch_add(1, std::memory_order_relaxed);
			if (!knownBlock(block, states)) { foreign.fetch_add(1, std::memory_order_relaxed); }
		}

		stop.store(true, std::memory_order_release);
		editor.join();

		QVERIFY2(reads.load() > 0, "no note was rendered while the editor was running");
		QCOMPARE(foreign.load(), 0);
	}

	//! The hand-off must protect a reader that HOLDS a waveform: the writer may
	//! publish as often as it likes, but a waveform the reader took must not
	//! change under it. This is the deterministic form of the reported race -
	//! the reader here is paused on a snapshot while three further publications
	//! go past.
	void aHeldWaveformSurvivesThreePublications()
	{
		Vibed::WaveformSnapshot snapshot;
		const auto first = flatWaveform(0.25f);
		snapshot.publish(first.data());

		// The audio thread takes the published waveform and keeps it while it
		// copies it into the string.
		const float* held = snapshot.samples();

		// The GUI thread publishes three more waveforms meanwhile.
		const auto second = flatWaveform(-0.5f);
		snapshot.publish(second.data());
		const auto third = flatWaveform(0.75f);
		snapshot.publish(third.data());
		const auto fourth = flatWaveform(-1.f);
		snapshot.publish(fourth.data());

		QVERIFY2(std::memcmp(held, first.data(), sizeof(first)) == 0,
			"a waveform the reader was holding was rewritten by a later "
			"publication: the reader would copy a half-written waveform");

		// The control: the publications did reach the next reader, so the check
		// above has the power to fail.
		const float* latest = snapshot.samples();
		QVERIFY2(std::memcmp(latest, fourth.data(), sizeof(fourth)) == 0,
			"the last publication did not reach the reader");
	}

private:
	//! One flat waveform of `kWaveLength` samples.
	static std::array<float, kWaveLength> flatWaveform(float value)
	{
		std::array<float, kWaveLength> samples;
		samples.fill(value);
		return samples;
	}

	bool m_engineUp = false;
	InstrumentTrack* m_track = nullptr;
	std::unique_ptr<Vibed> m_instrument;
	graphModel* m_wave = nullptr;
	f_cnt_t m_frames = 0;
	sample_rate_t m_savedRate = 0;
};

QTEST_GUILESS_MAIN(VibedWaveformHandoffTest)
#include "VibedWaveformHandoffTest.moc"
