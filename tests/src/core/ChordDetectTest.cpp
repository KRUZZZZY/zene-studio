/*
 * ChordDetectTest.cpp - what a note list spells, on known note sets.
 *
 * THE SHAPE OF THE PROOF. Every case here is a hand-written set of notes whose
 * chord is not in dispute (a C major triad is C-E-G), so the test asserts the
 * VOCABULARY'S OWN NAME for it and the root, not "a non-empty string came
 * back". The detection is a pure function over a NoteVector, so no Engine, no
 * clip and no GUI are involved - ControlChordCommandsTest.cpp runs the same
 * detection through the socket-shaped surface, on a clip built by commands.
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

#include <QString>
#include <QVector>

#include "ChordDetect.h"
#include "ChordTestSupport.h"
#include "Note.h"

using namespace lmms;
using namespace chordtest; // notes(), freeNotes() and the wire read-backs

namespace
{

//! The default options, named: "the notes that sound together", no window.
ChordDetect::DetectOptions exact() { return ChordDetect::DetectOptions(); }

} // namespace


class ChordDetectTest : public QObject
{
	Q_OBJECT
private slots:

	/*! THE KNOWN CLIP, in the small: a C major triad at tick 0. The name must be
	 *  the vocabulary's "Major", the root C, the bass the lowest key, and the
	 *  fit EXACT - missing and extra both zero. */
	void aCMajorTriadIsNamedExactly()
	{
		NoteVector notes = chordtest::notes({{60, 0, 96}, {64, 0, 96}, {67, 0, 96}});
		const std::vector<ChordDetect::ChordMatch> matches = ChordDetect::detectChords(notes, exact());
		freeNotes(notes);

		QCOMPARE(static_cast<int>(matches.size()), 1);
		const ChordDetect::ChordMatch& match = matches[0];
		QCOMPARE(match.pos, 0);
		QCOMPARE(match.length, 96);
		QCOMPARE(match.notes, 3);
		QCOMPARE(match.bass, 60);
		QCOMPARE(match.root, 0);
		QCOMPARE(match.rootKey, 60);
		QCOMPARE(match.chord, QStringLiteral("Major"));
		QCOMPARE(match.scale, QStringLiteral("Major"));
		QCOMPARE(match.missing, 0);
		QCOMPARE(match.extra, 0);
		QVERIFY(match.exact);
	}

	/*! The same tones in ANY order and ANY octave are the same chord: the
	 *  identity is a set of pitch classes, so an inversion is named by the
	 *  chord's root and NOT by its bass - and the bass is reported separately,
	 *  which is what lets a caller see the inversion. */
	void inversionsAndOctavesNameTheSameChord()
	{
		NoteVector inverted = chordtest::notes({{64, 0, 96}, {67, 0, 96}, {72, 0, 96}});
		const std::vector<ChordDetect::ChordMatch> first = ChordDetect::detectChords(inverted, exact());
		freeNotes(inverted);

		QCOMPARE(static_cast<int>(first.size()), 1);
		QCOMPARE(first[0].chord, QStringLiteral("Major"));
		QCOMPARE(first[0].root, 0);
		QCOMPARE(first[0].bass, 64);      // the third is the bass ...
		QCOMPARE(first[0].rootKey, 60);   // ... and the root is still C4
		QVERIFY(first[0].exact);
	}

	//! The vocabulary's own qualities, one case each, at their own roots.
	void theVocabularysOwnQualitiesAreNamed()
	{
		struct Case
		{
			int root;             // the root's pitch class
			int rootKey;          // the key the root sounds on
			std::initializer_list<int> keys;
			const char* name;
		};
		const Case cases[] = {
			{0, 60, {60, 64, 67}, "Major"},
			{0, 60, {60, 63, 67}, "minor"},
			{0, 60, {60, 63, 66}, "minb5"},
			{0, 60, {60, 64, 67, 71}, "Maj7"},
			{7, 67, {67, 71, 74, 77}, "7"},       // G7
			{9, 69, {69, 72, 76}, "minor"},       // A minor
			{2, 62, {62, 65, 69, 72}, "m7"},      // Dm7
			{0, 60, {60, 64, 67, 71, 74}, "Maj9"},
		};
		for (const Case& one : cases)
		{
			NoteVector notes = chordtest::notes({});
			for (const int key : one.keys) { notes.push_back(new Note(TimePos(96), TimePos(0), key, 100)); }
			const std::vector<ChordDetect::ChordMatch> matches = ChordDetect::detectChords(notes, exact());
			freeNotes(notes);

			QCOMPARE(static_cast<int>(matches.size()), 1);
			QCOMPARE(matches[0].chord, QString::fromLatin1(one.name));
			QCOMPARE(matches[0].root, one.root);
			QCOMPARE(matches[0].rootKey, one.rootKey);
			QVERIFY2(matches[0].exact, qPrintable(QStringLiteral("%1 is not an exact fit")
				.arg(QString::fromLatin1(one.name))));
		}
	}

	/*! A slice that is NOT exactly a chord still gets the nearest entry, with
	 *  the gap REPORTED: C-E-G with an added F# is a Major triad with one extra
	 *  tone, not a silent "Maj7b5" and not a refusal. This is the case that
	 *  makes `exact` worth reporting at all. */
	void anUnexactFitReportsWhatIsMissingAndExtra()
	{
		NoteVector notes = chordtest::notes({{60, 0, 96}, {64, 0, 96}, {66, 0, 96}, {67, 0, 96}});
		const std::vector<ChordDetect::ChordMatch> matches = ChordDetect::detectChords(notes, exact());
		freeNotes(notes);

		QCOMPARE(static_cast<int>(matches.size()), 1);
		QCOMPARE(matches[0].chord, QStringLiteral("Major"));
		QCOMPARE(matches[0].root, 0);
		QCOMPARE(matches[0].missing, 0);
		QCOMPARE(matches[0].extra, 1); // the F#
		QVERIFY2(!matches[0].exact, "a superset of a triad was reported as an exact fit");

		// And a slice that is a subset of nothing in the table: two tones a
		// semitone apart are not a chord, but they are still SLICED and the
		// nearest entry is named with its gap visible.
		NoteVector cluster = chordtest::notes({{60, 0, 96}, {61, 0, 96}});
		const std::vector<ChordDetect::ChordMatch> clusterMatches =
			ChordDetect::detectChords(cluster, exact());
		freeNotes(cluster);
		QCOMPARE(static_cast<int>(clusterMatches.size()), 1);
		QVERIFY(!clusterMatches[0].chord.isEmpty());
		QVERIFY(!clusterMatches[0].exact);
	}

	/*! Slices are what SOUND TOGETHER: two triads a bar apart are two chords in
	 *  ascending position order, and a single note is not a chord at all (the
	 *  table's one-tone "octave" entry is skipped - the default min of two
	 *  distinct pitch classes). */
	void slicesAreWhatSoundsTogether()
	{
		NoteVector notes = chordtest::notes({{60, 0, 96}, {64, 0, 96}, {67, 0, 96},
			{65, 192, 96}, {69, 192, 96}, {72, 192, 96}, {55, 384, 96}});
		const std::vector<ChordDetect::ChordMatch> matches = ChordDetect::detectChords(notes, exact());
		freeNotes(notes);

		QCOMPARE(static_cast<int>(matches.size()), 2);
		QCOMPARE(matches[0].pos, 0);
		QCOMPARE(matches[0].chord, QStringLiteral("Major"));
		QCOMPARE(matches[0].root, 0);
		QCOMPARE(matches[1].pos, 192);
		// F-A-C is an F major triad: the root follows the bass here, because
		// that is what the tones spell.
		QCOMPARE(matches[1].chord, QStringLiteral("Major"));
		QCOMPARE(matches[1].root, 5);
		QCOMPARE(matches[1].rootKey, 65);
	}

	/*! A strummed take needs a WINDOW, and the window is measured from each
	 *  slice's FIRST note: a run of notes 6 ticks apart with a 12-tick window is
	 *  one slice, and with the default window of 0 it is three slices of one
	 *  note each - which the default min of two pitch classes then drops. */
	void aWindowGroupsAStrummedTake()
	{
		NoteVector notes = chordtest::notes({{60, 0, 96}, {64, 6, 90}, {67, 12, 84}});

		ChordDetect::DetectOptions windowed;
		windowed.windowTicks = 12;
		const std::vector<ChordDetect::ChordMatch> grouped =
			ChordDetect::detectChords(notes, windowed);
		QCOMPARE(static_cast<int>(grouped.size()), 1);
		QCOMPARE(grouped[0].chord, QStringLiteral("Major"));
		QCOMPARE(grouped[0].notes, 3);
		QCOMPARE(grouped[0].length, 96); // the last note's end, from the slice's start

		const std::vector<ChordDetect::ChordMatch> ungrouped =
			ChordDetect::detectChords(notes, exact());
		QVERIFY2(ungrouped.empty(), "three notes on three ticks were grouped with no window");
		freeNotes(notes);
	}

	/*! The KEY: the root is the first note's pitch class (a stated rule, so the
	 *  answer is reproducible) and the scale is the most SPECIFIC entry rooted
	 *  there that covers every class - so a C major run reports "Major", not
	 *  "Chromatic". */
	void theKeyIsTheMostSpecificCoveringScale()
	{
		NoteVector major = chordtest::notes({{60, 0, 96}, {62, 96, 96}, {64, 192, 96}, {65, 288, 96},
			{67, 384, 96}, {69, 480, 96}, {71, 576, 96}});
		const ChordDetect::KeyEstimate key = ChordDetect::detectKey(major);
		freeNotes(major);
		QCOMPARE(key.scale, QStringLiteral("Major"));
		QCOMPARE(key.root, 0);
		QCOMPARE(key.rootKey, 60);
		QCOMPARE(key.pitchClasses, 7);
		QVERIFY(key.complete);

		// A run that no seven-note scale covers still gets an answer: the
		// chromatic entry covers everything, and the entry CHOSEN is the most
		// specific one that does.
		NoteVector odd = chordtest::notes({{60, 0, 96}, {61, 96, 96}, {63, 192, 96}, {64, 288, 96},
			{66, 384, 96}, {68, 480, 96}, {70, 576, 96}});
		const ChordDetect::KeyEstimate oddKey = ChordDetect::detectKey(odd);
		freeNotes(odd);
		QVERIFY(!oddKey.scale.isEmpty());
		QCOMPARE(oddKey.pitchClasses, 7);

		// An empty note list has no key at all, and says so rather than guessing.
		NoteVector none;
		const ChordDetect::KeyEstimate noneKey = ChordDetect::detectKey(none);
		QVERIFY(noneKey.scale.isEmpty());
		QCOMPARE(noneKey.root, -1);
		QVERIFY(!noneKey.complete);
	}

	//! The same input twice is the same output: a detection holds no state.
	void detectionIsRepeatable()
	{
		NoteVector notes = chordtest::notes({{60, 0, 96}, {64, 0, 96}, {67, 0, 96}, {71, 0, 96}});
		const std::vector<ChordDetect::ChordMatch> first = ChordDetect::detectChords(notes, exact());
		const std::vector<ChordDetect::ChordMatch> second = ChordDetect::detectChords(notes, exact());
		freeNotes(notes);

		QCOMPARE(static_cast<int>(first.size()), static_cast<int>(second.size()));
		QCOMPARE(first[0].chord, second[0].chord);
		QCOMPARE(first[0].root, second[0].root);
		QCOMPARE(first[0].rootKey, second[0].rootKey);
	}
};

QTEST_GUILESS_MAIN(ChordDetectTest)
#include "ChordDetectTest.moc"
