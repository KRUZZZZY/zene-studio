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

// Complete fixture method definitions; included once after the fixture declaration.

void InstrumentRoutingTest::initTestCase() {
	QString why;
	QVERIFY2(initRackFixture(&why), qPrintable(why));
}
void InstrumentRoutingTest::cleanupTestCase() { teardownRackFixture(); }
void InstrumentRoutingTest::presetWithoutOutputBusNodeKeepsRoutes() {
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
void InstrumentRoutingTest::controllerLaneDoesNotResendAcrossMultiTickAdvances() {
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
void InstrumentRoutingTest::controllerLaneResendsAfterClipUnmute() {
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
void InstrumentRoutingTest::midiNoteActivityDoesNotQueueQtSignalsFromTheEventThread() {
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
void InstrumentRoutingTest::outputBusesRouteAndDeliver() {
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
void InstrumentRoutingTest::outputBusMasterIndexIsCached() {
	InstrumentOutputBuses buses;
	buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out"), 2, 2}});
	const int masterId = Engine::mixer()->mixerChannel(0)->id();
	QVERIFY(buses.route(1, masterId));
	QCOMPARE(buses.routedIndex(1), 0); // prime the cache

	resetMixerIndexLookupCountsForTesting();
	QCOMPARE(buses.routedIndex(1), 0);
	QCOMPARE(mixerIndexCacheHitsForTesting(), 1);
	QCOMPARE(mixerIndexFallbacksForTesting(), 0);
}
void InstrumentRoutingTest::outputBusSlotPublicationWaitsForRenderPeriod() {
	const TestChannel target = newChannel();
	InstrumentOutputBuses buses;
	buses.setLayout({{QStringLiteral("Main"), 2, 0}, {QStringLiteral("Out"), 2, 2}});

	QVERIFY(!routeMutationEscapesRenderPeriod(buses, target.id, [&buses] {
		return buses.m_slots[1] != nullptr;
	}));
	QCOMPARE(buses.routeOf(1), target.id);
	QVERIFY(buses.routedBuffer(1, Engine::audioEngine()->framesPerPeriod()) != nullptr);
}
void InstrumentRoutingTest::outputBusRoutesSurviveSaveAndLoad() {
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
void InstrumentRoutingTest::outputBusShrinkClearsRoutesFromRemovedBuses() {
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
void InstrumentRoutingTest::outputBusDeferredRoutesSurviveSaveBeforeLayoutAppears() {
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
void InstrumentRoutingTest::outputBusBufferRejectsBusOutsideCurrentLayout() {
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
void InstrumentRoutingTest::outputBusCapacityRemainsBounded() {
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
void InstrumentRoutingTest::malformedOutputBusChannelIsIgnored() {
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
void InstrumentRoutingTest::midiPartsPickChannels() {
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
void InstrumentRoutingTest::midiPartsForgetChannelAfterClear() {
	InstrumentMidiParts parts;
	parts.zones().addZone(zoneOf(0, 127, 0, 200, 1));
	QCOMPARE(parts.noteOnChannel(60, 100, 0), 1);
	QCOMPARE(parts.noteOnChannel(61, 100, 0), 1);
	parts.zones().assign({});                      // parts cleared while both notes still sound
	QCOMPARE(parts.noteOffChannel(61, 9), 1);      // a note that started on a part still ends there
	QCOMPARE(parts.noteOnChannel(60, 100, 0), 0);  // retriggered with no parts: the fallback channel
	QCOMPARE(parts.channelOfKey(60, 9), 9);        // nothing remembered: key pressure keeps its channel
	QCOMPARE(parts.noteOffChannel(60, 9), 9);      // not channel 1, where the earlier note-on of key 60 went
}
void InstrumentRoutingTest::zonesReportEveryMatch() {
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
void InstrumentRoutingTest::voicePoolIsFixed() {
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
void InstrumentRoutingTest::perVoiceFieldsRoundTrip() {
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
