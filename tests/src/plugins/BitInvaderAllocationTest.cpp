/*
 * BitInvaderAllocationTest.cpp - BitInvader's per-note state on the render path
 *
 * A note takes a snapshot of the wavetable when it first renders. That snapshot
 * must not touch the heap while the engine renders the note, and it must stay
 * fixed for as long as the note sounds.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QtTest>

#include "../core/AllocationProbe.h"
#include "AudioEngine.h"
#include "BitInvader.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "base64.h"

namespace lmms
{

namespace
{

//! BitInvader's wavetable length (wavetableSize in plugins/BitInvader/BitInvader.h).
constexpr std::size_t WavetableSamples = 200;
//! Periods a held note is rendered for.
constexpr int HeldPeriods = 3;

using Wavetable = std::array<float, WavetableSamples>;

Wavetable squareWave()
{
	Wavetable shape{};
	for (std::size_t i = 0; i < shape.size(); ++i)
	{
		shape[i] = i < shape.size() / 2 ? 1.0f : -1.0f;
	}
	return shape;
}

//! Replaces the wavetable through a settings round trip, the way a project load does.
void setShape(BitInvader& instrument, const Wavetable& shape)
{
	QDomDocument doc;
	QDomElement settings = doc.createElement("bitinvader");
	instrument.saveSettings(doc, settings);

	QString encoded;
	base64::encode(reinterpret_cast<const char*>(shape.data()), static_cast<int>(sizeof(shape)), encoded);
	settings.setAttribute("sampleShape", encoded);
	instrument.loadSettings(settings);
}

bool sameFrames(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].left() != b[i].left() || a[i].right() != b[i].right()) { return false; }
	}
	return true;
}

//! One BitInvader on its own track, with one note held across periods.
class HeldNote
{
public:
	explicit HeldNote(f_cnt_t frames)
		: m_track{std::make_unique<InstrumentTrack>(Engine::getSong())}
		, m_instrument{std::make_unique<BitInvader>(m_track.get())}
		, m_handle{m_track.get(), 0, frames * HeldPeriods,
			Note{TimePos{static_cast<tick_t>(frames * HeldPeriods)}, TimePos{0}, DefaultKey},
			nullptr, -1, NotePlayHandle::Origin::MidiClip}
		, m_buffer(static_cast<std::size_t>(frames))
	{
	}

	~HeldNote()
	{
		// Hand the plugin's note state back while the instrument is alive. The handle's
		// own destructor would otherwise reach the instrument through the track.
		if (m_handle.m_pluginData != nullptr)
		{
			m_instrument->deleteNotePluginData(&m_handle);
			m_handle.m_pluginData = nullptr;
		}
	}

	HeldNote(const HeldNote&) = delete;
	HeldNote& operator=(const HeldNote&) = delete;

	//! Renders the next period; the frames are then in buffer().
	void playPeriod()
	{
		std::fill(m_buffer.begin(), m_buffer.end(), SampleFrame{0.0f, 0.0f});
		m_instrument->playNote(&m_handle, std::span<SampleFrame>{m_buffer});
	}

	const std::vector<SampleFrame>& buffer() const { return m_buffer; }
	BitInvader& instrument() { return *m_instrument; }
	NotePlayHandle& handle() { return m_handle; }

private:
	std::unique_ptr<InstrumentTrack> m_track;
	std::unique_ptr<BitInvader> m_instrument;
	NotePlayHandle m_handle;
	std::vector<SampleFrame> m_buffer;
};

} // namespace

class BitInvaderAllocationTest : public QObject
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

	//! The first render of a note and every later one make no heap allocation, and the
	//! note's state lives in the handle rather than on the heap.
	void noteRenderMakesNoHeapAllocation()
	{
		HeldNote held{Engine::audioEngine()->framesPerPeriod()};

		for (int period = 0; period < HeldPeriods; ++period)
		{
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			held.playPeriod();
			lmms::test::tlCountAllocations = false;

			QVERIFY2(lmms::test::tlAllocationCount == 0,
				qPrintable(QStringLiteral("period %1 made %2 heap allocations on the render path")
					.arg(period)
					.arg(static_cast<qulonglong>(lmms::test::tlAllocationCount))));
		}

		QVERIFY2(held.handle().m_pluginData == held.handle().pluginDataStorage(),
			"BitInvader's note state must live in the note handle, not on the heap");
	}

	//! A note that is already sounding keeps the wavetable it started with, and a note started
	//! after an edit uses the new one.
	void soundingNoteKeepsItsNoteOnWavetable()
	{
		const auto frames = Engine::audioEngine()->framesPerPeriod();

		// Reference: a note that is never edited.
		HeldNote untouched{frames};
		untouched.playPeriod();
		const auto untouchedFirst = untouched.buffer();
		untouched.playPeriod();
		const auto untouchedSecond = untouched.buffer();

		// The same note, with its instrument's wavetable replaced between the two periods.
		HeldNote edited{frames};
		edited.playPeriod();
		const auto editedFirst = edited.buffer();
		setShape(edited.instrument(), squareWave());
		edited.playPeriod();
		const auto editedSecond = edited.buffer();

		QVERIFY2(sameFrames(editedFirst, untouchedFirst),
			"the first period must match the unedited note");
		QVERIFY2(sameFrames(editedSecond, untouchedSecond),
			"a sounding note must keep the wavetable it started with");

		// Without this, the comparison above would also pass if the edit had no effect at all.
		HeldNote fresh{frames};
		setShape(fresh.instrument(), squareWave());
		fresh.playPeriod();
		QVERIFY2(!sameFrames(fresh.buffer(), untouchedFirst),
			"a note started after the edit must use the new wavetable");
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::BitInvaderAllocationTest)

#include "BitInvaderAllocationTest.moc"
