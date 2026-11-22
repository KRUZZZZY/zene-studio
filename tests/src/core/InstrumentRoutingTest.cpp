/*
 * InstrumentRoutingTest.cpp - 0.5.0 F2: the engine halves of MX-114 (output
 * buses, MIDI parts), MX-112 (the zones' every-match lookup) and MX-120 (the
 * voice pool, the per-voice fields' persistence).
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

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <QDomDocument>
#include <QDomElement>

#include "Instrument.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "InstrumentOutputBus.h"
#include "LmmsTypes.h"
#include "Song.h"
#define private public // This TU checks slot publication and installs an in-process instrument probe.
#include "InstrumentTrack.h"
#include "InstrumentOutputBuses.h"
#undef private
#include "InstrumentMidiParts.h"
#include "ModulationLayer.h"
#include "ModulationVoices.h"
#include "RackTestSupport.h"
#include "RackZones.h"

using namespace racktest;

namespace lmms
{

LMMS_EXPORT void resetMixerIndexLookupCountsForTesting();
LMMS_EXPORT auto mixerIndexCacheHitsForTesting() -> int;
LMMS_EXPORT auto mixerIndexFallbacksForTesting() -> int;

} // namespace lmms

namespace
{

const Plugin::Descriptor s_trackNotesProbeDescriptor{
	"instrumenttracknotesprobe", "InstrumentTrack notes probe", "Test probe",
	"Zene Studio test suite", 0x0100, Plugin::Type::Instrument, nullptr, nullptr, nullptr};

class TrackNotesProbe final : public Instrument
{
public:
	explicit TrackNotesProbe(InstrumentTrack* track) : Instrument(track, &s_trackNotesProbeDescriptor)
	{
		m_buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Aux"), 2, 2}});
	}

	QString nodeName() const override { return QStringLiteral("instrumenttracknotesprobe"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
	gui::PluginView* instantiateView(QWidget*) override { return nullptr; }
	InstrumentOutputBuses* outputBuses() override { return &m_buses; }
	bool handleMidiEvent(const MidiEvent& event, const TimePos&, f_cnt_t) override
	{
		m_events.push_back(event);
		return true;
	}

	InstrumentOutputBuses& buses() { return m_buses; }
	const std::vector<MidiEvent>& events() const { return m_events; }

protected:
	void playNoteImpl(NotePlayHandle*, std::span<SampleFrame>) override {}

private:
	InstrumentOutputBuses m_buses;
	std::vector<MidiEvent> m_events;
};

class TrackNotesProbeInstall
{
public:
	explicit TrackNotesProbeInstall(InstrumentTrack& track) : m_track(track), m_original(track.m_instrument),
		m_probe(new TrackNotesProbe(&track))
	{
		m_track.m_instrument = m_probe;
	}
	~TrackNotesProbeInstall()
	{
		m_track.m_instrument = m_original;
		delete m_probe;
	}
	TrackNotesProbe* get() const { return m_probe; }

private:
	InstrumentTrack& m_track;
	Instrument* m_original;
	TrackNotesProbe* m_probe;
};

//! A channel of the mixer, created for this test: its index and persistent id.
struct TestChannel
{
	int index = -1;
	int id = -1;
};

TestChannel newChannel()
{
	TestChannel channel;
	channel.index = Engine::mixer()->createChannel();
	channel.id = Engine::mixer()->mixerChannel(channel.index)->id();
	return channel;
}

void silenceChannel(int index)
{
	MixerChannel* channel = Engine::mixer()->mixerChannel(index);
	channel->m_bus.silenceAllChannels();
	channel->m_hasInput = false;
}

template <typename Changed>
bool routeMutationEscapesRenderPeriod(InstrumentOutputBuses& buses, int channelId, Changed changed)
{
	AudioEngine* engine = Engine::audioEngine();
	engine->requestChangeInModel();
	std::atomic<bool> started{false};
	std::thread mutation([&buses, channelId, &started] {
		started.store(true, std::memory_order_release);
		buses.route(1, channelId);
	});
	while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
	while (!changed() && std::chrono::steady_clock::now() < deadline) { std::this_thread::yield(); }
	const bool escaped = changed();
	engine->doneChangeInModel();
	mutation.join();
	return escaped;
}

//! Four planar channels of one period: main L/R silent, bus 1 at (0.5, -0.25).
struct PlanarBlock
{
	explicit PlanarBlock(f_cnt_t frames) :
		data(4, std::vector<float>(static_cast<std::size_t>(frames), 0.0f))
	{
		std::fill(data[2].begin(), data[2].end(), 0.5f);
		std::fill(data[3].begin(), data[3].end(), -0.25f);
		for (std::size_t c = 0; c < 4; ++c) { pointers[c] = data[c].data(); }
	}
	std::vector<std::vector<float>> data;
	const float* pointers[4] = {};
};

RackZone zoneOf(int lowKey, int highKey, int lowVelocity, int highVelocity, int chain)
{
	RackZone zone;
	zone.lowKey = lowKey;
	zone.highKey = highKey;
	zone.lowVelocity = lowVelocity;
	zone.highVelocity = highVelocity;
	zone.chain = chain;
	return zone;
}

} // namespace

class InstrumentRoutingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	//! A preset omits project mixer routes, while a project without this node still resets old routes.
	void presetWithoutOutputBusNodeKeepsRoutes();

	//! Multi-tick advancement is normal; only an actual seek or clip-loop rewind resends a held value.
	void controllerLaneDoesNotResendAcrossMultiTickAdvances();

	//! A muted clip's held controller state must be restored when it starts playing again.
	void controllerLaneResendsAfterClipUnmute();

	//! Note activity must reach GUI receivers through polling, not queued Qt signals from the event thread.
	void midiNoteActivityDoesNotQueueQtSignalsFromTheEventThread();

	//! MX-114: the main bus is never routed, an inactive bus is enabled through the host's hook, and a routed
	//! bus's channels arrive in its mixer channel - nothing reaches it while the track is muted.
	void outputBusesRouteAndDeliver();

	//! A master route (mixer index zero) is cached after its first lookup.
	void outputBusMasterIndexIsCached();

	//! A newly routed Slot must not become visible to the render worker before the model guard.
	void outputBusSlotPublicationWaitsForRenderPeriod();

	//! MX-114: routes are saved by channel id and read back; a document without them clears them.
	void outputBusRoutesSurviveSaveAndLoad();

	//! A route belonging to a bus removed by a smaller host layout is discarded.
	void outputBusShrinkClearsRoutesFromRemovedBuses();

	//! A route for a bus absent from the loaded layout remains saved for a later layout.
	void outputBusDeferredRoutesSurviveSaveBeforeLayoutAppears();

	//! A loaded route for a bus outside the current layout must not expose a render buffer.
	void outputBusBufferRejectsBusOutsideCurrentLayout();

	//! The fixed slot array's last valid index works, and layout overflow cannot reach it.
	void outputBusCapacityRemainsBounded();

	void malformedOutputBusChannelIsIgnored();

	//! MX-114: without parts every note keeps its channel; with parts, first match wins, the note-off
	//! follows its note-on, and channel-wide messages reach every part's channel.
	void midiPartsPickChannels();

	//! MX-114: a note-on made after the parts were cleared forgets the channel an earlier note-on of its key used,
	//! so its note-off keeps its own fallback channel; a note that started on a part still ends on that part.
	void midiPartsForgetChannelAfterClear();

	//! MX-112: a layer rack stacks - matchMask reports every matching chain, resolve() still the first zone.
	void zonesReportEveryMatch();

	//! MX-120: one voice per (track, channel, key); a retrigger restarts it; the fixed pool drops, never grows.
	void voicePoolIsFixed();

	//! MX-120: a modulator's per-voice flag and a route's track address survive the project file; a
	//! modulator without them writes neither attribute (existing projects keep their bytes).
	void perVoiceFieldsRoundTrip();
};

#include "InstrumentRoutingTestParts1.h"

QTEST_GUILESS_MAIN(InstrumentRoutingTest)
#include "InstrumentRoutingTest.moc"
