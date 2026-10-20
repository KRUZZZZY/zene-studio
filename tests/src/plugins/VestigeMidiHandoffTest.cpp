/*
 * VestigeMidiHandoffTest.cpp - the VeSTige VST host's MIDI path.
 *
 * Two properties of plugins/Vestige/Vestige.cpp are proven here:
 *
 *  - the "Turn off all notes" button ends the notes of EVERY MIDI channel, not
 *    only of channel 0 (cluster new-p14-358). The button used to queue the
 *    note-offs of channel 0 only, so a note that was sounding on another
 *    channel of a multitimbral plug-in kept sounding.
 *  - a note that arrives on the audio thread is published into the bounded
 *    hand-off queue and reaches the plug-in in order (cluster 5451). The
 *    queue is what keeps the note path off the plug-in mutex; the static half
 *    of that claim is measured by the rt-safety sweep, which now declares
 *    VestigeInstrument::handleMidiEvent and VestigeInstrument::processImpl
 *    (tests/rt-safety-scope.txt).
 *
 * The plug-in's own sources are compiled into this test (the SlicerT and Vibed
 * tests' shape), so the code under test is the shipping one, and the button is
 * the shipping button: the test clicks it.
 */

#include <QtTest>

#include <QPushButton>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioPlugin.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "InstrumentView.h"
#include "MidiEvent.h"
#include "MidiHandoffQueue.h"
#include "RemotePluginAudioPorts.h"
#include "Song.h"
#include "SubWindow.h"
#include "TimePos.h"
#include "Track.h"

// Vestige.h pulls the headers above in; they are included first so that the
// access relaxation below reaches only Vestige's own classes and not the
// engine's.
#define private public
#include "../../../plugins/Vestige/Vestige.h"
#undef private

using namespace lmms;

namespace
{

//! What the plug-in would receive in one period: the queued events and the
//! all-notes-off cuts the overflow path owes.
struct Delivered
{
	std::vector<HandoffMidiEvent> events;
	std::vector<int> cuts;
};

//! The all-notes-off control change (MIDI CC 123) the overflow path sends to
//! cut a channel.
constexpr std::int16_t AllNotesOffController = 123;

bool isAllNotesOff(const HandoffMidiEvent& event)
{
	return event.type == MidiControlChange && event.param1 == AllNotesOffController;
}

//! What the button sent for one MIDI channel: an all-notes-off, or a note-off
//! for every key of the channel.
struct ChannelCoverage
{
	bool allNotesOff = false;
	std::array<bool, 128> noteOff{};

	//! Whether the notes of this channel end with what was sent.
	auto covered() const -> bool
	{
		return allNotesOff || std::all_of(noteOff.begin(), noteOff.end(),
			[](bool ended) { return ended; });
	}
};

} // namespace

class VestigeMidiHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		// Nothing may pull on the engine in the background while the test drives it.
		Engine::audioEngine()->audioDev()->stopProcessing();

		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);

		m_instrument = std::make_unique<VestigeInstrument>(m_track);
		QVERIFY(m_instrument != nullptr);
	}

	void cleanupTestCase()
	{
		m_instrument.reset();
		m_track = nullptr;
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! The button of the instrument view, found by its label.
	void turnOffAllEndsTheNotesOfEveryMidiChannel()
	{
		// A note that is sounding on a channel other than 0: it has to end too.
		QVERIFY(m_instrument->handleMidiEvent(
			MidiEvent(MidiNoteOn, 3, 60, 100), TimePos(0)));

		clickTurnOffAllNotes();

		const auto delivered = drain();

		// Every MIDI channel must be covered: either with an all-notes-off, or
		// with a note-off for every key of the channel. Channel 0 was the only
		// one the button used to cover.
		const auto covered = coverage(delivered);
		for (std::size_t channel = 0; channel < covered.size(); ++channel)
		{
			QVERIFY2(covered[channel].covered(),
				qPrintable(QStringLiteral("channel %1 was not cut: a note that is sounding "
					"on it keeps sounding after \"Turn off all notes\"").arg(channel)));
		}

		// The cut lands after the note it has to end: the note-on of channel 3
		// was queued before the button was pressed, so the end of that note has
		// to come after it.
		const auto noteOn = std::find_if(delivered.events.begin(), delivered.events.end(),
			[](const HandoffMidiEvent& event)
			{
				return event.type == MidiNoteOn && event.channel == 3 && event.param1 == 60;
			});
		QVERIFY(noteOn != delivered.events.end());
		const auto endOfTheNote = std::find_if(noteOn, delivered.events.end(),
			[](const HandoffMidiEvent& event)
			{
				return (event.type == MidiNoteOff && event.channel == 3 && event.param1 == 60)
					|| (isAllNotesOff(event) && event.channel == 3);
			});
		QVERIFY2(endOfTheNote != delivered.events.end(),
			"the note of channel 3 was never ended");
	}

	//! A note on the audio thread is published into the queue and reaches the
	//! plug-in in the order it arrived - without the plug-in mutex in between.
	void aNoteIsQueuedForThePlugin()
	{
		QVERIFY(m_instrument->handleMidiEvent(
			MidiEvent(MidiNoteOn, 5, 64, 100), TimePos(0)));
		QVERIFY(m_instrument->handleMidiEvent(
			MidiEvent(MidiNoteOff, 5, 64, 0), TimePos(0)));

		const auto delivered = drain();

		QCOMPARE(delivered.events.size(), std::size_t{2});
		QCOMPARE(delivered.events[0].type, std::uint8_t(MidiNoteOn));
		QCOMPARE(delivered.events[0].channel, std::int8_t{5});
		QCOMPARE(delivered.events[0].param1, std::int16_t{64});
		QCOMPARE(delivered.events[1].type, std::uint8_t(MidiNoteOff));
		QCOMPARE(delivered.events[1].channel, std::int8_t{5});
		QVERIFY(delivered.cuts.empty());
	}

private:
	//! Presses the instrument view's "Turn off all notes" button.
	void clickTurnOffAllNotes()
	{
		// The view needs a parent with a parent of its own: the instrument view
		// walks that chain to find the instrument track window, and it copes
		// with not finding one.
		auto* root = new QWidget();
		auto* middle = new QWidget(root);
		auto* view = new gui::VestigeInstrumentView(m_instrument.get(), middle);
		// The product's own path: Plugin::createView() hands the model to the
		// finished view, and that is where the view learns its instrument.
		view->setModel(m_instrument.get());

		QPushButton* button = nullptr;
		const auto buttons = view->findChildren<QPushButton*>();
		for (auto* candidate : buttons)
		{
			if (candidate->text().contains(QStringLiteral("Turn off all")))
			{
				button = candidate;
				break;
			}
		}
		QVERIFY2(button != nullptr, "the instrument view has no \"Turn off all notes\" button");
		button->click();

		// The view first: its destructor walks the parent chain to find the
		// instrument track window, so the chain has to be intact while it goes.
		delete view;
		delete root;
	}

	//! What the button sent for every MIDI channel.
	std::array<ChannelCoverage, 16> coverage(const Delivered& delivered) const
	{
		std::array<ChannelCoverage, 16> result;
		for (const auto& event : delivered.events)
		{
			const auto channel = static_cast<std::size_t>(event.channel);
			if (channel >= result.size()) { continue; }
			if (isAllNotesOff(event)) { result[channel].allNotesOff = true; }
			else if (event.type == MidiNoteOff && event.param1 >= 0 && event.param1 < 128)
			{
				result[channel].noteOff[static_cast<std::size_t>(event.param1)] = true;
			}
		}
		// A channel the overflow path had to cut is cut as well.
		for (const int channel : delivered.cuts)
		{
			result[static_cast<std::size_t>(channel)].allNotesOff = true;
		}
		return result;
	}

	//! The events the plug-in receives in one period, drained the way
	//! VestigeInstrument::processImpl() drains them.
	Delivered drain()
	{
		Delivered delivered;
		drainMidi(m_instrument->m_midiHandoff, m_instrument->m_midiOverflow,
			m_instrument->m_midiHandoff.capacity(),
			[&delivered](const HandoffMidiEvent& event) -> bool
			{
				delivered.events.push_back(event);
				return true;
			},
			[&delivered](int channel) -> bool
			{
				delivered.cuts.push_back(channel);
				return true;
			});
		return delivered;
	}

	InstrumentTrack* m_track = nullptr;
	std::unique_ptr<VestigeInstrument> m_instrument;
};

QTEST_MAIN(VestigeMidiHandoffTest)

#include "VestigeMidiHandoffTest.moc"
