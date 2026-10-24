/*
 * WatsynWaveSnapshotTest.cpp - a note that starts while a graph edit is
 * half-way through rewriting the wavetable must render the last COMPLETE
 * published wave, never the half-written one.
 *
 * cluster new-p17-203. The upstream code copied the instrument's staging
 * tables (A1_wave .. B2_wave) into the note's own object at note start
 * ("copy wavegraphs to the synth object to prevent race conditions"), while
 * the GUI thread's graph edit rewrites exactly those tables between two
 * statements of updateWaveA1() - so a note could start on a half-rewritten
 * waveform. The change under test (commit 899ae8ea7) made note start claim
 * the last complete published snapshot and hold it for the note's whole life.
 *
 * The proof is deterministic and needs no threads: the staging table is
 * poisoned exactly where a half-finished graph edit leaves it - its first
 * half rewritten, nothing published - then a note starts. The published
 * snapshot must not have moved: the new note renders exactly what a note
 * started moments earlier rendered. A completed graph edit first proves the
 * render does follow the wavetable, so the check has the power to fail.
 *
 * The sibling contract - a sounding note keeps its wave while the next note
 * gets a completed edit - is locked by
 * PluginPortsMigrationTest::watsynKeepsTheWaveANoteStartedWith; that test
 * drives completed edits only, so it cannot see a half-written table.
 *
 * The staging tables are the instrument's private state with no accessor, so
 * the test reaches them through the access seam SlicerTZoomTest and
 * PluginPortsMigrationTest already use. The plug-in's own sources are compiled
 * into this test (the SlicerT/Vibed/BitInvader shape), so the audio path under
 * test is the shipping one.
 */

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <QtTest>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

// The staging tables are private and have no accessor; the test writes them
// the way a half-finished graph edit leaves them.
#define private public
#include "Watsyn.h"
#undef private

namespace lmms
{

namespace
{

//! One period of one note, both channels.
using Block = std::vector<SampleFrame>;

//! The note handle outlives every period this test plays.
constexpr int MaxPeriods = 4;

bool sameFrames(const Block& lhs, const Block& rhs)
{
	if (lhs.size() != rhs.size()) { return false; }
	for (std::size_t i = 0; i < lhs.size(); ++i)
	{
		if (lhs[i].left() != rhs[i].left() || lhs[i].right() != rhs[i].right()) { return false; }
	}
	return true;
}

/*! Rewrites the first half of the staging table exactly where a half-finished
 *  graph edit leaves it: part of A1 rewritten, nothing published yet. The
 *  value is far outside [-1, 1] so any read of the poisoned half shows up in
 *  the rendered frames.
 */
void leaveStagingTableMidEdit(WatsynInstrument& instrument)
{
	for (int i = 0; i < WAVELEN / 2; ++i) { instrument.A1_wave[i] = 8000.0f; }
}

//! The action the A-wave editor's square button performs: a completed graph edit.
void completeGraphEdit(WatsynInstrument& instrument)
{
	instrument.a1_graph.setWaveToSquare();
}

//! One Watsyn on its own track.
class Rig
{
public:
	Rig()
		: m_track{std::make_unique<InstrumentTrack>(Engine::getSong())}
		, m_instrument{std::make_unique<WatsynInstrument>(m_track.get())}
	{
	}

	InstrumentTrack& track() { return *m_track; }
	WatsynInstrument& instrument() { return *m_instrument; }

private:
	std::unique_ptr<InstrumentTrack> m_track;
	std::unique_ptr<WatsynInstrument> m_instrument;
};

//! One note held across periods, on a rig's instrument.
class HeldNote
{
public:
	HeldNote(Rig& rig, f_cnt_t frames)
		: m_rig{rig}
		, m_handle{&rig.track(), 0, frames * MaxPeriods,
			Note{TimePos{static_cast<tick_t>(frames * MaxPeriods)}, TimePos{0}, DefaultKey},
			nullptr, -1, NotePlayHandle::Origin::MidiClip}
		, m_buffer(static_cast<std::size_t>(frames))
	{
	}

	~HeldNote()
	{
		// Hand the note's plugin state back while the instrument is alive; the
		// handle's own destructor would otherwise reach it through the track.
		if (m_handle.m_pluginData != nullptr)
		{
			m_rig.instrument().deleteNotePluginData(&m_handle);
			m_handle.m_pluginData = nullptr;
		}
	}

	HeldNote(const HeldNote&) = delete;
	HeldNote& operator=(const HeldNote&) = delete;

	//! Renders the next period; the frames are then in buffer().
	void playPeriod()
	{
		std::fill(m_buffer.begin(), m_buffer.end(), SampleFrame{0.0f, 0.0f});
		m_rig.instrument().playNote(&m_handle, std::span<SampleFrame>{m_buffer});
	}

	const Block& buffer() const { return m_buffer; }

private:
	Rig& m_rig;
	NotePlayHandle m_handle;
	Block m_buffer;
};

} // namespace

class WatsynWaveSnapshotTest : public QObject
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

	void aNoteStartedWhileTheWavetableIsMidEditRendersThePublishedWave()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		Rig rig;

		// What the published wave renders: a note started before anything is touched.
		HeldNote early{rig, frames};
		early.playPeriod();
		const Block published = early.buffer();

		// The staging table now holds what a half-finished graph edit leaves
		// behind; nothing has published that state.
		leaveStagingTableMidEdit(rig.instrument());

		// A note that starts now must claim the published snapshot, ...
		HeldNote late{rig, frames};
		late.playPeriod();

		// ... and a completed edit must change what a note renders, so the
		// check below has the power to fail.
		completeGraphEdit(rig.instrument());
		HeldNote edited{rig, frames};
		edited.playPeriod();
		QVERIFY2(!sameFrames(edited.buffer(), published),
			"a completed graph edit did not change what a new note renders, so "
			"this check cannot tell a published wave from a half-written one");

		QVERIFY2(sameFrames(late.buffer(), published),
			"a note started while the wavetable was mid-edit did not render the "
			"published wave: it read the staging table the graph edit was "
			"half-way through rewriting");
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::WatsynWaveSnapshotTest)

#include "WatsynWaveSnapshotTest.moc"
