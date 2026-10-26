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
	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase() { teardownRackFixture(); }

	//! A preset omits project mixer routes, while a project without this node still resets old routes.
	void presetWithoutOutputBusNodeKeepsRoutes()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		TrackNotesProbeInstall probe(*track);
		const TestChannel first = newChannel();
		QVERIFY(probe.get()->buses().route(1, first.id));

		QDomDocument doc;
		QDomElement preset = doc.createElement(QStringLiteral("track"));
		preset.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Instrument));
		QDomElement instrumentTrack = doc.createElement(QStringLiteral("instrumenttrack"));
		preset.appendChild(instrumentTrack);
		track->loadPreset(preset);
		QCOMPARE(probe.get()->buses().routeOf(1), first.id);

		const TestChannel second = newChannel();
		QDomElement outputBuses = doc.createElement(QStringLiteral("outputbuses"));
		QDomElement route = doc.createElement(QStringLiteral("route"));
		route.setAttribute(QStringLiteral("bus"), 1);
		route.setAttribute(QStringLiteral("channel"), second.id);
		outputBuses.appendChild(route);
		instrumentTrack.appendChild(outputBuses);
		track->loadPreset(preset);
		QCOMPARE(probe.get()->buses().routeOf(1), second.id);

		instrumentTrack.removeChild(outputBuses);
		track->loadSettings(preset);
		QCOMPARE(probe.get()->buses().routeOf(1), -1);
	}

	//! Multi-tick advancement is normal; only an actual seek or clip-loop rewind resends a held value.
	void controllerLaneDoesNotResendAcrossMultiTickAdvances()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		TrackNotesProbeInstall probe(*track);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setControllerPoints(ControllerKind::ProgramChange, 0, {{0, 7}, {200, 9}});
		MidiPlayback& playback = MidiPlayback::instance();
		playback.stopped();
		playback.tickPlayed(0, 0);
		track->playClipControllers(clip, 0, 0);
		QCOMPARE(probe.get()->events().size(), std::size_t{1});

		playback.tickPlayed(1, 0);
		track->playClipControllers(clip, 12, 0); // the period can cover several clip ticks
		QCOMPARE(probe.get()->events().size(), std::size_t{1});
		playback.tickPlayed(2, 0);
		track->playClipControllers(clip, 24, 0);
		QCOMPARE(probe.get()->events().size(), std::size_t{1});

		playback.tickPlayed(3, 0);
		track->playClipControllers(clip, 200, 0); // a changed value still goes out
		QCOMPARE(probe.get()->events().size(), std::size_t{2});
		playback.tickPlayed(4, 0);
		track->playClipControllers(clip, 220, 0);
		QCOMPARE(probe.get()->events().size(), std::size_t{2});

		playback.tickPlayed(7, 0); // a real song seek
		track->playClipControllers(clip, 225, 0);
		QCOMPARE(probe.get()->events().size(), std::size_t{3});
		playback.tickPlayed(8, 0);
		track->playClipControllers(clip, 205, 0); // clip-local loop rewind, same held value
		QCOMPARE(probe.get()->events().size(), std::size_t{4});

		clip->setControllerPoints(ControllerKind::ProgramChange, 0, {{0, 7}, {200, 9}});
		playback.tickPlayed(9, 0);
		track->playClipControllers(clip, 10, 0); // reconfigured lanes start with no sent value
		QCOMPARE(probe.get()->events().size(), std::size_t{5});
	}

	//! A muted clip's held controller state must be restored when it starts playing again.
	void controllerLaneResendsAfterClipUnmute()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		TrackNotesProbeInstall probe(*track);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setControllerPoints(ControllerKind::ProgramChange, 0, {{0, 7}});
		MidiPlayback& playback = MidiPlayback::instance();
		playback.stopped();
		playback.tickPlayed(0, 0);
		track->playClipControllers(clip, 10, 0); // value 7 was sent before the mute
		QCOMPARE(probe.get()->events().size(), std::size_t{1});

		playback.tickPlayed(1, 0);
		clip->setPlaybackWasMuted(true);
		track->play(TimePos(10), Engine::audioEngine()->framesPerPeriod(), 0);
		QCOMPARE(probe.get()->events().size(), std::size_t{2});
	}

	//! Note activity must reach GUI receivers through polling, not queued Qt signals from the event thread.
	void midiNoteActivityDoesNotQueueQtSignalsFromTheEventThread()
	{
		InstrumentTrack track{Engine::getSong()};
		QDomDocument doc;
		QDomElement state = doc.createElement(QStringLiteral("track"));
		state.setAttribute(QStringLiteral("type"), static_cast<int>(Track::Type::Instrument));
		doc.appendChild(state);
		QDomElement trackState = doc.createElement(QStringLiteral("instrumenttrack"));
		QDomElement instrument = doc.createElement(QStringLiteral("instrument"));
		instrument.setAttribute(QStringLiteral("name"), QStringLiteral("missing-instrument-for-activity-test"));
		trackState.appendChild(instrument);
		state.appendChild(trackState);
		track.restoreState(state);
		QVERIFY(track.instrument() != nullptr);

		int noteOns = 0;
		int noteOffs = 0;
		connect(&track, &InstrumentTrack::newNote, this, [&noteOns] { ++noteOns; });
		connect(&track, &InstrumentTrack::endNote, this, [&noteOffs] { ++noteOffs; });
		std::thread eventThread([&track] {
			track.processOutEvent(MidiEvent(MidiNoteOn, 0, 60, 100));
			track.processOutEvent(MidiEvent(MidiNoteOff, 0, 60, 0));
		});
		eventThread.join();
		QCoreApplication::processEvents();

		QCOMPARE(noteOns, 0);
		QCOMPARE(noteOffs, 0);
	}

	//! MX-114: the main bus is never routed, an inactive bus is enabled through the host's hook, and a routed
	//! bus's channels arrive in its mixer channel - nothing reaches it while the track is muted.
	void outputBusesRouteAndDeliver()
	{
		const TestChannel target = newChannel();
		InstrumentOutputBuses buses;
		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Kick"), 2, 2}, {QStringLiteral("Aux"), 2, -1}});
		QCOMPARE(buses.busCount(), 3);
		QVERIFY(!buses.route(0, target.id));       // the main output reaches the track, never a route
		QVERIFY(!buses.route(3, target.id));       // past the layout
		QVERIFY(!buses.route(2, target.id));       // inactive, and no host to enable it
		int asked = -1;
		buses.setEnableHook([&buses, &asked](int bus) {
			asked = bus;
			buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Kick"), 2, 2}, {QStringLiteral("Aux"), 2, 4}});
			return true;
		});
		QVERIFY(buses.route(2, target.id));
		QCOMPARE(asked, 2);
		QVERIFY(buses.route(1, target.id));
		QCOMPARE(buses.routedCount(), 2);
		QVERIFY(buses.route(2, -1));               // un-routed again
		QCOMPARE(buses.routeOf(2), -1);

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		PlanarBlock block(frames);
		silenceChannel(target.index);
		buses.deliver(block.pointers, 4, frames, true);
		QVERIFY(!Engine::mixer()->mixerChannel(target.index)->m_hasInput);  // muted: nothing delivered
		buses.deliver(block.pointers, 4, frames, false);
		const MixerChannel* channel = Engine::mixer()->mixerChannel(target.index);
		QVERIFY(channel->m_hasInput);
		QCOMPARE(channel->m_buffer[frames - 1][0], 0.5f);
		QCOMPARE(channel->m_buffer[frames - 1][1], -0.25f);
		silenceChannel(target.index);
	}

	//! A newly routed Slot must not become visible to the render worker before the model guard.
	void outputBusSlotPublicationWaitsForRenderPeriod()
	{
		const TestChannel target = newChannel();
		InstrumentOutputBuses buses;
		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out"), 2, 2}});

		QVERIFY(!routeMutationEscapesRenderPeriod(buses, target.id, [&buses] {
			return buses.m_slots[1] != nullptr;
		}));
		QCOMPARE(buses.routeOf(1), target.id);
		QVERIFY(buses.routedBuffer(1, Engine::audioEngine()->framesPerPeriod()) != nullptr);
	}

	//! MX-114: routes are saved by channel id and read back; a document without them clears them.
	void outputBusRoutesSurviveSaveAndLoad()
	{
		const TestChannel target = newChannel();
		InstrumentOutputBuses buses;
		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out 2"), 2, 2}});
		QVERIFY(buses.route(1, target.id));
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrumenttrack"));
		doc.appendChild(parent);
		buses.saveSettings(doc, parent);
		QCOMPARE(parent.elementsByTagName(QStringLiteral("route")).size(), 1);

		InstrumentOutputBuses loaded;
		loaded.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out 2"), 2, 2}});
		loaded.loadSettings(parent);
		QCOMPARE(loaded.routeOf(1), target.id);
		loaded.loadSettings(doc.createElement(QStringLiteral("instrumenttrack")));
		QCOMPARE(loaded.routedCount(), 0);
	}

	//! A route belonging to a bus removed by a smaller host layout is discarded.
	void outputBusShrinkClearsRoutesFromRemovedBuses()
	{
		const TestChannel target = newChannel();
		InstrumentOutputBuses buses;
		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Kick"), 2, 2},
			{QStringLiteral("Snare"), 2, 4}, {QStringLiteral("Aux"), 2, 6}});
		QVERIFY(buses.route(3, target.id));
		QCOMPARE(buses.routedCount(), 1);

		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Kick"), 2, 2}});

		QCOMPARE(buses.routeOf(3), -1);
		QCOMPARE(buses.routedCount(), 0);
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrumenttrack"));
		doc.appendChild(parent);
		buses.saveSettings(doc, parent);
		QCOMPARE(parent.elementsByTagName(QStringLiteral("route")).size(), 0);
	}

	//! A route for a bus absent from the loaded layout remains saved for a later layout.
	void outputBusDeferredRoutesSurviveSaveBeforeLayoutAppears()
	{
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrumenttrack"));
		doc.appendChild(parent);
		QDomElement outputBuses = doc.createElement(QStringLiteral("outputbuses"));
		parent.appendChild(outputBuses);
		QDomElement route = doc.createElement(QStringLiteral("route"));
		route.setAttribute(QStringLiteral("bus"), 2);
		route.setAttribute(QStringLiteral("channel"), 417);
		outputBuses.appendChild(route);

		InstrumentOutputBuses buses;
		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out"), 2, 2}});
		buses.loadSettings(parent);
		QCOMPARE(buses.routeOf(2), 417);

		QDomDocument savedDoc;
		QDomElement savedParent = savedDoc.createElement(QStringLiteral("instrumenttrack"));
		savedDoc.appendChild(savedParent);
		buses.saveSettings(savedDoc, savedParent);
		QCOMPARE(savedParent.elementsByTagName(QStringLiteral("route")).size(), 1);

		buses.setLayout({{QStringLiteral("Main"), 2, 0}});
		QCOMPARE(buses.routeOf(2), 417);  // the absent bus was never part of the prior layout

		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out"), 2, 2},
			{QStringLiteral("Deferred"), 2, 4}});
		QCOMPARE(buses.routeOf(2), 417);
	}

	//! A loaded route for a bus outside the current layout must not expose a render buffer.
	void outputBusBufferRejectsBusOutsideCurrentLayout()
	{
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrumenttrack"));
		doc.appendChild(parent);
		QDomElement outputBuses = doc.createElement(QStringLiteral("outputbuses"));
		parent.appendChild(outputBuses);
		QDomElement route = doc.createElement(QStringLiteral("route"));
		route.setAttribute(QStringLiteral("bus"), 2);
		route.setAttribute(QStringLiteral("channel"), 417);
		outputBuses.appendChild(route);

		InstrumentOutputBuses buses;
		buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out"), 2, 2}});
		buses.loadSettings(parent);
		QCOMPARE(buses.routeOf(2), 417);  // saved route retained until the matching layout appears
		QVERIFY(buses.routedBuffer(2, Engine::audioEngine()->framesPerPeriod()) == nullptr);
	}

	//! The fixed slot array's last valid index works, and layout overflow cannot reach it.
	void outputBusCapacityRemainsBounded()
	{
		const TestChannel target = newChannel();
		std::vector<InstrumentOutputBus> layout;
		for (int bus = 0; bus <= InstrumentOutputBuses::MaxBuses; ++bus)
		{
			layout.push_back({QStringLiteral("Bus %1").arg(bus), 2, bus * 2});
		}

		InstrumentOutputBuses buses;
		buses.setLayout(layout);
		QCOMPARE(buses.busCount(), InstrumentOutputBuses::MaxBuses);
		QVERIFY(buses.route(InstrumentOutputBuses::MaxBuses - 1, target.id));
		QVERIFY(buses.routedBuffer(InstrumentOutputBuses::MaxBuses - 1,
			Engine::audioEngine()->framesPerPeriod()) != nullptr);
		QVERIFY(!buses.route(InstrumentOutputBuses::MaxBuses, target.id));
		QVERIFY(buses.routedBuffer(InstrumentOutputBuses::MaxBuses,
			Engine::audioEngine()->framesPerPeriod()) == nullptr);
	}

	void malformedOutputBusChannelIsIgnored()
	{
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrumenttrack"));
		doc.appendChild(parent);
		QDomElement outputBuses = doc.createElement(QStringLiteral("outputbuses"));
		parent.appendChild(outputBuses);
		QDomElement route = doc.createElement(QStringLiteral("route"));
		route.setAttribute(QStringLiteral("bus"), 1);
		route.setAttribute(QStringLiteral("channel"), QStringLiteral("bad"));
		outputBuses.appendChild(route);

		InstrumentOutputBuses loaded;
		loaded.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out 2"), 2, 2}});
		loaded.loadSettings(parent);
		QCOMPARE(loaded.routeOf(1), -1);
		QCOMPARE(loaded.routedCount(), 0);
	}

	//! MX-114: without parts every note keeps its channel; with parts, first match wins, the note-off
	//! follows its note-on, and channel-wide messages reach every part's channel.
	void midiPartsPickChannels()
	{
		InstrumentMidiParts parts;
		QCOMPARE(parts.noteOnChannel(60, 100, 3), 3);
		parts.zones().addZone(zoneOf(0, 59, 0, 200, 1));
		parts.zones().addZone(zoneOf(60, 127, 150, 200, 2));  // loud notes only
		parts.zones().addZone(zoneOf(60, 127, 0, 200, 4));
		QCOMPARE(parts.noteOnChannel(40, 64, 0), 1);
		QCOMPARE(parts.noteOnChannel(70, 100, 0), 2);  // velocity 100 is note volume 156
		QCOMPARE(parts.noteOnChannel(72, 40, 0), 4);
		QCOMPARE(parts.noteOffChannel(40, 9), 1);
		QCOMPARE(parts.noteOffChannel(40, 9), 9);      // forgotten once it ended
		QCOMPARE(parts.channelOfKey(70, 9), 2);
		QCOMPARE(parts.channelMask(), static_cast<std::uint16_t>((1u << 1) | (1u << 2) | (1u << 4)));

		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrumenttrack"));
		doc.appendChild(parent);
		parts.saveSettings(doc, parent);
		InstrumentMidiParts loaded;
		loaded.loadSettings(parent);
		QCOMPARE(loaded.zones().zoneCount(), 3);
		QCOMPARE(loaded.zones().zone(1)->chain, 2);
		loaded.loadSettings(doc.createElement(QStringLiteral("instrumenttrack")));
		QVERIFY(!loaded.active());
	}

	//! MX-112: a layer rack stacks - matchMask reports every matching chain, resolve() still the first zone.
	void zonesReportEveryMatch()
	{
		RackZones zones;
		zones.addZone(zoneOf(0, 127, 0, 200, 0));
		zones.addZone(zoneOf(48, 72, 0, 200, 1));
		zones.addZone(zoneOf(48, 72, 120, 200, 2));
		QCOMPARE(zones.matchMask(60, 100), std::uint64_t{0b011});
		QCOMPARE(zones.matchMask(60, 150), std::uint64_t{0b111});
		QCOMPARE(zones.matchMask(30, 150), std::uint64_t{0b001});
		QCOMPARE(zones.resolve(60, 150), 0);
		zones.assign({zoneOf(0, 10, 0, 200, 3)});
		QCOMPARE(zones.zoneCount(), 1);
		QCOMPARE(zones.matchMask(60, 150), std::uint64_t{0});

		std::vector<RackZone> manyZones;
		manyZones.reserve(65);
		for (int i = 0; i < 64; ++i) { manyZones.push_back(zoneOf(0, 0, 0, 200, 1)); }
		manyZones.push_back(zoneOf(80, 80, 0, 200, 8));
		zones.assign(std::move(manyZones));
		QCOMPARE(zones.zoneCount(), 65);
		QCOMPARE(zones.resolve(80, 100), 64);
		QCOMPARE(zones.matchMask(80, 100), std::uint64_t{1} << 8);
	}

	//! MX-120: one voice per (track, channel, key); a retrigger restarts it; the fixed pool drops, never grows.
	void voicePoolIsFixed()
	{
		ModulationVoices voices;
		voices.noteOn(7, 0, 60, 1.0);
		voices.noteOn(7, 0, 60, 2.0);  // retriggered: the same voice, restarted
		QCOMPARE(voices.activeCount(), 1);
		ModulationVoice voice;
		int found = -1;
		for (int i = 0; i < ModulationVoices::VoiceCount && found < 0; ++i) { found = voices.voice(i, &voice) ? i : -1; }
		QVERIFY(found >= 0);
		QCOMPARE(voice.startedAt, 2.0);
		voices.noteOff(7, 0, 60);
		QCOMPARE(voices.activeCount(), 0);
		for (int key = 0; key < ModulationVoices::VoiceCount + 3; ++key) { voices.noteOn(1, 0, key, 0.0); }
		QCOMPARE(voices.activeCount(), ModulationVoices::VoiceCount);
		QCOMPARE(voices.dropped(), 3u);
		voices.clear();
		QCOMPARE(voices.activeCount(), 0);
	}

	//! MX-120: a modulator's per-voice flag and a route's track address survive the project file; a
	//! modulator without them writes neither attribute (existing projects keep their bytes).
	void perVoiceFieldsRoundTrip()
	{
		ModulationLayer layer;
		ModulatorSource source;
		source.triggerTrack = 12;
		source.perVoice = true;
		const int index = layer.addModulator(QStringLiteral("voice"), source);
		ModulationRoute route;
		route.channel = -1;
		route.track = 12;
		route.parameter = QStringLiteral("Cutoff");
		route.depth = 0.5f;
		QCOMPARE(layer.addRoute(index, route), 0);
		layer.addModulator(QStringLiteral("plain"), ModulatorSource{});

		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("song"));
		doc.appendChild(parent);
		QVERIFY(layer.saveSettings(doc, parent));
		const QDomNodeList modulators = parent.elementsByTagName(QStringLiteral("modulator"));
		QCOMPARE(modulators.size(), 2);
		QVERIFY(!modulators.at(1).toElement().hasAttribute(QStringLiteral("pervoice")));

		ModulationLayer loaded;
		QVERIFY(loaded.loadSettings(parent.firstChildElement()));
		QVERIFY(loaded.modulator(0)->source.perVoice);
		QCOMPARE(loaded.modulator(0)->routes.front().track, 12);
		QVERIFY(!loaded.modulator(1)->source.perVoice);
		QVERIFY(loaded.findRoute(route) != nullptr);
		ModulationRoute rackRoute = route;
		rackRoute.track = -1;
		QVERIFY(loaded.findRoute(rackRoute) == nullptr);  // a rack address is not the instrument's parameter
	}
};

QTEST_GUILESS_MAIN(InstrumentRoutingTest)
#include "InstrumentRoutingTest.moc"
