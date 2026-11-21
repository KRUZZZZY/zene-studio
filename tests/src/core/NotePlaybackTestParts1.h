/*
 * NotePlaybackTest.cpp - repeats, recurrence, articulations, chase, swing, lyrics and groups
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
 *
 */

// Complete fixture method definitions; included once after the fixture declaration.

void NotePlaybackTest::initTestCase() {
	#ifdef LMMS_TEST_PLUGIN_DIR
	qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, so notes start (SessionTuningTest's pattern)
#endif
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	NotePlayHandleManager::init();
	ControlRegistry::setReady(true);
}
void NotePlaybackTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::getSong()->clearProject();
	Engine::destroy();
}
void NotePlaybackTest::importingDistinctLyricsUsesIndexedLookup() {
	QDomDocument document;
	QDomElement emptyLyric = document.createElement(QStringLiteral("note"));
	NoteConditions emptyConditions;
	emptyConditions.load(emptyLyric);
	QCOMPARE(emptyConditions.lyric, 0);
	QCOMPARE(lyricText(0), QString());
	QCOMPARE(lyricText(std::numeric_limits<int>::max()), QString());

	constexpr int lyricCount = 512;
	std::vector<QString> lyrics;
	std::vector<QDomElement> notes;
	std::vector<int> ids;
	lyrics.reserve(lyricCount);
	notes.reserve(lyricCount);
	ids.reserve(lyricCount);
	for (int i = 0; i < lyricCount; ++i)
	{
		lyrics.push_back(QStringLiteral("new-p18-69-import-") + QString::number(i));
		QDomElement note = document.createElement(QStringLiteral("note"));
		note.setAttribute(QStringLiteral("lyr"), lyrics.back());
		notes.push_back(note);
	}

	resetInternLyricLookupWorkForTest();
	for (const QDomElement& note : notes)
	{
		NoteConditions conditions;
		conditions.load(note);
		ids.push_back(conditions.lyric);
	}
	for (int i = 0; i < lyricCount; ++i)
	{
		QCOMPARE(ids[static_cast<std::size_t>(i)], ids.front() + i);
		QCOMPARE(lyricText(ids[static_cast<std::size_t>(i)]), lyrics[static_cast<std::size_t>(i)]);

		NoteConditions repeated;
		repeated.load(notes[static_cast<std::size_t>(i)]);
		QCOMPARE(repeated.lyric, ids[static_cast<std::size_t>(i)]);
	}

	QVERIFY(internLyricLookupWorkForTest() <= static_cast<std::uint64_t>(2 * lyricCount));
}
void NotePlaybackTest::monstroNoteStartDoesNotAllocate() {
	Engine::getSong()->clearProject();
	m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	QVERIFY(m_track != nullptr);
	Instrument* instrument = m_track->loadInstrument(QStringLiteral("monstro"));
	QVERIFY(instrument != nullptr);
	QCOMPARE(instrument->nodeName(), QStringLiteral("monstro"));

	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
	const Note note(TimePos(384), TimePos(0), 60);
	NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, 0, frames, note);

	allocationCount = 0;
	countAllocations = true;
	instrument->playNote(nph, std::span<SampleFrame>{output});
	countAllocations = false;
	const std::uint64_t allocations = allocationCount;
	NotePlayHandleManager::release(nph);

	QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
}
void NotePlaybackTest::nesNoteStartDoesNotAllocate() {
	Engine::getSong()->clearProject();
	m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	QVERIFY(m_track != nullptr);
	Instrument* instrument = m_track->loadInstrument(QStringLiteral("nes"));
	QVERIFY(instrument != nullptr);
	QCOMPARE(instrument->nodeName(), QStringLiteral("nes"));

	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
	const Note note(TimePos(384), TimePos(0), 60);
	NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, 0, frames, note);
	QVERIFY(nph->m_pluginData == nullptr);

	allocationCount = 0;
	countAllocations = true;
	instrument->playNote(nph, std::span<SampleFrame>{output});
	countAllocations = false;
	const std::uint64_t allocations = allocationCount;
	QVERIFY(nph->m_pluginData != nullptr);
	NotePlayHandleManager::release(nph);

	QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
}
void NotePlaybackTest::overAlignedAllocationsAreCounted() {
	allocationCount = 0;
	countAllocations = true;
	auto* single = new OverAligned{};
	auto* array = new OverAligned[2]{};
	countAllocations = false;
	const std::uint64_t allocations = allocationCount;

	QVERIFY(reinterpret_cast<std::uintptr_t>(single) % alignof(OverAligned) == 0);
	QVERIFY(reinterpret_cast<std::uintptr_t>(array) % alignof(OverAligned) == 0);
	delete[] array;
	delete single;

	QCOMPARE(static_cast<qulonglong>(allocations), 2ull);
}
void NotePlaybackTest::plainAllocationsAreCounted() {
	// Positive control: a zero from monstroNoteStartDoesNotAllocate only means something if this counts.
	allocationCount = 0;
	countAllocations = true;
	auto* single = new int{0};
	countAllocations = false;
	const std::uint64_t allocations = allocationCount;
	delete single;

	QCOMPARE(static_cast<qulonglong>(allocations), 1ull);
}
void NotePlaybackTest::monstroConcurrentVoicesKeepIndependentModulation() {
	const auto [serialTrack, serialInstrument] = createModulatedMonstroTrack();
	QVERIFY(serialTrack != nullptr && serialInstrument != nullptr);
	const int renderContexts = audioEngineRenderContextCount();
	if (renderContexts < 2) { QSKIP("the audio engine has no parallel render worker"); }

	bool affinityMismatchEstablished = false;
	const auto parallel = createParallelMonstroTrack(renderContexts, &affinityMismatchEstablished);
	if (!affinityMismatchEstablished) { QSKIP("could not constrain Qt's ideal thread count for this test"); }
	InstrumentTrack* parallelTrack = parallel.first;
	Instrument* parallelInstrument = parallel.second;
	QVERIFY(parallelTrack != nullptr && parallelInstrument != nullptr);

	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	auto serialVoices = createMonstroVoiceSet(serialTrack, static_cast<std::size_t>(renderContexts), frames);
	auto parallelVoices = createMonstroVoiceSet(parallelTrack, static_cast<std::size_t>(renderContexts), frames);
	primeMonstroVoiceSet(serialInstrument, serialVoices);
	primeMonstroVoiceSet(parallelInstrument, parallelVoices);
	renderMonstroVoiceSetSerial(serialInstrument, serialVoices);
	renderMonstroVoiceSetConcurrent(parallelInstrument, parallelVoices);
	releaseMonstroVoiceSet(serialVoices);
	releaseMonstroVoiceSet(parallelVoices);
	QVERIFY2(monstroVoiceSetsMatch(serialVoices, parallelVoices),
		"a voice was dropped or its modulation changed at engine render concurrency");
}
void NotePlaybackTest::monstroRandomLfoAtLowSampleRateRenders() {
	// At 8 kHz the 0.1 ms LFO rate is 0.8 frames. The random LFO modes take the period as
	// whole frames for their modulo, and it truncated to zero: a division by zero on the
	// first period.
	DeviceRatePin pin(8000);
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	// WAVE_RANDOM (9) and WAVE_RANDOM_SMOOTH (10): both take the period from the same LFO rate
	for (const int wave : {9, 10})
	{
		Engine::getSong()->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Instrument* instrument = track->loadInstrument(QStringLiteral("monstro"));
		QVERIFY(instrument != nullptr);
		QDomDocument document;
		QDomElement settings = document.createElement(QStringLiteral("monstro"));
		settings.setAttribute(QStringLiteral("l1wav"), wave);
		settings.setAttribute(QStringLiteral("l1rat"), 0.1);   // LFO 1 rate in ms, the minimum
		settings.setAttribute(QStringLiteral("v1l1"), 1.0);
		instrument->restoreState(settings);

		std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
		const Note note(TimePos(384), TimePos(0), 60);
		NotePlayHandle* handle = NotePlayHandleManager::acquire(track, 0, frames, note);
		for (int period = 0; period < 16; ++period) { instrument->playNote(handle, output); }
		NotePlayHandleManager::release(handle);

		float peak = 0.0f;
		for (const SampleFrame& frame : output)
		{
			QVERIFY(std::isfinite(frame[0]) && std::isfinite(frame[1]));
			peak = std::max(peak, std::max(std::abs(frame[0]), std::abs(frame[1])));
		}
		QVERIFY2(peak > 0.0f, "the note rendered silence, so the LFO path proves nothing");
	}
	Engine::getSong()->clearProject();
}
void NotePlaybackTest::repeatsSoundAsMoreHits() {
	Note* note = oneNote();
	QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("repeats"), 3}})).ok);
	QCOMPARE(note->conditions().repeats, 3);
	QCOMPARE(playFrom(0), (QList<int>{60, 60, 60, 60}));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(m_clip->notes().front()->conditions().repeats, 0);
}
void NotePlaybackTest::stepSetRatchetAndChanceArePlayed() {
	oneNote();
	const QString clip = control::clipIdOf(m_clip);
	// An undo reloads the clip's notes, so each edit names the note as the clip holds it now.
	const auto step = [&](const QJsonObject& values) {
		QJsonObject entry = values;
		entry.insert(QStringLiteral("note"), control::noteIdOf(m_clip->notes().front()));
		return run(QStringLiteral("note.step_set"), {{QStringLiteral("clip"), clip}, {QStringLiteral("edits"), QJsonArray{entry}}});
	};
	QCOMPARE(playFrom(0), (QList<int>{60}));
	QVERIFY(step({{QStringLiteral("ratchet"), 4}}).ok);
	QCOMPARE(m_clip->notes().front()->conditions().repeats, 3);
	QCOMPARE(playFrom(0), (QList<int>{60, 60, 60, 60}));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(playFrom(0), (QList<int>{60}));
	QVERIFY(step({{QStringLiteral("chance"), 0.0}}).ok);
	QCOMPARE(playFrom(0), (QList<int>{}));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(playFrom(0), (QList<int>{60}));
}
void NotePlaybackTest::upDownArpeggioHandlesRepeatedSinglePitch() {
	oneNote();
	QDomDocument doc;
	QDomElement trackSettings = doc.createElement(QStringLiteral("instrumenttrack"));
	QDomElement arpeggio = doc.createElement(QStringLiteral("arpeggiator"));
	arpeggio.setAttribute(QStringLiteral("arp-enabled"), 1);
	arpeggio.setAttribute(QStringLiteral("arp"), 0); // The "octave" chord contains one pitch.
	arpeggio.setAttribute(QStringLiteral("arprange"), 1);
	arpeggio.setAttribute(QStringLiteral("arprepeats"), 2);
	arpeggio.setAttribute(QStringLiteral("arpdir"), static_cast<int>(InstrumentFunctionArpeggio::ArpDirection::UpAndDown));
	arpeggio.setAttribute(QStringLiteral("arptime"), 25);
	trackSettings.appendChild(arpeggio);
	m_track->loadTrackSpecificSettings(trackSettings);
	QVERIFY(m_track->isArpeggioEnabled());

	const QList<int> keys = playFrom(0, 3);
	QVERIFY2(keys.size() >= 2, qPrintable(QString::number(keys.size())));
	QVERIFY(std::all_of(keys.cbegin(), keys.cend(), [](int key) { return key == 60; }));
}
void NotePlaybackTest::upDownArpeggioWithRepeatsReachesEveryStep() {
	oneNote();
	for (const auto direction : {InstrumentFunctionArpeggio::ArpDirection::UpAndDown, InstrumentFunctionArpeggio::ArpDirection::DownAndUp})
	{
		QDomDocument doc;
		QDomElement trackSettings = doc.createElement(QStringLiteral("instrumenttrack"));
		QDomElement arpeggio = doc.createElement(QStringLiteral("arpeggiator"));
		arpeggio.setAttribute(QStringLiteral("arp-enabled"), 1);
		arpeggio.setAttribute(QStringLiteral("arp"), 0); // The "octave" chord: each step is one octave of the note.
		arpeggio.setAttribute(QStringLiteral("arprange"), 3);
		arpeggio.setAttribute(QStringLiteral("arprepeats"), 2);
		arpeggio.setAttribute(QStringLiteral("arpdir"), static_cast<int>(direction));
		arpeggio.setAttribute(QStringLiteral("arptime"), 25);
		trackSettings.appendChild(arpeggio);
		m_track->loadTrackSpecificSettings(trackSettings);
		QVERIFY(m_track->isArpeggioEnabled());

		// Render whole periods offline and record every key that sounds, so a step that never plays is seen.
		Song* song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		QList<int> sounded;
		for (int period = 0; period < 256; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			for (int key : handleKeys(m_track)) { if (!sounded.contains(key)) { sounded << key; } }
		}
		song->stop();
		for (int period = 0; period < 64 && !handleKeys(m_track).isEmpty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }

		std::sort(sounded.begin(), sounded.end());
		QStringList soundedText;
		for (int key : sounded) { soundedText << QString::number(key); }
		QVERIFY2(sounded.contains(60) && sounded.contains(72) && sounded.contains(84),
			qPrintable(QStringLiteral("direction %1 sounded keys: %2").arg(static_cast<int>(direction)).arg(soundedText.join(QLatin1Char(' ')))));
	}
}
void NotePlaybackTest::cycledArpeggioWithFractionalRepeatsDoesNotDivideByZero() {
	oneNote();
	QDomDocument doc;
	QDomElement trackSettings = doc.createElement(QStringLiteral("instrumenttrack"));
	QDomElement arpeggio = doc.createElement(QStringLiteral("arpeggiator"));
	arpeggio.setAttribute(QStringLiteral("arp-enabled"), 1);
	arpeggio.setAttribute(QStringLiteral("arp"), 0); // The "octave" chord contains one pitch.
	arpeggio.setAttribute(QStringLiteral("arprange"), 1);
	arpeggio.setAttribute(QStringLiteral("arprepeats"), QStringLiteral("1.5"));
	arpeggio.setAttribute(QStringLiteral("arpcycle"), 1);
	arpeggio.setAttribute(QStringLiteral("arpdir"), static_cast<int>(InstrumentFunctionArpeggio::ArpDirection::Up));
	arpeggio.setAttribute(QStringLiteral("arptime"), 25);
	trackSettings.appendChild(arpeggio);
	m_track->loadTrackSpecificSettings(trackSettings);
	QVERIFY(m_track->isArpeggioEnabled());

	const QList<int> keys = playFrom(0, 3);
	QVERIFY2(!keys.isEmpty(), qPrintable(QString::number(keys.size())));
	QVERIFY(std::all_of(keys.cbegin(), keys.cend(), [](int key) { return key == 60; }));
}
void NotePlaybackTest::downArpeggioWithFractionalRepeatsStaysOnTheChord() {
	oneNote();
	QDomDocument doc;
	QDomElement trackSettings = doc.createElement(QStringLiteral("instrumenttrack"));
	QDomElement arpeggio = doc.createElement(QStringLiteral("arpeggiator"));
	arpeggio.setAttribute(QStringLiteral("arp-enabled"), 1);
	arpeggio.setAttribute(QStringLiteral("arp"), 0); // The "octave" chord contains one pitch.
	arpeggio.setAttribute(QStringLiteral("arprange"), 1);
	arpeggio.setAttribute(QStringLiteral("arprepeats"), QStringLiteral("1.5"));
	arpeggio.setAttribute(QStringLiteral("arpdir"), static_cast<int>(InstrumentFunctionArpeggio::ArpDirection::Down));
	arpeggio.setAttribute(QStringLiteral("arptime"), 25);
	trackSettings.appendChild(arpeggio);
	m_track->loadTrackSpecificSettings(trackSettings);
	QVERIFY(m_track->isArpeggioEnabled());

	const QList<int> keys = playFrom(0, 3);
	QVERIFY2(!keys.isEmpty(), qPrintable(QString::number(keys.size())));
	QVERIFY(std::all_of(keys.cbegin(), keys.cend(), [](int key) { return key == 60; }));
}
void NotePlaybackTest::aRecurrenceSkipsThePassesItIsNotOn() {
	Note* note = oneNote();
	QCOMPARE(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_mask"), 2}})).errorKind, ControlErrorKind::InvalidArgs);
	// The second of every two passes: not on the first.
	QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_length"), 2}, {QStringLiteral("recur_mask"), 2}})).ok);
	QVERIFY(playFrom(0).isEmpty());
	NoteConditions conditions = m_clip->notes().front()->conditions();
	QVERIFY(!conditions.recursOnPass(0) && conditions.recursOnPass(1) && !conditions.recursOnPass(2));
	// The pass counts a loop wrap (Song::loopWraps) - not a jump back - and a stop starts it again (0.5.0 W2).
	MidiPlayback& playback = MidiPlayback::instance();
	playback.stopped();
	playback.tickPlayed(10, 5);
	QVERIFY(playback.chasingNow());
	QCOMPARE(playback.pass(), 0);  // the wraps before the start are no pass
	playback.tickPlayed(11, 5);
	QVERIFY(!playback.chasingNow());
	playback.tickPlayed(0, 5);     // a seek back
	QVERIFY(playback.chasingNow());
	QCOMPARE(playback.pass(), 0);
	playback.tickPlayed(0, 6);     // a wrap
	QCOMPARE(playback.pass(), 1);
	playback.stopped();
	playback.tickPlayed(0, 6);
	QCOMPARE(playback.pass(), 0);
}
void NotePlaybackTest::malformedRecurrenceMaskLeavesNoteOnEveryPass() {
	QDomDocument document;
	QDomElement note = document.createElement(QStringLiteral("note"));
	note.setAttribute(QStringLiteral("recl"), QStringLiteral("2"));
	note.setAttribute(QStringLiteral("recm"), QStringLiteral("broken"));

	NoteConditions conditions;
	conditions.load(note);
	QCOMPARE(conditions.recurLength, 0);
	QCOMPARE(conditions.recurMask, 0);
	QVERIFY(conditions.recursOnPass(0));
	QVERIFY(conditions.recursOnPass(1));
}
void NotePlaybackTest::aLoopWrapIsAPassAndASeekBackIsNot() {
	oneNote();
	Song* song = Engine::getSong();
	Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	timeline.setLoopPoints(TimePos(0), TimePos(48));
	timeline.setLoopEnabled(true);
	song->setPlayPos(0, Song::PlayMode::Song);
	MidiPlayback::instance().stopped();  // the test above fed it by hand: a fresh start, as after a stop
	song->playSong();
	const auto before = song->loopWraps();
	for (int period = 0; period < 2000 && song->loopWraps() == before; ++period) { Engine::audioEngine()->renderNextPeriod(); }
	QCOMPARE(song->loopWraps(), before + 1);
	QCOMPARE(MidiPlayback::instance().pass(), 1);
	for (int period = 0; period < 2000 && song->getPlayPos().getTicks() < 24; ++period) { Engine::audioEngine()->renderNextPeriod(); }
	song->setPlayPos(4, Song::PlayMode::Song);  // a seek back inside the loop
	Engine::audioEngine()->renderNextPeriod();
	QCOMPARE(song->loopWraps(), before + 1);
	QCOMPARE(MidiPlayback::instance().pass(), 1);
	song->setPlayPos(100, Song::PlayMode::Song);  // a seek past the loop: put back inside, no wrap either
	Engine::audioEngine()->renderNextPeriod();
	QVERIFY(song->getPlayPos().getTicks() < 48);
	QCOMPARE(song->loopWraps(), before + 1);
	QCOMPARE(MidiPlayback::instance().pass(), 1);
	song->stop();
	timeline.setLoopEnabled(false);
}
void NotePlaybackTest::aLoopWrapCountPassesTheThirtyTwoBitLimit() {
	oneNote();
	Song* song = Engine::getSong();
	Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	// A failed QCOMPARE returns from this function before its last lines, and a loop left on or a count left at
	// the 32-bit limit would fail the tests that run after it: the guard puts both back on every return path.
	struct RestoreLoop
	{
		Song* song;
		Timeline& timeline;
		std::uint64_t wraps;
		~RestoreLoop()
		{
			song->stop();
			timeline.setLoopEnabled(false);
			song->setLoopWrapsForTest(wraps);
		}
	} restore{song, timeline, song->loopWraps()};
	timeline.setLoopPoints(TimePos(0), TimePos(48));
	timeline.setLoopEnabled(true);
	song->setPlayPos(0, Song::PlayMode::Song);
	MidiPlayback::instance().stopped();
	const std::uint64_t limit = 0xFFFFFFFFull;
	song->setLoopWrapsForTest(limit);
	song->playSong();
	QCOMPARE(static_cast<std::uint64_t>(song->loopWraps()), limit);
	for (int period = 0; period < 2000 && song->loopWraps() == limit; ++period) { Engine::audioEngine()->renderNextPeriod(); }
	QCOMPARE(static_cast<std::uint64_t>(song->loopWraps()), limit + 1);
	QCOMPARE(MidiPlayback::instance().pass(), 1);
}
