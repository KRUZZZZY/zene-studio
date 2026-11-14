/*
 * ArrangeToolsTest.cpp - the clip list, sections, remove silence, tab to transient, note rotation
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

void ArrangeToolsTest::initTestCase() {
	Engine::init(true);
	ControlRegistry::setReady(true);
}
void ArrangeToolsTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void ArrangeToolsTest::theClipListFindsByName() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	Clip* verse = track->createClip(TimePos(0));
	verse->setName(QStringLiteral("Verse riff"));
	track->createClip(TimePos(192))->setName(QStringLiteral("Chorus"));
	const QJsonArray found = run(QStringLiteral("clip.list"), {{QStringLiteral("name"), QStringLiteral("riff")}}).result.value(QStringLiteral("clips")).toArray();
	QCOMPARE(found.size(), 1);
	QCOMPARE(found.first().toObject().value(QStringLiteral("clip")).toString(), control::clipIdOf(verse));
	QCOMPARE(run(QStringLiteral("clip.list"), {{QStringLiteral("track"), control::trackIdOf(track)}}).result.value(QStringLiteral("count")).toInt(), 2);
}
void ArrangeToolsTest::aTrackSwitchesBetweenVersions() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	track->createClip(TimePos(0))->setName(QStringLiteral("old bass"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	track->deleteClips();
	track->createClip(TimePos(192))->setName(QStringLiteral("new bass"));
	track->createClip(TimePos(384));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	QCOMPARE(track->getClips().size(), std::size_t{2});
	QCOMPARE(track->getClips().front()->name(), QStringLiteral("new bass"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // back on B, both clips
	QCOMPARE(track->getClips().size(), std::size_t{2});
	QCOMPARE(TrackVersions::instance().of(track->id()).active, QStringLiteral("B"));
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("root"));
	TrackVersions::instance().save(doc, root);
	QCOMPARE(root.firstChildElement().firstChildElement().elementsByTagName(QStringLiteral("version")).size(), 2);
	QCOMPARE(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("C")}}).errorKind,
		ControlErrorKind::NotFound);
	QCOMPARE(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral(" ")}}).errorKind,
		ControlErrorKind::InvalidArgs);
}
void ArrangeToolsTest::switchingTrackVersionTrimsTheName() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	track->createClip(TimePos(0))->setName(QStringLiteral("old bass"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	track->deleteClips();
	track->createClip(TimePos(192))->setName(QStringLiteral("new bass"));

	const ControlResult switched = run(QStringLiteral("track.version_switch"),
		{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A ")}});
	QVERIFY2(switched.ok, qPrintable(switched.errorMessage));
	QCOMPARE(TrackVersions::instance().of(track->id()).active, QStringLiteral("A"));
	QCOMPARE(track->getClips().size(), std::size_t{1});
	QCOMPARE(track->getClips().front()->name(), QStringLiteral("old bass"));

	const TrackVersions::Set state = TrackVersions::instance().of(track->id());
	const QString currentClips = TrackVersions::clipsOf(track);
	QCOMPARE(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("missing ")}}).errorKind,
		ControlErrorKind::NotFound);
	QCOMPARE(TrackVersions::instance().of(track->id()).active, state.active);
	QCOMPARE(TrackVersions::clipsOf(track), currentClips);
}
void ArrangeToolsTest::replacingTrackVersionKeepsModelGuardThroughClipRestore() {
	// The audio thread takes the same change mutex the guard takes, so the
	// device is stopped for the probe: the only holder of the mutex during
	// the replacement is then the guard itself.
	Engine::audioEngine()->audioDev()->stopProcessing();
	const auto restartDevice = qScopeGuard([] { Engine::audioEngine()->audioDev()->startProcessing(); });

	// Control: with no guard in sight the mutex is free, so a probe that
	// finds it held really means the replacement ran inside a guard.
	QVERIFY2(!changeMutexIsHeld(), "the change mutex was held outside the replacement");

	auto track = std::make_unique<GuardProbeSampleTrack>(Engine::getSong(), [this] { return changeMutexIsHeld(); });
	QVERIFY(TrackVersions::replaceClips(track.get(), QStringLiteral("<clips><sampleclip/></clips>")));

	QVERIFY2(track->createRanInsideGuard(),
		"the track version replaced its clips outside the audio guard: the change mutex was free while the replacement clip was created");
	QVERIFY2(track->restoreRanInsideGuard(),
		"the track version replaced its clips outside the audio guard: the change mutex was free while the replacement clip was restored");
	QCOMPARE(track->numOfClips(), 1);
	QVERIFY2(!changeMutexIsHeld(), "the audio change mutex remained held after track-version replacement");
}
void ArrangeToolsTest::undoAndRedoOfTrackVersionSwitchKeepModelGuardThroughClipRestore() {
	Engine::audioEngine()->audioDev()->stopProcessing();
	const auto restartDevice = qScopeGuard([] { Engine::audioEngine()->audioDev()->startProcessing(); });
	QVERIFY2(!changeMutexIsHeld(), "the change mutex was held before the version switch");

	auto track = std::make_unique<GuardProbeSampleTrack>(Engine::getSong(), [this] { return changeMutexIsHeld(); });
	const QString id = control::trackIdOf(track.get());
	track->createClip(TimePos(0))->setName(QStringLiteral("version A"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	track->deleteClips();
	track->createClip(TimePos(192))->setName(QStringLiteral("version B"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);

	track->resetGuardObservations();
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY2(track->createRanInsideGuard(),
		"undo restored the track-version clips outside the audio guard while creating a clip");
	QVERIFY2(track->restoreRanInsideGuard(),
		"undo restored the track-version clips outside the audio guard while restoring a clip");
	QCOMPARE(TrackVersions::instance().of(track->id()).active, QStringLiteral("B"));

	track->resetGuardObservations();
	QVERIFY(run(QStringLiteral("control.redo")).ok);
	QVERIFY2(track->createRanInsideGuard(),
		"redo restored the track-version clips outside the audio guard while creating a clip");
	QVERIFY2(track->restoreRanInsideGuard(),
		"redo restored the track-version clips outside the audio guard while restoring a clip");
	QCOMPARE(TrackVersions::instance().of(track->id()).active, QStringLiteral("A"));
}
void ArrangeToolsTest::savingAnotherVersionPreservesEditsToTheActiveVersion() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	track->createClip(TimePos(0))->setName(QStringLiteral("original A"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);

	track->deleteClips();
	track->createClip(TimePos(192))->setName(QStringLiteral("edited A"));
	track->createClip(TimePos(384))->setName(QStringLiteral("second edit"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_switch"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);

	QCOMPARE(track->getClips().size(), std::size_t{2});
	QCOMPARE(track->getClips().front()->name(), QStringLiteral("edited A"));
	QCOMPARE(track->getClips().back()->name(), QStringLiteral("second edit"));
}
void ArrangeToolsTest::aVersionIsListedAndForgotten() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	track->createClip(TimePos(0))->setName(QStringLiteral("take"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	const ControlResult listed = run(QStringLiteral("track.version_list"), {{QStringLiteral("track"), id}});
	QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
	QCOMPARE(listed.result.value(QStringLiteral("active")).toString(), QStringLiteral("B"));
	QCOMPARE(listed.result.value(QStringLiteral("versions")).toArray(), (QJsonArray{QStringLiteral("A"), QStringLiteral("B")}));
	const ControlResult removed = run(QStringLiteral("track.version_remove"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}});
	QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
	QCOMPARE(removed.result.value(QStringLiteral("versions")).toArray(), QJsonArray{QStringLiteral("B")});
	QVERIFY(!TrackVersions::instance().of(track->id()).clips.contains(QStringLiteral("A")));
	QCOMPARE(track->getClips().size(), std::size_t{1});  // only the stored copy goes
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(TrackVersions::instance().of(track->id()).clips.contains(QStringLiteral("A")));
	QCOMPARE(run(QStringLiteral("track.version_remove"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("C")}}).errorKind,
		ControlErrorKind::NotFound);
}
void ArrangeToolsTest::removingTrackVersionTrimsTheName() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	track->createClip(TimePos(0))->setName(QStringLiteral("current"));
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("A")}}).ok);
	QVERIFY(run(QStringLiteral("track.version_save"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B")}}).ok);
	const QString clipsBefore = TrackVersions::clipsOf(track);

	const ControlResult removed = run(QStringLiteral("track.version_remove"),
		{{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("B ")}});
	QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
	QVERIFY(!TrackVersions::instance().of(track->id()).clips.contains(QStringLiteral("B")));
	QVERIFY(TrackVersions::instance().of(track->id()).clips.contains(QStringLiteral("A")));
	QVERIFY(TrackVersions::instance().of(track->id()).active.isEmpty());
	QCOMPARE(TrackVersions::clipsOf(track), clipsBefore);

	const TrackVersions::Set state = TrackVersions::instance().of(track->id());
	QCOMPARE(run(QStringLiteral("track.version_remove"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("  ")}}).errorKind,
		ControlErrorKind::NotFound);
	QCOMPARE(TrackVersions::instance().of(track->id()).clips, state.clips);
	QCOMPARE(TrackVersions::clipsOf(track), clipsBefore);
}
void ArrangeToolsTest::sectionsAreSpansWithUndo() {
	QVERIFY(!ArrangementSections::instance().shouldPersist());
	const ControlResult added = run(QStringLiteral("section.add"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 768},
		{QStringLiteral("name"), QStringLiteral("Verse")}});
	QVERIFY2(added.ok, qPrintable(added.errorMessage));
	const QString id = added.result.value(QStringLiteral("section")).toString();
	QVERIFY(run(QStringLiteral("section.set"), {{QStringLiteral("section"), id}, {QStringLiteral("name"), QStringLiteral("Verse 1")}}).ok);
	QCOMPARE(ArrangementSections::instance().at(100)->name, QStringLiteral("Verse 1"));
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("song"));
	ArrangementSections::instance().save(doc, root);
	QCOMPARE(root.firstChildElement().firstChildElement().attribute(QStringLiteral("name")), QStringLiteral("Verse 1"));
	QVERIFY(run(QStringLiteral("section.select"), {{QStringLiteral("section"), id}}).ok);
	const ControlResult removed = run(QStringLiteral("section.remove"), {{QStringLiteral("section"), id}});
	QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
	QCOMPARE(removed.result.value(QStringLiteral("removed")).toString(), id);
	QVERIFY(ArrangementSections::instance().sections().isEmpty());
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the remove
	QCOMPARE(ArrangementSections::instance().at(100)->name, QStringLiteral("Verse 1"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the rename
	QCOMPARE(ArrangementSections::instance().at(100)->name, QStringLiteral("Verse"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the add
	QVERIFY(ArrangementSections::instance().sections().isEmpty());
	QCOMPARE(run(QStringLiteral("section.add"), {{QStringLiteral("start"), 100}, {QStringLiteral("end"), 50}}).errorKind, ControlErrorKind::InvalidArgs);
}
void ArrangeToolsTest::aSectionReplacementRefusesAnInvalidSpan() {
	ArrangementSections& sections = ArrangementSections::instance();
	const int id = sections.add(0, 768, QStringLiteral("Verse"), QString());
	QVERIFY(id > 0);
	ArrangementSection reversed{id, 100, 50, QStringLiteral("Reversed"), QString()};
	QVERIFY(!sections.replace(id, reversed));   // end before start
	QCOMPARE(sections.find(id)->end, tick_t{768});
	ArrangementSection empty{id, 300, 300, QStringLiteral("Empty"), QString()};
	QVERIFY(!sections.replace(id, empty));     // no span at all
	QCOMPARE(sections.find(id)->end, tick_t{768});
	ArrangementSection negative{id, -100, 200, QStringLiteral("Negative"), QString()};
	QVERIFY(!sections.replace(id, negative));   // starting before the song
	QCOMPARE(sections.find(id)->start, tick_t{0});
	ArrangementSection good{id, 192, 960, QStringLiteral("Chorus"), QString()};
	QVERIFY(sections.replace(id, good));       // a real span still replaces
	QCOMPARE(sections.find(id)->name, QStringLiteral("Chorus"));
	QCOMPARE(sections.find(id)->id, id);       // and keeps the id it was found by
	sections.clear();
}
void ArrangeToolsTest::aNegativeSectionSpanIsRefusedOverTheCommands() {
	ArrangementSections& sections = ArrangementSections::instance();
	QCOMPARE(run(QStringLiteral("section.add"), {{QStringLiteral("start"), -5}, {QStringLiteral("end"), -1}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(sections.sections().isEmpty());
	const int id = sections.add(0, 768, QStringLiteral("Verse"), QString());
	QVERIFY(id > 0);
	const QString wireId = QStringLiteral("sec-%1").arg(id);
	QCOMPARE(run(QStringLiteral("section.set"), {{QStringLiteral("section"), wireId}, {QStringLiteral("start"), -5}}).errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(run(QStringLiteral("section.set"), {{QStringLiteral("section"), wireId}, {QStringLiteral("end"), -1}}).errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(sections.find(id)->start, tick_t{0});
	QCOMPARE(sections.find(id)->end, tick_t{768});
	sections.clear();
}
void ArrangeToolsTest::aMalformedSectionSpanIsNotReadAsTickZero() {
	ArrangementSections& sections = ArrangementSections::instance();
	QDomDocument doc;
	QDomElement list = doc.createElement(QStringLiteral("sections"));
	QDomElement bad = doc.createElement(QStringLiteral("section"));
	bad.setAttribute(QStringLiteral("id"), 1);
	bad.setAttribute(QStringLiteral("start"), QStringLiteral("oops"));
	bad.setAttribute(QStringLiteral("end"), 192);
	list.appendChild(bad);
	QDomElement noStart = doc.createElement(QStringLiteral("section"));
	noStart.setAttribute(QStringLiteral("id"), 2);
	noStart.setAttribute(QStringLiteral("end"), 192);
	list.appendChild(noStart);
	QDomElement noEnd = doc.createElement(QStringLiteral("section"));
	noEnd.setAttribute(QStringLiteral("id"), 3);
	noEnd.setAttribute(QStringLiteral("start"), 0);
	list.appendChild(noEnd);
	QDomElement good = doc.createElement(QStringLiteral("section"));
	good.setAttribute(QStringLiteral("id"), 4);
	good.setAttribute(QStringLiteral("start"), 96);
	good.setAttribute(QStringLiteral("end"), 288);
	good.setAttribute(QStringLiteral("name"), QStringLiteral("Chorus"));
	list.appendChild(good);
	sections.load(list);
	QCOMPARE(sections.sections().size(), 1);        // only the one that reads as a real span
	QVERIFY(sections.at(96) != nullptr);
	QCOMPARE(sections.at(96)->name, QStringLiteral("Chorus"));
	QCOMPARE(sections.at(96)->start, tick_t{96});    // and it keeps the start it was written with
	sections.clear();
}
void ArrangeToolsTest::aQuietStretchIsCutOut() {
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	SampleClip* clip = loudQuietLoud(track);
	const ControlResult cut = run(QStringLiteral("clip.remove_silence"), {{QStringLiteral("clip"), control::clipIdOf(clip)}});
	QVERIFY2(cut.ok, qPrintable(cut.errorMessage));
	QCOMPARE(cut.result.value(QStringLiteral("removed")).toInt(), 1);
	QCOMPARE(track->getClips().size(), std::size_t{2});  // the loud parts either side
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(track->getClips().size(), std::size_t{1});
}
void ArrangeToolsTest::aQuietStretchIsMeasuredInTheClipsOwnRate() {
	const int engineRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	const auto engineFramesPerTick = static_cast<int>(Engine::framesPerTick());
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	// 72 engine-frames-per-tick of silence is 36 ticks at the sample's own rate: under the default
	// min_length of 48 ticks, so nothing qualifies. The engine scalar (48 ticks) would have cut it.
	SampleClip* clip = loudQuietLoudAtRate(track, engineRate * 2, engineFramesPerTick * 96, engineFramesPerTick * 72);
	QCOMPARE(run(QStringLiteral("clip.remove_silence"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).errorKind,
		ControlErrorKind::NotFound);
	QCOMPARE(track->getClips().size(), std::size_t{1});  // untouched
}
void ArrangeToolsTest::aQuietStretchShorterThanOneTickIsNotRemoved() {
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	constexpr int sampleRate = 44101;
	const float framesPerTick = Engine::framesPerTick(sampleRate);
	const int quietFrames = static_cast<int>(framesPerTick);
	QVERIFY(framesPerTick > static_cast<float>(quietFrames));  // this sample rate makes one tick fractional
	SampleClip* clip = loudQuietLoudAtRate(track, sampleRate, 8, quietFrames);
	const ControlResult result = run(QStringLiteral("clip.remove_silence"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("min_length"), 1}});
	QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
	QCOMPARE(track->getClips().size(), std::size_t{1});  // the clip was not cut
}
void ArrangeToolsTest::aClipListRefusesATrackThatIsNotThere() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	track->createClip(TimePos(0));
	QCOMPARE(run(QStringLiteral("clip.list"), {{QStringLiteral("track"), QStringLiteral("trk-999999")}}).errorKind,
		ControlErrorKind::NotFound);
	QCOMPARE(run(QStringLiteral("clip.list"), {{QStringLiteral("track"), control::trackIdOf(track)}}).result.value(QStringLiteral("count")).toInt(), 1);
}
void ArrangeToolsTest::noTransientWithoutAudio() {
	Engine::getSong()->setPlayPos(5000);  // the current mode's play head, the one the command reads
	QCOMPARE(run(QStringLiteral("arrangement.next_transient")).errorKind, ControlErrorKind::NotFound);
}
void ArrangeToolsTest::tabMovesToTheNextTransient() {
	test::sampleClipOf(test::burstAudio(200, {10, 58, 106, 154}));  // a new song: one clip of four bursts
	Engine::getSong()->setPlayPos(30);
	const ControlResult next = run(QStringLiteral("arrangement.next_transient"));
	QVERIFY2(next.ok, qPrintable(next.errorMessage));
	const int at = next.result.value(QStringLiteral("position_ticks")).toInt();
	QVERIFY2(std::abs(at - 58) <= 6, qPrintable(QStringLiteral("next transient at %1, burst at 58").arg(at)));  // the detector's window
	QCOMPARE(static_cast<int>(Engine::getSong()->getPlayPos().getTicks()), at);
	const ControlResult back = run(QStringLiteral("arrangement.next_transient"), {{QStringLiteral("direction"), QStringLiteral("previous")}});
	QVERIFY2(back.ok, qPrintable(back.errorMessage));
	const int before = back.result.value(QStringLiteral("position_ticks")).toInt();
	QVERIFY2(std::abs(before - 10) <= 6, qPrintable(QStringLiteral("previous transient at %1, burst at 10").arg(before)));
	QCOMPARE(static_cast<int>(Engine::getSong()->getPlayPos().getTicks()), before);
}
void ArrangeToolsTest::nextTransientDoesNotLeaveTheVisibleClip() {
	SampleClip* clip = test::sampleClipOf(test::burstAudio(200, {10, 58, 106, 154}));
	clip->setStartTimeOffset(TimePos(100));  // its later source bursts now map past the 200-tick clip end
	Engine::getSong()->setPlayPos(170);      // still inside the visible clip
	const ControlResult next = run(QStringLiteral("arrangement.next_transient"));
	QCOMPARE(next.errorKind, ControlErrorKind::NotFound);
	QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), tick_t{170});  // no seek outside the clip
}
void ArrangeToolsTest::aClipBecomesAScore() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(48), TimePos(0), 60), false);    // C4, a quarter
	clip->addNote(Note(TimePos(48), TimePos(0), 64), false);    // E4 with it: a chord
	clip->addNote(Note(TimePos(240), TimePos(96), 69), false);  // A4 from beat 3, across the bar line
	const QString xml = musicXmlOf(*clip, 4, 4, QStringLiteral("Test"));
	QVERIFY(xml.contains(QStringLiteral("<step>C</step>")));
	QVERIFY(xml.contains(QStringLiteral("<octave>4</octave>")));
	QVERIFY(xml.contains(QStringLiteral("<chord/>")));
	QVERIFY(xml.contains(QStringLiteral("<tie type=\"start\"/>")));  // the A is tied into bar 2
	QCOMPARE(xml.count(QStringLiteral("<measure ")), 2);
	QVERIFY(!xml.contains(QStringLiteral("<backup>")));  // one length per start: one voice, as before W6
	QCOMPARE(run(QStringLiteral("clip.export_musicxml"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("path"), QStringLiteral("/tmp/x.mid")}}).errorKind,
		ControlErrorKind::InvalidArgs);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("score.musicxml"));
	const ControlResult written = run(QStringLiteral("clip.export_musicxml"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("path"), path}});
	QVERIFY2(written.ok, qPrintable(written.errorMessage));
	QCOMPARE(written.result.value(QStringLiteral("notes")).toInt(), 3);
	QCOMPARE(written.result.value(QStringLiteral("path")).toString(), path);
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	const QString onDisk = QString::fromUtf8(file.readAll());
	QVERIFY(onDisk.contains(QStringLiteral("<chord/>")));
	QCOMPARE(onDisk.count(QStringLiteral("<measure ")), 2);  // the same score the function above builds, in 4/4
}
void ArrangeToolsTest::theScoreSuffixIsCaseInsensitive() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	for (const QString& name : {QStringLiteral("score.MUSICXML"), QStringLiteral("score.XML"),
			QStringLiteral("score.MusicXml")})
	{
		const QString path = dir.filePath(name);
		const ControlResult written = run(QStringLiteral("clip.export_musicxml"),
			{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("path"), path}});
		QVERIFY2(written.ok, qPrintable(QStringLiteral("%1: %2").arg(name, written.errorMessage)));
		QCOMPARE(written.result.value(QStringLiteral("path")).toString(), path);
		QVERIFY2(QFileInfo::exists(path), qPrintable(name));
	}
	// And a suffix that is not an XML one at all is still refused.
	QCOMPARE(run(QStringLiteral("clip.export_musicxml"),
			{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("path"), dir.filePath(QStringLiteral("score.mid"))}}).errorKind,
		ControlErrorKind::InvalidArgs);
}
void ArrangeToolsTest::aChordOfMixedLengthsBecomesVoices() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(48), TimePos(0), 64), false);   // E4, a quarter
	clip->addNote(Note(TimePos(192), TimePos(0), 48), false);  // C3 under it, held for the whole bar
	clip->addNote(Note(TimePos(48), TimePos(48), 67), false);  // G4 on beat 2, while the C3 still sounds
	const QString xml = musicXmlOf(*clip, 4, 4, QStringLiteral("Voices"));
	QCOMPARE(xml.count(QStringLiteral("<measure ")), 1);
	QCOMPARE(xml.count(QStringLiteral("<backup>")), 1);
	QVERIFY(!xml.contains(QStringLiteral("<chord/>")));  // two lengths are two voices, not one chord
	// The held note keeps its whole bar, untied, in voice 2.
	static const QRegularExpression held(QStringLiteral("<step>C</step>\\s*<octave>3</octave>\\s*</pitch>\\s*"
		"<duration>192</duration>\\s*<voice>2</voice>"));
	QVERIFY2(held.match(xml).hasMatch(), qPrintable(xml));
	QVERIFY(!xml.contains(QStringLiteral("<tie ")));
	// Voice 1 carries the melody: E4 and G4, a quarter each, then a half rest.
	static const QRegularExpression melody(QStringLiteral("<step>G</step>\\s*<octave>4</octave>\\s*</pitch>\\s*"
		"<duration>48</duration>\\s*<voice>1</voice>"));
	QVERIFY(melody.match(xml).hasMatch());
	QVERIFY(xml.contains(QStringLiteral("<duration>96</duration>")));
}
void ArrangeToolsTest::notesRotateInsideTheClip() {
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	clip->addNote(Note(TimePos(24), TimePos(0), 60), false);
	clip->addNote(Note(TimePos(24), TimePos(144), 64), false);
	clip->changeLength(TimePos(192));
	QVERIFY(run(QStringLiteral("note.rotate"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("amount"), 96}}).ok);
	QCOMPARE(clip->notes()[0]->pos().getTicks(), tick_t{48});   // 144 + 96 wrapped
	QCOMPARE(clip->notes()[0]->key(), 64);
	QCOMPARE(clip->notes()[1]->pos().getTicks(), tick_t{96});
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(clip->notes()[0]->key(), 60);
}
