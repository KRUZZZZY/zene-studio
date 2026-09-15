/*
 * ControlChordWriteTest.cpp - the WRITE half of the chord.* command group's
 *                            surface: the two generators, the track's
 *                            write-out and the project file
 *                                (SPEC-zene-studio.md A11-A16, the release
 *                                contract section 3.1, docs/CHORD-TRACK.md).
 *
 * The read half - the nine ids and their schemas, the contract rows, the typed
 * refusals, the track verbs and their recorded action checkpoint, and the known
 * clip's detection - is ControlChordCommandsTest.cpp, the same split as the two
 * command translation units it drives.
 *
 * The ENGINE arithmetic is held to account by ChordTrackTest.cpp,
 * ChordDetectTest.cpp and ChordProgressionTest.cpp with no Engine at all; this
 * file holds the SURFACE to account: nine registered commands with argument and
 * result schemas, typed refusals that change nothing, the key-format constants
 * that come from the pre-existing vocabulary, and - the part that is not
 * optional - the two KINDS of inverse this group has:
 *
 *   - a TRACK edit reverses through a recorded action checkpoint (the track is
 *     project state the Song's journal checkpoint does not carry);
 *   - a GENERATOR reverses through the CLIP's own journal checkpoint.
 *
 * The numbers are asserted, not the success of a call: the chords a detection
 * names are read back off the wire, the notes a generator writes are read back
 * through roll.get_state and compared between seeds, and the track is read back
 * through chord.get_state after every step - including after a save and a
 * reload of the project file. Every handler is invoked through
 * ControlRegistry::invoke, which is the entry the socket dispatches to, so the
 * protocol a socket client sees (schemas, typed refusals, transaction records)
 * is the one exercised here.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include "ChordTestSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;
using namespace chordtest;

class ControlChordWriteTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		// The chord track is PROJECT state, so a test starts from empty rather
		// than from whatever the previous test wrote.
		Engine::getSong()->chordTrack().clear();
	}

	//! Every command of the group declares the contract's parts.

	void theGeneratorIsSeededAndRepeatable()
	{
		const QString first = makeClip();
		const QString second = makeClip();
		const QString third = makeClip();
		QVERIFY(!first.isEmpty() && !second.isEmpty() && !third.isEmpty());
		QVERIFY(addNote(first, 40, 0, 60)); // something to REPLACE, so `replace` is proved
		const QVector<NoteAt> beforeFirst = notesOf(first);
		QVERIFY(beforeFirst.size() == 1);

		const auto generate = [](const QString& clip, int seed)
		{
			return run(QStringLiteral("chord.progression_generate"),
				{{QStringLiteral("clip"), clip},
					{QStringLiteral("progression"), QStringLiteral("I-V-vi-IV")},
					{QStringLiteral("scale"), QStringLiteral("Major")},
					{QStringLiteral("root"), 0}, {QStringLiteral("step_ticks"), 192},
					{QStringLiteral("steps"), 4}, {QStringLiteral("pattern"), QStringLiteral("block")},
					{QStringLiteral("seed"), seed}, {QStringLiteral("variation"), 0.5}});
		};

		const ControlResult one = generate(first, 7);
		const ControlResult two = generate(second, 7);
		const ControlResult other = generate(third, 8);
		QVERIFY2(one.ok, qPrintable(one.errorMessage));
		QVERIFY2(two.ok, qPrintable(two.errorMessage));
		QVERIFY2(other.ok, qPrintable(other.errorMessage));
		QCOMPARE(one.result.value(QStringLiteral("notes_written")).toInt(), 12);
		QCOMPARE(one.result.value(QStringLiteral("notes_replaced")).toInt(), 1);
		QCOMPARE(one.result.value(QStringLiteral("progression")).toString(),
			QStringLiteral("I-V-vi-IV"));
		QCOMPARE(one.result.value(QStringLiteral("seed")).toInt(), 7);

		const QVector<NoteAt> takeOne = notesOf(first);
		const QVector<NoteAt> takeTwo = notesOf(second);
		const QVector<NoteAt> takeOther = notesOf(third);
		QCOMPARE(takeOne.size(), 12);
		QVERIFY2((takeOne == takeTwo),
			qPrintable(QStringLiteral("one seed produced two takes: %1 vs %2")
				.arg(describe(takeOne), describe(takeTwo))));
		QVERIFY2((takeOne != takeOther),
			qPrintable(QStringLiteral("two seeds produced the same take: %1").arg(describe(takeOne))));

		// The chords the wire reports are the progression's own.
		const QJsonArray chords = one.result.value(QStringLiteral("chords")).toArray();
		QCOMPARE(chords.size(), 4);
		QCOMPARE(chords.at(0).toObject().value(QStringLiteral("chord")).toString(),
			QStringLiteral("Major"));
		QCOMPARE(chords.at(2).toObject().value(QStringLiteral("chord")).toString(),
			QStringLiteral("minor"));

		// No variation: the seed decides nothing, and the take is on the grid.
		const ControlResult plain = run(QStringLiteral("chord.progression_generate"),
			{{QStringLiteral("clip"), second}, {QStringLiteral("progression"), QStringLiteral("I-V-vi-IV")},
				{QStringLiteral("seed"), 99}, {QStringLiteral("variation"), 0.0}});
		QVERIFY2(plain.ok, qPrintable(plain.errorMessage));
		const ControlResult plainAgain = run(QStringLiteral("chord.progression_generate"),
			{{QStringLiteral("clip"), third}, {QStringLiteral("progression"), QStringLiteral("I-V-vi-IV")},
				{QStringLiteral("seed"), 100}, {QStringLiteral("variation"), 0.0}});
		QVERIFY2(plainAgain.ok, qPrintable(plainAgain.errorMessage));
		QVERIFY2((notesOf(second) == notesOf(third)),
			"a variation of 0 let the seed change the take");

		// A progression this engine does not have is refused BEFORE anything is
		// written, and records no undo step (the five undos below are exactly
		// the five generations). The kind is invalid_args and not not_found
		// because the ARGUMENTS SCHEMA's enum refuses the name first - the
		// shape every enumerated argument in this surface has.
		QCOMPARE(run(QStringLiteral("chord.progression_generate"),
			{{QStringLiteral("clip"), first}, {QStringLiteral("progression"), QStringLiteral("it")}})
			.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(notesOf(first).size(), 12);

		// THE CLIP'S OWN CHECKPOINT is the A16 inverse: undo the generations in
		// the order they were made (LIFO) and the clip whose note was replaced
		// comes back exactly as it was.
		for (int step = 0; step < 5; ++step)
		{
			QVERIFY2(run(QStringLiteral("control.undo")).ok,
				qPrintable(QStringLiteral("control.undo refused generation %1").arg(step)));
		}
		QVERIFY2((notesOf(first) == beforeFirst),
			qPrintable(QStringLiteral("the clip did not come back: %1").arg(describe(notesOf(first)))));
		QVERIFY2(notesOf(second).isEmpty(), "the second clip was not restored");
		QVERIFY2(notesOf(third).isEmpty(), "the third clip was not restored");
	}

	/*! chord.track_write: the chord track's OWN chords become notes. The track
	 *  is written first (two chords, the second with a length), and the clip
	 *  must hold the triads at the ticks the track names. */
	void theTrackIsWrittenIntoAClip()
	{
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Major")},
				{QStringLiteral("root"), 0}, {QStringLiteral("octave"), 4}}).ok);
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 192}, {QStringLiteral("chord"), QStringLiteral("minor")},
				{QStringLiteral("root"), 9}, {QStringLiteral("octave"), 3}}).ok);

		// A clip id that names nothing is a typed not_found, and the track is
		// not touched by the attempt.
		const ControlResult unknown = run(QStringLiteral("chord.track_write"),
			{{QStringLiteral("clip"), QStringLiteral("clip-99")}});
		QVERIFY(!unknown.ok);
		QCOMPARE(unknown.errorKind, ControlErrorKind::NotFound);

		const QString clip = makeClip();
		QVERIFY(!clip.isEmpty());
		const ControlResult written = run(QStringLiteral("chord.track_write"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(written.ok, qPrintable(written.errorMessage));
		QCOMPARE(written.result.value(QStringLiteral("chords_written")).toInt(), 2);
		QCOMPARE(written.result.value(QStringLiteral("notes_written")).toInt(), 6);
		QCOMPARE(written.result.value(QStringLiteral("pattern")).toString(), QStringLiteral("block"));

		const QVector<NoteAt> notes = notesOf(clip);
		QCOMPARE(notes.size(), 6);
		// The clip's own order is position ascending, then key DESCENDING
		// (Note::lessThan), so the C major triad at 0 reads back 67, 64, 60 -
		// held until the next chord, 192 ticks.
		QCOMPARE(notes[0].position, 0);
		QCOMPARE(notes[0].length, 192);
		QCOMPARE(notes[0].key, 67);
		QCOMPARE(notes[1].key, 64);
		QCOMPARE(notes[2].key, 60);
		// The A minor triad at 192 (A3, C4, E4), held for the fallback step.
		QCOMPARE(notes[3].position, 192);
		QCOMPARE(notes[3].length, DefaultTicksPerBar);
		QCOMPARE(notes[3].key, 64);
		QCOMPARE(notes[4].key, 60);
		QCOMPARE(notes[5].key, 57);

		// ... and an EMPTY chord track has nothing to write: refused, typed.
		QVERIFY(run(QStringLiteral("chord.clear")).ok);
		const ControlResult none = run(QStringLiteral("chord.track_write"),
			{{QStringLiteral("clip"), makeClip()}});
		QVERIFY(!none.ok);
		QCOMPARE(none.errorKind, ControlErrorKind::Refused);
	}

	/*! The track is PROJECT state: it is written into the project file when it
	 *  holds a chord, restored when the file is loaded, and - the load-bearing
	 *  half - a project that holds none carries NO element, so the bytes a
	 *  project that never used a chord saves are the bytes it always had. */
	void theChordTrackPersistsInTheProjectFile()
	{
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Major")},
				{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 384}, {QStringLiteral("chord"), QStringLiteral("m7")},
				{QStringLiteral("root"), 2}, {QStringLiteral("octave"), 3},
				{QStringLiteral("length"), 96}}).ok);
		const QVector<EventAt> saved = trackOf();
		QCOMPARE(saved.size(), 2);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("chord.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(path), "the project could not be written");

		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(file.readAll());
		file.close();
		QVERIFY2(text.contains(QStringLiteral("<chord-track")),
			"the saved project carries no chord-track element");

		// The negative control: with the track empty the element is NOT there.
		QVERIFY(run(QStringLiteral("chord.clear")).ok);
		const QString bare = dir.filePath(QStringLiteral("bare.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(bare));
		QFile bareFile(bare);
		QVERIFY(bareFile.open(QIODevice::ReadOnly));
		const QString bareText = QString::fromUtf8(bareFile.readAll());
		bareFile.close();
		QVERIFY2(!bareText.contains(QStringLiteral("<chord-track")),
			"a project with no chord wrote a chord-track element");

		// ... and the file that HAS the element restores it, field for field.
		Engine::getSong()->loadProject(path);
		const QVector<EventAt> restored = trackOf();
		QCOMPARE(restored.size(), 2);
		QVERIFY2((restored == saved),
			qPrintable(QStringLiteral("the track did not come back: %1 vs %2")
				.arg(describe(restored), describe(saved))));
	}

	//! The vocabularies the group draws on are reported, so a caller can ask
	//! what is possible instead of guessing.
	void theGroupReportsItsVocabularies()
	{
		const ControlResult listed = run(QStringLiteral("chord.progression_list"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QVERIFY(listed.result.value(QStringLiteral("progressions")).toArray().size() >= 4);
		QVERIFY(listed.result.value(QStringLiteral("scales")).toArray().size() > 0);
		QVERIFY(listed.result.value(QStringLiteral("chords")).toArray().size() > 0);
		const QJsonArray patterns = listed.result.value(QStringLiteral("patterns")).toArray();
		QCOMPARE(patterns.size(), 4);
		QCOMPARE(patterns.at(0).toString(), QStringLiteral("block"));
		QCOMPARE(listed.result.value(QStringLiteral("max_chords")).toInt(), 64);

		// The two vocabularies are the piano roll's own: "Maj7" is a chord of
		// it and "Dorian" a scale, and neither list carries the other's names.
		const QJsonArray chords = listed.result.value(QStringLiteral("chords")).toArray();
		const QJsonArray scales = listed.result.value(QStringLiteral("scales")).toArray();
		QStringList chordNames;
		for (const QJsonValue& value : chords) { chordNames << value.toString(); }
		QStringList scaleNames;
		for (const QJsonValue& value : scales) { scaleNames << value.toString(); }
		QVERIFY(chordNames.contains(QStringLiteral("Maj7")));
		QVERIFY(scaleNames.contains(QStringLiteral("Dorian")));
		QVERIFY(!chordNames.contains(QStringLiteral("Dorian")));
		QVERIFY(!scaleNames.contains(QStringLiteral("Maj7")));

		const ControlResult one = run(QStringLiteral("chord.progression_list"),
			{{QStringLiteral("name"), QStringLiteral("ii-V-I")}});
		QVERIFY(one.ok);
		QCOMPARE(one.result.value(QStringLiteral("progression")).toObject()
			.value(QStringLiteral("degrees")).toArray().size(), 3);
		QCOMPARE(run(QStringLiteral("chord.progression_list"),
			{{QStringLiteral("name"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::NotFound);

		// A generation that names a progression this engine does not have: the
		// schema's enum is the first gate, so the kind is invalid_args (the
		// listing above is what a caller reads to get a name right).
		QCOMPARE(run(QStringLiteral("chord.progression_generate"),
			{{QStringLiteral("clip"), makeClip()},
				{QStringLiteral("progression"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}
};

QTEST_GUILESS_MAIN(ControlChordWriteTest)
#include "ControlChordWriteTest.moc"
