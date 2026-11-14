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

void NotePlaybackTest::anArticulationPlaysItsKeyswitchFirst() {
	Note* note = oneNote();
	const QString track = control::trackIdOf(m_track);
	QCOMPARE(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track},
		{QStringLiteral("articulations"), QJsonArray{QJsonObject{{QStringLiteral("keyswitch"), 200}}}}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track}, {QStringLiteral("articulations"),
		QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("legato")}}, QJsonObject{{QStringLiteral("name"), QStringLiteral("staccato")},
			{QStringLiteral("keyswitch"), 25}}}}}).ok);
	QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Staccato")), 2);
	const ControlResult listed = run(QStringLiteral("track.articulations"), {{QStringLiteral("track"), track}});
	QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
	const QJsonArray map = listed.result.value(QStringLiteral("articulations")).toArray();
	QCOMPARE(map.size(), 2);  // in order: a note's 'articulation' is the 1-based index into it
	QCOMPARE(map.at(1).toObject().value(QStringLiteral("keyswitch")).toInt(), 25);
	QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("articulation"), 2}})).ok);
	// Q050-015: the keyswitch is a MIDI key sent to the instrument ahead of the note, no play handle of its own
	// (ArticulationPlaybackTest reads what the instrument is handed); the note's handle is the only one.
	QCOMPARE(playFrom(0, 1), (QList<int>{60}));
	// Saved with the track, and one undo takes the map away.
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("track"));
	m_track->articulations().save(element);
	QVERIFY(element.attribute(QStringLiteral("articulations")).contains(QStringLiteral("staccato")));
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the note's articulation
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the map
	QVERIFY(m_track->articulations().empty());
}
void NotePlaybackTest::aMalformedArticulationJsonWarnsAndLoadsWithNone() {
	oneNote();
	QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), control::trackIdOf(m_track)},
		{QStringLiteral("articulations"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("legato")}}}}}).ok);
	QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Legato")), 1);
	QDomDocument doc;
	QDomElement truncated = doc.createElement(QStringLiteral("track"));
	truncated.setAttribute(QStringLiteral("articulations"), QStringLiteral("[{\"name\":\"legato\","));
	QDomElement notAnArray = doc.createElement(QStringLiteral("track"));
	notAnArray.setAttribute(QStringLiteral("articulations"), QStringLiteral("{\"name\":\"legato\"}"));
	WarningCapture warnings;
	m_track->articulations().load(truncated);
	QVERIFY(m_track->articulations().empty());
	QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
		qPrintable(warnings.texts().join(QLatin1Char('\n'))));
	m_track->articulations().load(notAnArray);
	QVERIFY(m_track->articulations().empty());
	QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
		qPrintable(warnings.texts().join(QLatin1Char('\n'))));
}
void NotePlaybackTest::anInvalidArticulationLeavesNoPrefixLoaded() {
	oneNote();
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("track"));
	element.setAttribute(QStringLiteral("articulations"),
		QStringLiteral("[{\"name\":\"legato\"},{\"keyswitch\":25}]"));  // the second entry has no name
	WarningCapture warnings;
	m_track->articulations().load(element);
	QVERIFY(m_track->articulations().empty());  // not the valid prefix alone, which would save back as the whole map
	QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
		qPrintable(warnings.texts().join(QLatin1Char('\n'))));
}
void NotePlaybackTest::aLoopedClipPlaysItsStartAgain() {
	Note* note = oneNote(60, 24);
	m_clip->changeLength(TimePos(384));
	QVERIFY(playFrom(96).isEmpty());  // no loop: nothing starts at 96
	QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("length"), 96}}).ok);
	QCOMPARE(playFrom(96), (QList<int>{60}));  // 96 folds to 0, where the note is
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(m_clip->clipEdits().loopTicks, 0);
	(void) note;
}
void NotePlaybackTest::recurrenceSelectsThePassesOfALocallyLoopedClip() {
	// A note shorter than the 24-tick local loop leaves an observable gap after each start.
	Note* note = oneNote(60, 18);
	m_clip->changeLength(TimePos(96));
	QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
		{QStringLiteral("length"), 24}}).ok);
	QCOMPARE(playFrom(0).size(), 1);  // pass 0, tick 0: it plays before any recurrence is set
	QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_length"), 2},
		{QStringLiteral("recur_mask"), 1}})).ok);
	QCOMPARE(m_clip->notes().front()->conditions().recurLength, 2);
	// The same pair through the agent's reading of the clip (Q050-242): what the properties box
	// and the socket driven by a user both see, not only the model.
	const ControlResult read = run(QStringLiteral("roll.get_state"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)}});
	QVERIFY(read.ok);
	const QJsonObject row = read.result.value(QStringLiteral("notes")).toArray().at(0).toObject();
	QCOMPARE(row.value(QStringLiteral("recur_length")).toInt(), 2);
	QCOMPARE(row.value(QStringLiteral("recur_mask")).toInt(), 1);

	Song* song = Engine::getSong();
	Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	const bpm_t tempo = song->getTempo();
	struct Restore
	{
		Song* song;
		Timeline& timeline;
		bpm_t tempo;
		~Restore()
		{
			song->stop();
			timeline.setLoopEnabled(false);
			song->setTempo(tempo);
		}
	} restore{song, timeline, tempo};
	// The song pass is four times the clip loop: each selected pass must produce four starts,
	// while the clip's three internal loop wraps must leave MidiPlayback::pass() unchanged.
	song->setTempo(180);
	timeline.setLoopPoints(TimePos(0), TimePos(96));
	timeline.setLoopEnabled(true);

	// Which song pass sounds the note: render a good many periods and record the pass at each
	// trigger. The note ends six ticks before its local loop restarts, so count distinct STARTS.
	const auto soundsOnPasses = [&](int periods) {
		QList<int> passes;
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		int live = 0;
		for (int period = 0; period < periods; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			const QList<int> keys = handleKeys(m_track);
			// A handle appearing is a start; the count of handles is 0 or 1 with one note.
			if (keys.size() > live) { passes << MidiPlayback::instance().pass(); }
			live = keys.size();
		}
		song->stop();
		for (int period = 0; period < 64 && !handleKeys(m_track).isEmpty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }
		return passes;
	};

	// The first-of-two mask sounds only on even song passes. Each selected pass has four starts
	// (the clip's local repeats); an odd song pass has none. This distinguishes the song-pass
	// mask from incorrectly advancing it at each local clip-loop wrap.
	const QList<int> passes = soundsOnPasses(6000);
	QVERIFY2(passes.size() >= 12, qPrintable(QStringLiteral("only %1 local starts across passes 0, 2 and 4").arg(passes.size())));
	for (int pass : passes) { QCOMPARE(pass % 2, 0); }
	QCOMPARE(passes.count(0), 4);
	QCOMPARE(passes.count(2), 4);
	QCOMPARE(passes.count(4), 4);

	// Mask 0 with a positive length means NO pass, opposite of length 0 (every pass).
	QVERIFY(run(QStringLiteral("note.edit"), edit(m_clip->notes().front(), {{QStringLiteral("recur_length"), 2},
		{QStringLiteral("recur_mask"), 0}})).ok);
	QVERIFY2(soundsOnPasses(2000).isEmpty(), "a positive length with mask 0 plays on no pass");
	QVERIFY(run(QStringLiteral("note.edit"), edit(m_clip->notes().front(), {{QStringLiteral("recur_length"), 0},
		{QStringLiteral("recur_mask"), 0}})).ok);
	QVERIFY2(!soundsOnPasses(600).isEmpty(), "length 0 plays on every pass, the neutral pair");
}
void NotePlaybackTest::aRecurrencePairRoundTripsThroughTheProjectFile() {
	Note* note = oneNote(60, 24);
	m_clip->addNote(Note(TimePos(24), TimePos(48), 64), false);
	QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_length"), 4},
		{QStringLiteral("recur_mask"), 5}})).ok);

	// The clip's own element carries the optional pair; a note with none writes neither attribute.
	QDomDocument document;
	QDomElement element = document.createElement(QStringLiteral("midiclip"));
	m_clip->saveState(document, element);
	document.appendChild(element);  // a detached element has no serialization of its own
	const QString saved = document.toString();
	QVERIFY2(saved.contains(QStringLiteral("recl=\"4\"")), qPrintable(saved.left(4000)));
	QVERIFY2(saved.contains(QStringLiteral("recm=\"5\"")), qPrintable(saved.left(4000)));
	QCOMPARE(saved.count(QStringLiteral("recl=")), 1);

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString project = QDir(dir.path()).filePath(QStringLiteral("recurrence.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(project));
	QVERIFY(run(QStringLiteral("project.new")).ok);
	Engine::getSong()->loadProject(project);
	MidiClip* reloaded = nullptr;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
		{
			if (instrument->numOfClips() > 0) { reloaded = dynamic_cast<MidiClip*>(instrument->getClip(0)); }
		}
	}
	QVERIFY(reloaded != nullptr);
	QCOMPARE(static_cast<int>(reloaded->notes().size()), 2);
	const Note* a = reloaded->notes().front();
	QCOMPARE(a->conditions().recurLength, 4);
	QCOMPARE(a->conditions().recurMask, 5);
	QVERIFY(a->conditions().recursOnPass(0) && !a->conditions().recursOnPass(1)
		&& a->conditions().recursOnPass(2) && !a->conditions().recursOnPass(3));
	// The neutral note wrote no recurrence at all: an old project still saves byte-identically.
	const Note* b = reloaded->notes().at(1);
	QCOMPARE(b->conditions().recurLength, 0);
	QCOMPARE(b->conditions().recurMask, 0);
	QDomDocument reread;
	QDomElement again = reread.createElement(QStringLiteral("midiclip"));
	reloaded->saveState(reread, again);
	reread.appendChild(again);
	QCOMPARE(reread.toString().count(QStringLiteral("recl=")), 1);
	// ...and the reloaded pair is what the agent surface reads back (Q050-242: the readback the
	// properties box and a socket client both use, not a private model path).
	const ControlResult state = run(QStringLiteral("roll.get_state"),
		{{QStringLiteral("clip"), control::clipIdOf(reloaded)}});
	QVERIFY(state.ok);
	const QJsonArray rows = state.result.value(QStringLiteral("notes")).toArray();
	QCOMPARE(rows.size(), 2);
	QCOMPARE(rows.at(0).toObject().value(QStringLiteral("recur_length")).toInt(), 4);
	QCOMPARE(rows.at(0).toObject().value(QStringLiteral("recur_mask")).toInt(), 5);
	QCOMPARE(rows.at(1).toObject().value(QStringLiteral("recur_length")).toInt(), 0);
	QCOMPARE(rows.at(1).toObject().value(QStringLiteral("recur_mask")).toInt(), 0);
}
void NotePlaybackTest::chaseSoundsANotePlaybackStartsInside() {
	oneNote();
	QVERIFY(playFrom(96).isEmpty());  // off by default: playback as it always was
	QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
	QCOMPARE(playFrom(96), (QList<int>{60}));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(!MidiPlayback::instance().chase());
}
void NotePlaybackTest::swingDelaysTheOffBeatsOnly() {
	MidiPlayback& playback = MidiPlayback::instance();
	QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{0});
	QVERIFY(run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.5}}).ok);
	QCOMPARE(playback.delayFrames(0, 100.f, 48000), f_cnt_t{0});
	QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{600});  // half of a 12-tick step
	QCOMPARE(playback.delayFrames(24, 100.f, 48000), f_cnt_t{0});
	QVERIFY(playback.shouldPersist());
	QVERIFY(run(QStringLiteral("transport.groove"), {{QStringLiteral("grid"), QStringLiteral("8th")}}).ok);
	QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{0});
	QCOMPARE(playback.delayFrames(24, 100.f, 48000), f_cnt_t{1200});
	QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("offset_ms"), 10}}).ok);
	QCOMPARE(playback.delayFrames(0, 100.f, 48000), f_cnt_t{480});
	for (int i = 0; i < 3; ++i) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
	QCOMPARE(playback.swing(), 0.f);
	QCOMPARE(playback.offsetMs(), 0);
	QVERIFY(!playback.shouldPersist());
	QCOMPARE(run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.9}}).errorKind, ControlErrorKind::InvalidArgs);
}
void NotePlaybackTest::perNoteLyricsRequireText() {
	Note* note = oneNote();
	const QString clip = control::clipIdOf(m_clip);
	QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("original")}}).ok);
	const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
		{QStringLiteral("lyrics"), QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}}}}});
	QCOMPARE(lyricText(note->conditions().lyric), QStringLiteral("original"));
	QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
}
void NotePlaybackTest::perNoteLyricsRequireStringText() {
	Note* note = oneNote();
	const QString clip = control::clipIdOf(m_clip);
	QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("original")}}).ok);
	const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
		{QStringLiteral("lyrics"), QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)}, {QStringLiteral("text"), 42}}}}});
	QCOMPARE(lyricText(note->conditions().lyric), QStringLiteral("original"));
	QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
}
void NotePlaybackTest::lyricsRejectBothInputFormsTogether() {
	Note* note = oneNote();
	const QString clip = control::clipIdOf(m_clip);
	QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("original")}}).ok);
	ProjectJournal* journal = Engine::projectJournal();
	QVERIFY(journal != nullptr);
	const int depthBefore = journal->undoDepth();
	const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
		{QStringLiteral("text"), QStringLiteral("ignored")},
		{QStringLiteral("lyrics"), QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(note)},
			{QStringLiteral("text"), QStringLiteral("per note")}}}}});
	QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(lyricText(note->conditions().lyric), QStringLiteral("original"));
	QCOMPARE(journal->undoDepth(), depthBefore);
}
void NotePlaybackTest::lyricsReportEveryNoteTheyWrite() {
	oneNote();
	for (int i = 1; i < 3; ++i) { m_clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i), false); }
	const QString clip = control::clipIdOf(m_clip);
	QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("a b c")}}).ok);
	const ControlResult result = run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip},
		{QStringLiteral("text"), QStringLiteral("solo")}});
	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	QCOMPARE(result.result.value(QStringLiteral("set")).toInt(), 3);
	QCOMPARE(result.result.value(QStringLiteral("syllables")).toInt(), 1);
	QStringList sung;
	for (const Note* note : m_clip->notes()) { sung << lyricText(note->conditions().lyric); }
	QCOMPARE(sung, (QStringList{QStringLiteral("solo"), QString(), QString()}));
}
void NotePlaybackTest::nonFiniteSwingLoadsBounded() {
	MidiPlayback& playback = MidiPlayback::instance();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	playback.setSwing(nan, 2 * MidiPlayback::DefaultSwingGrid);
	QVERIFY(std::isfinite(playback.swing()));
	QVERIFY(playback.swing() >= 0.f);
	QVERIFY(playback.swing() <= MidiPlayback::MaxSwing);
	playback.setSwing(std::numeric_limits<float>::infinity(), MidiPlayback::DefaultSwingGrid);
	QVERIFY(std::isfinite(playback.swing()));
	QVERIFY(playback.swing() <= MidiPlayback::MaxSwing);
	playback.setSwing(std::numeric_limits<float>::lowest(), MidiPlayback::DefaultSwingGrid);
	QVERIFY(std::isfinite(playback.swing()));
	QVERIFY(playback.swing() >= 0.f);
	// The delayFrames() result must stay a real frame count throughout.
	QVERIFY(playback.delayFrames(12, 100.f, 48000) >= 0);
	playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
	QCOMPARE(playback.swing(), 0.f);
}
void NotePlaybackTest::delayFramesStaysAFrameCount() {
	MidiPlayback& playback = MidiPlayback::instance();
	playback.setSwing(0.75f, MidiPlayback::DefaultSwingGrid);
	const float badTicks[] = {-1.f, -100.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
	for (const float framesPerTick : badTicks)
	{
		QCOMPARE(playback.delayFrames(12, framesPerTick, 44100), f_cnt_t{0});  // an off-beat, no swing
	}
	QCOMPARE(playback.delayFrames(12, 100.f, 44100), f_cnt_t{900});  // 0.75 of a 12-tick step at 100 frames a tick
	// The MIDI offset is not swing: it still delays a note when the tick length is unusable.
	QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("offset_ms"), 10}}).ok);
	QCOMPARE(playback.delayFrames(12, -1.f, 44100), f_cnt_t{441});
	QCOMPARE(playback.delayFrames(0, -1.f, 44100), f_cnt_t{441});
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(playback.offsetMs(), 0);
	playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
}
void NotePlaybackTest::swingRejectsValuesJustAboveMaximum() {
	MidiPlayback& playback = MidiPlayback::instance();
	playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
	const ControlResult result = run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.75000001}});
	if (result.ok) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
	QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(playback.swing(), 0.f);
	QCOMPARE(playback.swingGrid(), MidiPlayback::DefaultSwingGrid);
}
void NotePlaybackTest::swingRejectsTinyNegativeValues() {
	MidiPlayback& playback = MidiPlayback::instance();
	playback.setSwing(0.f, MidiPlayback::DefaultSwingGrid);
	const ControlResult result = run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), -1e-50}});
	QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(playback.swing(), 0.f);
	QCOMPARE(playback.swingGrid(), MidiPlayback::DefaultSwingGrid);
}
void NotePlaybackTest::clipControllerUsesTheTrackOutputChannel() {
	oneNote();
	m_clip->setControllerPoints(ControllerKind::ControlChange, 7, {{0, 64}});
	MidiPort* port = m_track->midiPort();
	port->setOutputChannel(6);
	port->setMode(MidiPort::Mode::Output);
	const quint64 written = port->outputEventsWritten();

	m_track->playClipControllers(m_clip, 0, 0);

	QCOMPARE(port->outputEventsWritten(), written + 1);
}
void NotePlaybackTest::lyricsAndGroups() {
	oneNote();
	for (int i = 1; i < 5; ++i) { m_clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i), false); }
	const QString clip = control::clipIdOf(m_clip);
	QCOMPARE(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("Hal-le-lu-jah sing")}}).ok);
	QStringList sung;
	for (const Note* note : m_clip->notes()) { sung << lyricText(note->conditions().lyric); }
	QCOMPARE(sung, (QStringList{QStringLiteral("Hal-"), QStringLiteral("le-"), QStringLiteral("lu-"), QStringLiteral("jah"), QStringLiteral("sing")}));
	for (Note* note : m_clip->notes()) { note->setSelected(note->key() >= 62); }
	const ControlResult grouped = run(QStringLiteral("note.group"), {{QStringLiteral("clip"), clip}});
	QVERIFY2(grouped.ok, qPrintable(grouped.errorMessage));
	QCOMPARE(grouped.result.value(QStringLiteral("notes")).toInt(), 3);
	Note* last = m_clip->notes().back();
	QCOMPARE(last->conditions().group, 1);
	// Saved only when set, and read back whole.
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("root"));
	const QDomElement saved = last->saveState(doc, root);
	QCOMPARE(saved.attribute(QStringLiteral("lyr")), QStringLiteral("sing"));
	Note restored;
	restored.restoreState(saved);
	QVERIFY(restored.conditions() == last->conditions());
	QVERIFY(!m_clip->notes().front()->saveState(doc, root).hasAttribute(QStringLiteral("grp")));
	QVERIFY(run(QStringLiteral("note.ungroup"), {{QStringLiteral("clip"), clip}}).ok);
	QCOMPARE(m_clip->notes().back()->conditions().group, 0);
	QCOMPARE(run(QStringLiteral("note.group"), {{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), QStringLiteral("clip")}}).result
		.value(QStringLiteral("group")).toInt(), 1);
}
void NotePlaybackTest::emptyPerNoteLyricsDoNotCreateAnUndoStep() {
	oneNote();
	const QString clip = control::clipIdOf(m_clip);
	ProjectJournal* journal = Engine::projectJournal();
	QVERIFY(journal != nullptr);
	const int depthBefore = journal->undoDepth();

	const ControlResult result = run(QStringLiteral("note.lyrics"),
		{{QStringLiteral("clip"), clip}, {QStringLiteral("lyrics"), QJsonArray{}}});
	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	QCOMPARE(result.result.value(QStringLiteral("set")).toInt(), 0);
	QCOMPARE(journal->undoDepth(), depthBefore);
}
