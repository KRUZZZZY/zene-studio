/*
 * ScriptBindingsTest.cpp - coverage for the Lua binding layer
 *
 * Copyright (c) 2026 LMMS contributors
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <stdexcept>

#include "AutomatableModel.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "SampleTrack.h"
#include "ScriptBindings.h"
#include "ScriptEngine.h"
#include "Song.h"
#include "Track.h"

namespace
{

//! Run \a fn and report whether it threw a std::exception; its what() lands in
//! \a message when non-null.
template <typename Fn>
bool throws(Fn&& fn, QString* message = nullptr)
{
	try
	{
		fn();
	}
	catch (const std::exception& e)
	{
		if (message != nullptr) { *message = QString::fromUtf8(e.what()); }
		return true;
	}
	catch (...)
	{
		return true;
	}
	return false;
}

} // namespace

class ScriptBindingsTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		Engine::destroy();
	}

	void init()
	{
		using namespace lmms;
		Engine::getSong()->clearProject();
		ScriptEngine::instance()->takeLogMessages();
		ScriptEngine::instance()->setProjectDir(QDir::currentPath());
		ScriptBindings::beginRun();
	}

	// --- Note / NoteBuilder (spec section 3) ---

	void luaNoteDefaults()
	{
		using namespace lmms;
		LuaNote note;
		QCOMPARE(note.key(), 60);
		QCOMPARE(note.pos(), 0);
		QCOMPARE(note.length(), TimePos::ticksPerBar() / 4);
		QCOMPARE(note.volume(), 100);
		QCOMPARE(note.panning(), 0);
	}

	void luaNoteRoundTrip()
	{
		using namespace lmms;
		LuaNote note;
		note.setKey(64);
		note.setPos(48);
		note.setLength(24);
		note.setVolume(80);
		note.setPanning(-10);

		QCOMPARE(note.key(), 64);
		QCOMPARE(note.pos(), 48);
		QCOMPARE(note.length(), 24);
		QCOMPARE(note.volume(), 80);
		QCOMPARE(note.panning(), -10);

		// toNote() carries the fields over verbatim.
		const Note asNote = note.toNote();
		QCOMPARE(asNote.key(), 64);
		QCOMPARE(asNote.pos().getTicks(), 48);
		QCOMPARE(asNote.length().getTicks(), 24);
		QCOMPARE(int(asNote.getVolume()), 80);
		QCOMPARE(int(asNote.getPanning()), -10);

		// Out-of-range values are clamped by toNote(), not by the setters.
		note.setVolume(300);
		note.setPanning(-500);
		const Note clamped = note.toNote();
		QCOMPARE(int(clamped.getVolume()), int(MaxVolume));
		QCOMPARE(int(clamped.getPanning()), int(PanningLeft));

		// Construction from a Note copies every field.
		const Note source(TimePos(96), TimePos(12), 55, 111, -64);
		const LuaNote fromNote(source);
		QCOMPARE(fromNote.key(), 55);
		QCOMPARE(fromNote.pos(), 12);
		QCOMPARE(fromNote.length(), 96);
		QCOMPARE(fromNote.volume(), 111);
		QCOMPARE(fromNote.panning(), -64);
	}

	void noteBuilderClamps()
	{
		using namespace lmms;
		LuaNoteBuilder builder;
		QCOMPARE(builder.key(), 60);
		QCOMPARE(builder.position(), 0);
		QCOMPARE(builder.length(), TimePos::ticksPerBar() / 4);
		QCOMPARE(builder.volume(), 100);
		QCOMPARE(builder.panning(), 0);

		// Fluent chaining returns the same object.
		QCOMPARE(&builder.at(72), &builder);

		builder.at(NumKeys + 50).pos(-5).len(0).vol(999).pan(999);
		QCOMPARE(builder.key(), NumKeys - 1);
		QCOMPARE(builder.position(), 0);
		QCOMPARE(builder.length(), 1);
		QCOMPARE(builder.volume(), 255);
		QCOMPARE(builder.panning(), 127);

		builder.at(-9).pan(-999);
		QCOMPARE(builder.key(), 0);
		QCOMPARE(builder.panning(), -128);

		builder.at(60).pos(0).len(24).vol(90).pan(-20);
		LuaNote& built = builder.build();
		QCOMPARE(built.key(), 60);
		QCOMPARE(built.pos(), 0);
		QCOMPARE(built.length(), 24);
		QCOMPARE(built.volume(), 90);
		QCOMPARE(built.panning(), -20);
	}

	void noteBuilderAddToClip()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaPatternStore store;
		LuaPatternClip& clip = store.addPattern();
		QVERIFY(clip.isValid());
		QCOMPARE(clip.noteCount(), 0);

		LuaNoteBuilder builder;
		builder.at(65).pos(48).len(24).vol(77).pan(12);
		builder.addTo(clip);
		QCOMPARE(engine->processCommands(), 1);

		QCOMPARE(clip.noteCount(), 1);
		LuaNote& note = clip.note(0);
		QCOMPARE(note.key(), 65);
		QCOMPARE(note.pos(), 48);
		QCOMPARE(note.length(), 24);
		QCOMPARE(note.volume(), 77);
	}

	// --- FloatModel / BoolModel wrappers ---

	void floatModelWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		FloatModel model(0.25f, -1.0f, 2.0f, 0.01f, nullptr, QStringLiteral("Cutoff"));
		LuaFloatModel wrapper(&model);
		QVERIFY(wrapper.isValid());
		QCOMPARE(wrapper.value(), 0.25f);
		QCOMPARE(wrapper.minValue(), -1.0f);
		QCOMPARE(wrapper.maxValue(), 2.0f);
		QCOMPARE(wrapper.name(), QStringLiteral("Cutoff"));

		wrapper.setValue(1.5f);
		// Model writes are queued, not applied by the worker thread: the apply
		// side drains them (ScriptBindings.cpp / spec section 4).
		QCOMPARE(engine->processCommands(), 1);
		QCOMPARE(model.value(), 1.5f);
		QCOMPARE(wrapper.value(), 1.5f);

		// A default-constructed wrapper is inert, not a crash.
		LuaFloatModel nullWrapper;
		QVERIFY(!nullWrapper.isValid());
		QCOMPARE(nullWrapper.value(), 0.0f);
		QCOMPARE(nullWrapper.minValue(), 0.0f);
		QCOMPARE(nullWrapper.maxValue(), 0.0f);
		QVERIFY(nullWrapper.name().isEmpty());
		nullWrapper.setValue(9.0f); // no-op, must not crash
		QVERIFY(!nullWrapper.isValid());
	}

	void boolModelWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		BoolModel model(false, nullptr, QStringLiteral("Mute"));
		LuaBoolModel wrapper(&model);
		QVERIFY(wrapper.isValid());
		QCOMPARE(wrapper.value(), false);
		QCOMPARE(wrapper.name(), QStringLiteral("Mute"));

		wrapper.setValue(true);
		QCOMPARE(engine->processCommands(), 1);
		QCOMPARE(model.value(), true);
		QCOMPARE(wrapper.value(), true);

		LuaBoolModel nullWrapper;
		QVERIFY(!nullWrapper.isValid());
		QCOMPARE(nullWrapper.value(), false);
		QVERIFY(nullWrapper.name().isEmpty());
		nullWrapper.setValue(true); // no-op, must not crash
		QCOMPARE(model.value(), true);
	}

	// --- Track / InstrumentTrack wrappers ---

	void instrumentTrackWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaPatternStore store;
		LuaInstrumentTrack& track = store.addInstrumentTrack();
		QVERIFY(track.isValid());

		auto* raw = dynamic_cast<InstrumentTrack*>(engine->trackAt(0));
		QVERIFY(raw != nullptr);

		QCOMPARE(track.name(), raw->name());
		QVERIFY(!track.name().isEmpty());

		track.setName(QStringLiteral("Lead"));
		engine->processCommands();
		QCOMPARE(track.name(), QStringLiteral("Lead"));
		QCOMPARE(raw->name(), QStringLiteral("Lead"));

		// Nothing in this headless harness loads an instrument plugin, so the
		// track's instrument is null and the wrapper mirrors the empty name.
		QVERIFY(raw->instrument() == nullptr);
		QVERIFY(track.instrumentName().isEmpty());

		track.setVolume(42);
		QCOMPARE(track.volume(), 42);
		QCOMPARE(raw->getVolume(), 42);

		track.setPanning(-30);
		QCOMPARE(track.panning(), -30);
		QCOMPARE(int(raw->panningModel()->value()), -30);

		LuaFloatModel& volume = track.volumeModel();
		QVERIFY(volume.isValid());
		QCOMPARE(volume.value(), raw->volumeModel()->value());
		LuaFloatModel& panning = track.panningModel();
		QVERIFY(panning.isValid());
		QCOMPARE(panning.value(), raw->panningModel()->value());

		// Null wrapper: every accessor degrades to a sentinel, never crashes.
		LuaInstrumentTrack nullTrack;
		QVERIFY(!nullTrack.isValid());
		QVERIFY(nullTrack.name().isEmpty());
		QVERIFY(nullTrack.instrumentName().isEmpty());
		QCOMPARE(nullTrack.volume(), 0);
		QCOMPARE(nullTrack.panning(), 0);
		QVERIFY(!nullTrack.volumeModel().isValid());
		QVERIFY(!nullTrack.panningModel().isValid());
		nullTrack.setName(QStringLiteral("ignored"));
		nullTrack.setVolume(10);
		nullTrack.setPanning(10);
		QCOMPARE(engine->pendingCommandCount(), 0);
	}

	void trackWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaPatternStore store;
		LuaInstrumentTrack& added = store.addInstrumentTrack();
		QVERIFY(added.isValid());

		LuaTrack& track = store.track(0);
		QVERIFY(track.isValid());
		QCOMPARE(track.type(), QStringLiteral("instrument"));
		QCOMPARE(track.index(), 0);
		QCOMPARE(track.name(), added.name());
		QCOMPARE(track.clipCount(), 1);
		QVERIFY(track.asInstrumentTrack().isValid());
		QVERIFY(track.volumeModel().isValid());
		QVERIFY(track.muteModel().isValid());
		QCOMPARE(track.muteModel().value(), false);

		track.setName(QStringLiteral("Bass"));
		engine->processCommands();
		QCOMPARE(track.name(), QStringLiteral("Bass"));

		// Out-of-range index yields an invalid view.
		QVERIFY(!store.track(99).isValid());
		QCOMPARE(store.track(99).index(), -1);
		QCOMPARE(store.track(99).type(), QStringLiteral("invalid"));
		QVERIFY(store.track(99).name().isEmpty());
		QCOMPARE(store.track(99).clipCount(), 0);
		QVERIFY(!store.track(99).asInstrumentTrack().isValid());
		QVERIFY(!store.track(99).volumeModel().isValid());
		QVERIFY(!store.track(99).muteModel().isValid());
	}

	void trackTypeNames_data()
	{
		using namespace lmms;
		QTest::addColumn<int>("trackType");
		QTest::addColumn<QString>("expected");
		QTest::newRow("instrument") << int(Track::Type::Instrument) << QStringLiteral("instrument");
		QTest::newRow("pattern") << int(Track::Type::Pattern) << QStringLiteral("pattern");
		QTest::newRow("sample") << int(Track::Type::Sample) << QStringLiteral("sample");
		QTest::newRow("automation") << int(Track::Type::Automation) << QStringLiteral("automation");
		QTest::newRow("hidden-automation")
			<< int(Track::Type::HiddenAutomation) << QStringLiteral("hidden_automation");
	}

	void trackTypeNames()
	{
		using namespace lmms;
		QFETCH(int, trackType);
		QFETCH(QString, expected);

		Track* track = Track::create(static_cast<Track::Type>(trackType), Engine::getSong());
		QVERIFY(track != nullptr);
		QCOMPARE(LuaTrack(track).type(), expected);
	}

	// --- PatternClip wrapper ---

	void patternClipWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaPatternStore store;
		LuaPatternClip& clip = store.addPattern();
		QVERIFY(clip.isValid());
		QCOMPARE(clip.patternIndex(), 0);
		QCOMPARE(clip.trackIndex(), 0);
		QCOMPARE(clip.noteCount(), 0);

		clip.setName(QStringLiteral("Verse"));
		QCOMPARE(engine->processCommands(), 1);
		QCOMPARE(clip.name(), QStringLiteral("Verse"));

		clip.setLengthTicks(0); // clamped up to 1
		engine->processCommands();
		QCOMPARE(clip.lengthTicks(), 1);
		clip.setLengthTicks(TimePos::ticksPerBar() * 2);
		engine->processCommands();
		QCOMPARE(clip.lengthTicks(), TimePos::ticksPerBar() * 2);

		// addNoteAt clamps key/pos/length/volume.
		clip.addNoteAt(NumKeys + 40, -7, 0, 999);
		engine->processCommands();
		QCOMPARE(clip.noteCount(), 1);
		LuaNote& clamped = clip.note(0);
		QCOMPARE(clamped.key(), NumKeys - 1);
		QCOMPARE(clamped.pos(), 0);
		QCOMPARE(clamped.length(), 1);
		QCOMPARE(clamped.volume(), int(MaxVolume));

		// addNote(LuaNote) routes through the same clamp path.
		LuaNote note;
		note.setKey(61);
		note.setPos(24);
		note.setLength(12);
		note.setVolume(64);
		clip.addNote(note);
		engine->processCommands();
		QCOMPARE(clip.noteCount(), 2);
		LuaNote& added = clip.note(1);
		QCOMPARE(added.key(), 61);
		QCOMPARE(added.pos(), 24);
		QCOMPARE(added.length(), 12);
		QCOMPARE(added.volume(), 64);

		clip.removeNote(0);
		engine->processCommands();
		QCOMPARE(clip.noteCount(), 1);
		QCOMPARE(clip.note(0).key(), 61);

		clip.addCheckPoint();
		QCOMPARE(engine->processCommands(), 1);

		// Out-of-range reads throw instead of returning garbage.
		QString message;
		QVERIFY(throws([&] { clip.note(-1); }, &message));
		QVERIFY2(message.contains(QStringLiteral("out of range")), qPrintable(message));
		QVERIFY(throws([&] { clip.note(clip.noteCount()); }));

		clip.clearNotes();
		engine->processCommands();
		QCOMPARE(clip.noteCount(), 0);

		// A null clip is inert: no commands, sentinel reads.
		LuaPatternClip nullClip;
		QVERIFY(!nullClip.isValid());
		QCOMPARE(nullClip.patternIndex(), -1);
		QCOMPARE(nullClip.trackIndex(), -1);
		QVERIFY(nullClip.name().isEmpty());
		QCOMPARE(nullClip.lengthTicks(), 0);
		QCOMPARE(nullClip.noteCount(), 0);
		QCOMPARE(nullClip.note(0).key(), 60);
		nullClip.setName(QStringLiteral("ignored"));
		nullClip.setLengthTicks(480);
		nullClip.addNote(note);
		nullClip.addNoteAt(60, 0, 24, 100);
		nullClip.removeNote(0);
		nullClip.clearNotes();
		nullClip.addCheckPoint();
		QCOMPARE(engine->pendingCommandCount(), 0);
		QCOMPARE(nullClip.noteCount(), 0);
	}

	// --- Transport / PatternStore / Song ---

	void transportWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		auto* song = Engine::getSong();
		LuaTransport transport;

		QCOMPARE(transport.isPlaying(), false);
		song->getTimeline().setTicks(480);
		QCOMPARE(transport.position(), 480);

		transport.play();
		QCOMPARE(engine->processCommands(), 1);
		QCOMPARE(transport.isPlaying(), true);
		QCOMPARE(song->isPlaying(), true);

		transport.stop();
		QCOMPARE(engine->processCommands(), 1);
		QCOMPARE(transport.isPlaying(), false);
		QCOMPARE(song->isPlaying(), false);
	}

	void patternStoreWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaPatternStore store;

		QCOMPARE(store.patternCount(), 0);
		QCOMPARE(store.trackCount(), 0);

		LuaPatternClip& first = store.addPattern();
		QVERIFY(first.isValid());
		QCOMPARE(store.patternCount(), 1);
		QCOMPARE(store.trackCount(), 1);
		QCOMPARE(first.patternIndex(), 0);
		QCOMPARE(first.trackIndex(), 0);

		LuaInstrumentTrack& second = store.addInstrumentTrack();
		QVERIFY(second.isValid());
		QCOMPARE(store.trackCount(), 2);
		QCOMPARE(store.patternCount(), 1);

		// addPattern() now targets the newest track's clip of the newest pattern.
		LuaPatternClip& secondClip = store.addPattern();
		QVERIFY(secondClip.isValid());
		QCOMPARE(secondClip.patternIndex(), 0);
		QCOMPARE(secondClip.trackIndex(), 1);

		// patternClip() exposes the same clip by index.
		LuaPatternClip& byIndex = store.patternClip(0, 0);
		QVERIFY(byIndex.isValid());
		QCOMPARE(byIndex.patternIndex(), 0);
		QCOMPARE(byIndex.trackIndex(), 0);
		QVERIFY(!store.patternClip(0, 99).isValid());
		QVERIFY(!store.patternClip(-1, 0).isValid());

		QCOMPARE(engine->pendingCommandCount(), 0);
	}

	void songWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaSong song;

		// patternStore()/transport()/addPattern()/pattern()/patternCount().
		QCOMPARE(song.patternCount(), 0);
		QCOMPARE(song.patternStore().patternCount(), 0);
		QVERIFY(!song.transport().isPlaying());

		LuaPatternClip& clip = song.addPattern();
		QVERIFY(clip.isValid());
		QCOMPARE(song.patternCount(), 1);
		QCOMPARE(song.patternStore().trackCount(), 1);

		clip.setName(QStringLiteral("Chorus"));
		engine->processCommands();
		QCOMPARE(song.pattern(0, 0).name(), QStringLiteral("Chorus"));

		// tempo()/setTempo() with clamping.
		song.setTempo(123);
		engine->processCommands();
		QCOMPARE(song.tempo(), 123);
		song.setTempo(5000);
		engine->processCommands();
		QCOMPARE(song.tempo(), 999);
		song.setTempo(-7);
		engine->processCommands();
		QCOMPARE(song.tempo(), int(MinTempo));

		// masterVolume()/setMasterVolume() with clamping.
		song.setMasterVolume(77);
		engine->processCommands();
		QCOMPARE(song.masterVolume(), 77);
		song.setMasterVolume(999);
		engine->processCommands();
		QCOMPARE(song.masterVolume(), 200);
		song.setMasterVolume(-1);
		engine->processCommands();
		QCOMPARE(song.masterVolume(), 0);

		// saveProject() writes inside the sandbox and refuses to escape it.
		QTemporaryDir projectDir;
		QVERIFY(projectDir.isValid());
		engine->setProjectDir(projectDir.path());
		QVERIFY(song.saveProject(QStringLiteral("saved.mmp")));
		// saveProjectFile() defaults to withResources=false, so DataFile writes
		// the plain project file at the resolved path.
		QVERIFY(QFileInfo::exists(QDir(projectDir.path()).absoluteFilePath(QStringLiteral("saved.mmp"))));

		QString message;
		QVERIFY(throws([&] { song.saveProject(QStringLiteral("../escape.mmp")); }, &message));
		QVERIFY2(message.contains(QStringLiteral("escapes the project directory")),
			qPrintable(message));
	}

	// --- ProjectFile / Log / MIDI ---

	void projectFileWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		QTemporaryDir projectDir;
		QVERIFY(projectDir.isValid());
		engine->setProjectDir(projectDir.path());

		LuaProjectFile files;
		QCOMPARE(files.projectDir(), projectDir.path());
		// exists() resolves with forWrite=false, and resolveProjectPath()
		// rejects paths that do not exist yet, so a missing file is reported
		// as an error rather than as false.
		QString missing;
		QVERIFY(throws([&] { files.exists(QStringLiteral("notes.txt")); }, &missing));
		QVERIFY2(missing.contains(QStringLiteral("does not exist")), qPrintable(missing));

		QVERIFY(files.write(QStringLiteral("notes.txt"), QStringLiteral("hello sandbox")));
		QVERIFY(files.exists(QStringLiteral("notes.txt")));
		QCOMPARE(files.read(QStringLiteral("notes.txt")), QStringLiteral("hello sandbox"));

		const QStringList listing = files.list(QStringLiteral("."));
		QVERIFY2(listing.contains(QStringLiteral("notes.txt")), qPrintable(listing.join('|')));

		// Reads outside the sandbox are refused.
		QString message;
		QVERIFY(throws([&] { files.read(QStringLiteral("/etc/passwd")); }, &message));
		QVERIFY2(message.contains(QStringLiteral("escapes the project directory")),
			qPrintable(message));

		message.clear();
		QVERIFY(throws([&] { files.write(QStringLiteral("../escape.txt"), QStringLiteral("x")); },
			&message));
		QVERIFY2(message.contains(QStringLiteral("escapes the project directory")),
			qPrintable(message));

		message.clear();
		QVERIFY(throws([&] { files.list(QStringLiteral("../../")); }, &message));
		QVERIFY2(message.contains(QStringLiteral("escapes the project directory")),
			qPrintable(message));

		// Missing file inside the sandbox.
		message.clear();
		QVERIFY(throws([&] { files.read(QStringLiteral("missing.txt")); }, &message));
		QVERIFY2(message.contains(QStringLiteral("does not exist")), qPrintable(message));

		// Writing onto a directory fails at open() time.
		QVERIFY(QDir(projectDir.path()).mkdir(QStringLiteral("adir")));
		message.clear();
		QVERIFY(throws([&] { files.write(QStringLiteral("adir"), QStringLiteral("x")); }, &message));
		QVERIFY2(message.contains(QStringLiteral("cannot open")), qPrintable(message));

		// A sandbox root that does not exist is rejected outright.
		engine->setProjectDir(QDir(projectDir.path()).absoluteFilePath(QStringLiteral("gone")));
		message.clear();
		QVERIFY(throws([&] { files.exists(QStringLiteral("x")); }, &message));
		QVERIFY2(message.contains(QStringLiteral("sandbox root does not exist")), qPrintable(message));
	}

	void logWrapper()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		LuaLog log;

		log.info(QStringLiteral("hello"));
		log.warn(QStringLiteral("careful"));
		log.error(QStringLiteral("boom"));
		log.write(QStringLiteral("raw line"));

		const QStringList messages = engine->takeLogMessages();
		QCOMPARE(messages.size(), 4);
		QCOMPARE(messages[0], QStringLiteral("[info] hello"));
		QCOMPARE(messages[1], QStringLiteral("[warn] careful"));
		QCOMPARE(messages[2], QStringLiteral("[error] boom"));
		QCOMPARE(messages[3], QStringLiteral("raw line"));
	}

	void midiWrappers()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();

		LuaMidiEvent event;
		QVERIFY(!event.isValid());
		QCOMPARE(&event.set(3, 64, 100, true), &event);
		QVERIFY(event.isValid());
		QCOMPARE(event.channel(), 3);
		QCOMPARE(event.key(), 64);
		QCOMPARE(event.velocity(), 100);
		QCOMPARE(event.isNoteOn(), true);

		LuaMidiIn midiIn;
		QVERIFY(!midiIn.hasEvent());
		QVERIFY(!midiIn.next().isValid());

		engine->pushMidiInEvent(1, 60, 90, true);
		engine->pushMidiInEvent(1, 62, 0, false);
		QVERIFY(midiIn.hasEvent());
		LuaMidiEvent& first = midiIn.next();
		QVERIFY(first.isValid());
		QCOMPARE(first.channel(), 1);
		QCOMPARE(first.key(), 60);
		QCOMPARE(first.velocity(), 90);
		QCOMPARE(first.isNoteOn(), true);
		LuaMidiEvent& second = midiIn.next();
		QCOMPARE(second.key(), 62);
		QCOMPARE(second.isNoteOn(), false);
		QVERIFY(!midiIn.hasEvent());

		LuaMidiOut midiOut;
		midiOut.noteOn(2, 67, 111);
		midiOut.noteOff(2, 67);
		QCOMPARE(engine->processCommands(), 2);
		const QStringList log = engine->takeLogMessages();
		QCOMPARE(log.size(), 2);
		QCOMPARE(log[0], QStringLiteral("MIDI out: note-on ch2 key67 vel111"));
		QCOMPARE(log[1], QStringLiteral("MIDI out: note-off ch2 key67 vel0"));
	}

	// --- lmms namespace surface (registerAll) ---

	void lmmsNamespace()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		QTemporaryDir projectDir;
		QVERIFY(projectDir.isValid());
		engine->setProjectDir(projectDir.path());
		QVERIFY(engine->runString(QStringLiteral(R"(
assert(lmms.version() == '0.1')
assert(lmms.ticksPerBar() == 192)
assert(lmms.stepsPerBar() == 16)
assert(lmms.song() ~= nil)
assert(lmms.patternStore() ~= nil)
assert(lmms.transport() ~= nil)
assert(lmms.log() ~= nil)
assert(lmms.projectFile() ~= nil)
assert(lmms.midiIn() ~= nil)
assert(lmms.midiOut() ~= nil)
local builder = lmms.note()
assert(builder:at(72):pos(24):len(12):vol(90):pan(-5):key() == 72)
assert(lmms.projectDir() == lmms.projectFile():projectDir())
local files = lmms.projectFile():list('.')
assert(type(files) == 'table')
assert(#files == 0)
lmms.log():info('namespace ok')
local budget = lmms.instructionBudget()
lmms.setInstructionBudget(budget)
)"), nullptr, QStringLiteral("=(namespace)")) == ScriptEngine::RunResult::Ok);
		QVERIFY2(engine->lastError().isEmpty(), qPrintable(engine->lastError()));
		QVERIFY(engine->takeLogMessages().contains(QStringLiteral("[info] namespace ok")));
		QCOMPARE(engine->instructionBudget(), quint64(5000000));
	}
};

QTEST_GUILESS_MAIN(ScriptBindingsTest)
#include "ScriptBindingsTest.moc"
