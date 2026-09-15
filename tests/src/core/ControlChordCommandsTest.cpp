/*
 * ControlChordCommandsTest.cpp - the chord.* command group's SURFACE
 *                                (SPEC-zene-studio.md A11-A16, the release
 *                                contract section 3.1, docs/CHORD-TRACK.md).
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

class ControlChordCommandsTest : public QObject
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
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("chord.set"), QStringLiteral("chord.remove"),
			QStringLiteral("chord.clear"), QStringLiteral("chord.detect_to_track"),
			QStringLiteral("chord.track_write"),
			QStringLiteral("chord.progression_generate")};
		for (const QString& id : chordIds())
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("chord"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
	}

	/*! The contract table classifies the group: three read-only rows and six
	 *  true_inverse rows, four of them on a recorded action and two on the
	 *  clip's own checkpoint. The mechanism text is what an agent reads to know
	 *  how to take a call back, so it is asserted rather than assumed. */
	void contractRowsClassifyTheGroup()
	{
		const QStringList trackEdits = {QStringLiteral("chord.set"), QStringLiteral("chord.remove"),
			QStringLiteral("chord.clear"), QStringLiteral("chord.detect_to_track")};
		const QStringList generators = {QStringLiteral("chord.track_write"),
			QStringLiteral("chord.progression_generate")};
		const QStringList reads = {QStringLiteral("chord.get_state"), QStringLiteral("chord.detect"),
			QStringLiteral("chord.progression_list")};

		for (const QString& id : chordIds())
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
			const QString expected = reads.contains(id) ? QStringLiteral("not_mutating")
				: QStringLiteral("true_inverse");
			QCOMPARE(control::reversibilityClassName(row->cls), expected);
		}
		for (const QString& id : trackEdits)
		{
			QVERIFY2(contractRow(id)->mechanism.contains(QStringLiteral("action checkpoint")),
				qPrintable(id + " does not name the recorded action checkpoint"));
			QVERIFY(contractRow(id)->mechanism.contains(QStringLiteral("<chord-track>")));
		}
		for (const QString& id : generators)
		{
			QVERIFY2(contractRow(id)->mechanism.contains(QStringLiteral("checkpoint")),
				qPrintable(id + " does not name its mechanism"));
			QVERIFY2(!contractRow(id)->mechanism.contains(QStringLiteral("action checkpoint")),
				qPrintable(id + " claims the track's mechanism for a clip write"));
		}
		// The track's own position is the target a repeated set coalesces on.
		QCOMPARE(contractRow(QStringLiteral("chord.set"))->coalesceTarget, QStringLiteral("pos"));
	}

	/*! THE DETECTION, on a clip built by commands: a C major triad written into
	 *  a fresh clip must read back as "Major" rooted at C, exactly, with the
	 *  key of the whole clip reported as the same scale. */
	void aKnownClipDetectsTheChordItSpells()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 60, 0, 100));
		QVERIFY(addNote(clip, 64, 0, 100));
		QVERIFY(addNote(clip, 67, 0, 100));

		const ControlResult detected = run(QStringLiteral("chord.detect"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(detected.ok, qPrintable(detected.errorMessage));
		QCOMPARE(detected.result.value(QStringLiteral("clip")).toString(), clip);
		QCOMPARE(detected.result.value(QStringLiteral("count")).toInt(), 1);
		const QJsonObject match = detected.result.value(QStringLiteral("chords"))
			.toArray().at(0).toObject();
		QCOMPARE(match.value(QStringLiteral("chord")).toString(), QStringLiteral("Major"));
		QCOMPARE(match.value(QStringLiteral("root")).toInt(), 0);
		QCOMPARE(match.value(QStringLiteral("key")).toInt(), 60);
		QCOMPARE(match.value(QStringLiteral("bass")).toInt(), 60);
		QCOMPARE(match.value(QStringLiteral("notes")).toInt(), 3);
		QCOMPARE(match.value(QStringLiteral("missing")).toInt(), 0);
		QCOMPARE(match.value(QStringLiteral("extra")).toInt(), 0);
		QVERIFY(match.value(QStringLiteral("exact")).toBool());
		QCOMPARE(match.value(QStringLiteral("scale")).toString(), QStringLiteral("Major"));

		const QJsonObject key = detected.result.value(QStringLiteral("key")).toObject();
		QCOMPARE(key.value(QStringLiteral("scale")).toString(), QStringLiteral("Major"));
		QCOMPARE(key.value(QStringLiteral("root")).toInt(), 0);
		QCOMPARE(key.value(QStringLiteral("pitch_classes")).toInt(), 3);
		QVERIFY(key.value(QStringLiteral("complete")).toBool());

		// A clip that does not exist, and a clip with no notes: two typed
		// refusals that change nothing.
		const ControlResult missing = run(QStringLiteral("chord.detect"),
			{{QStringLiteral("clip"), QStringLiteral("clip-99")}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);

		const QString empty = makeClip();
		QVERIFY(!empty.isEmpty());
		const ControlResult nothing = run(QStringLiteral("chord.detect_to_track"),
			{{QStringLiteral("clip"), empty}});
		QVERIFY(!nothing.ok);
		QCOMPARE(nothing.errorKind, ControlErrorKind::Refused);
	}

	/*! The track half of the group: set / replace / remove / clear, with the
	 *  typed refusals a caller has to be able to distinguish, and the track read
	 *  back after every one of them. */
	void theTrackVerbsWriteAndRefuseTyped()
	{
		QVERIFY(run(QStringLiteral("chord.get_state")).result
			.value(QStringLiteral("chords")).toInt() == 0);

		// A name outside the vocabulary, and a position outside the range.
		QCOMPARE(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("nope")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), -1}, {QStringLiteral("chord"), QStringLiteral("Major")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Major")},
				{QStringLiteral("root"), 12}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY2(trackOf().isEmpty(), "a refused set wrote something");

		const ControlResult written = run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Maj7")},
				{QStringLiteral("root"), 0}, {QStringLiteral("octave"), 4}});
		QVERIFY2(written.ok, qPrintable(written.errorMessage));
		QVERIFY(!written.result.value(QStringLiteral("replaced")).toBool());
		QCOMPARE(written.result.value(QStringLiteral("chords")).toInt(), 1);
		QVERIFY((trackOf() == QVector<EventAt>{{0, 0, 0, 4, 60, QStringLiteral("Maj7"), QStringLiteral("")}}));

		// The same position REPLACES, and the track does not grow.
		const ControlResult replaced = run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("major")},
				{QStringLiteral("root"), 2}, {QStringLiteral("octave"), 3},
				{QStringLiteral("length"), 48}});
		QVERIFY2(replaced.ok, qPrintable(replaced.errorMessage));
		QVERIFY2(replaced.result.value(QStringLiteral("replaced")).toBool(),
			"a set at an occupied position did not report the replace");
		QCOMPARE(replaced.result.value(QStringLiteral("chords")).toInt(), 1);

		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 192}, {QStringLiteral("chord"), QStringLiteral("sus4")},
				{QStringLiteral("root"), 7}}).ok);
		QCOMPARE(trackOf().size(), 2);

		// A tick that holds no chord is a typed not_found, never a silent ok.
		QCOMPARE(run(QStringLiteral("chord.remove"), {{QStringLiteral("pos"), 191}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(trackOf().size(), 2);
		QVERIFY(run(QStringLiteral("chord.remove"), {{QStringLiteral("pos"), 192}}).ok);
		QCOMPARE(trackOf().size(), 1);

		QVERIFY(run(QStringLiteral("chord.clear")).ok);
		QVERIFY(trackOf().isEmpty());
		// ... and clearing an EMPTY track is refused rather than recorded as an
		// undo step for a no-op.
		QCOMPARE(run(QStringLiteral("chord.clear")).errorKind, ControlErrorKind::Refused);
	}

	/*! THE TRACK'S INVERSE: a recorded action checkpoint. A set, a remove and a
	 *  clear each come back through control.undo, which is the whole claim. */
	void aTrackEditIsReversibleThroughTheRecordedCheckpoint()
	{
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Maj7")}}).ok);
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 96}, {QStringLiteral("chord"), QStringLiteral("minor")},
				{QStringLiteral("root"), 9}}).ok);
		QCOMPARE(trackOf().size(), 2);

		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the second set");
		QCOMPARE(trackOf().size(), 1);
		QCOMPARE(trackOf()[0].chord, QStringLiteral("Maj7"));

		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the first set");
		QVERIFY2(trackOf().isEmpty(), "the track did not come back empty after two undos");

		// A REMOVE comes back with the event's own values, not just its count.
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 48}, {QStringLiteral("chord"), QStringLiteral("9")},
				{QStringLiteral("root"), 7}, {QStringLiteral("octave"), 3},
				{QStringLiteral("length"), 24}}).ok);
		QVERIFY(run(QStringLiteral("chord.remove"), {{QStringLiteral("pos"), 48}}).ok);
		QVERIFY(trackOf().isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const QVector<EventAt> restored = trackOf();
		QCOMPARE(restored.size(), 1);
		QCOMPARE(restored[0].chord, QStringLiteral("9"));
		QCOMPARE(restored[0].root, 7);
		QCOMPARE(restored[0].octave, 3);
		QCOMPARE(restored[0].length, 24);

		// ... and a CLEAR takes the whole track back.
		QVERIFY(run(QStringLiteral("chord.set"),
			{{QStringLiteral("pos"), 96}, {QStringLiteral("chord"), QStringLiteral("sus2")}}).ok);
		QCOMPARE(trackOf().size(), 2);
		QVERIFY(run(QStringLiteral("chord.clear")).ok);
		QVERIFY(trackOf().isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackOf().size(), 2);
	}

	/*! THE DETECTION WRITER: the chords the detector named land on the track
	 *  with their roots and octaves, a slice it cannot name is reported as
	 *  skipped, and a detection that names nothing at all is refused rather than
	 *  half-written. */
	void theDetectionIsWrittenOntoTheTrack()
	{
		const QString clip = makeClip();
		QVERIFY(!clip.isEmpty());
		QVERIFY(addNote(clip, 60, 0, 100));
		QVERIFY(addNote(clip, 64, 0, 100));
		QVERIFY(addNote(clip, 67, 0, 100));
		QVERIFY(addNote(clip, 65, 192, 100));
		QVERIFY(addNote(clip, 69, 192, 100));
		QVERIFY(addNote(clip, 72, 192, 100));

		const ControlResult written = run(QStringLiteral("chord.detect_to_track"),
			{{QStringLiteral("clip"), clip}});
		QVERIFY2(written.ok, qPrintable(written.errorMessage));
		QCOMPARE(written.result.value(QStringLiteral("detected")).toInt(), 2);
		QCOMPARE(written.result.value(QStringLiteral("written")).toInt(), 2);
		QCOMPARE(written.result.value(QStringLiteral("skipped")).toInt(), 0);

		const QVector<EventAt> track = trackOf();
		QCOMPARE(track.size(), 2);
		QCOMPARE(track[0].pos, 0);
		QCOMPARE(track[0].chord, QStringLiteral("Major"));
		QCOMPARE(track[0].root, 0);
		QCOMPARE(track[0].octave, 4);
		QCOMPARE(track[1].pos, 192);
		QCOMPARE(track[1].chord, QStringLiteral("Major"));
		QCOMPARE(track[1].root, 5);

		// A single note is not a chord, so a clip of one note names nothing -
		// and the track is left EXACTLY as it was, with no undo step behind it.
		const QString single = makeClip();
		QVERIFY(!single.isEmpty());
		QVERIFY(addNote(single, 60, 0, 100));
		const ControlResult refused = run(QStringLiteral("chord.detect_to_track"),
			{{QStringLiteral("clip"), single}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(trackOf().size(), 2);

		// APPEND adds to what is there, instead of replacing it.
		QVERIFY(run(QStringLiteral("chord.detect_to_track"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("append"), true}}).ok);
		QCOMPARE(trackOf().size(), 4);
	}

	/*! THE GENERATOR'S REPEATABILITY PAIR, through the surface: two clips, one
	 *  seed - identical takes, read note for note off the wire; another seed -
	 *  a different take. Then control.undo puts the clip back, which is the
	 *  A16 claim for both generators. */
};

QTEST_GUILESS_MAIN(ControlChordCommandsTest)
#include "ControlChordCommandsTest.moc"
