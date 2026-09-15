/*
 * ChordProgressionTest.cpp - the catalogue, the chords a walk builds, and the
 *                            SEEDED generator's repeatability pair.
 *
 * THE TWO CLAIMS THIS FILE EXISTS FOR:
 *
 *   1. the chords of a progression are built from the SCALE's own tones and
 *      named by the vocabulary this product already has - "I-V-vi-IV" in C
 *      major is Major / Major / minor / Major, and that is asserted by name;
 *   2. the generator is SEEDED and REPEATABLE, so the repeatability pair
 *      holds: the same request with the same seed produces an IDENTICAL note
 *      list, and the same request with a different seed produces a DIFFERENT
 *      one - while `variation` 0 draws nothing at all and the seed decides
 *      nothing (stated, and asserted, so the seed's reach is visible).
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
#include <QStringList>
#include <QVector>

#include "ChordProgression.h"

using namespace lmms;
using namespace ChordProgression;

namespace
{

QString nameOf(const QString& progression, const QString& scale, int root, int octave = 4)
{
	Request request;
	request.progression = progression;
	request.scale = scale;
	request.root = root;
	request.octave = octave;
	// ONE PASS of the walk: `steps` defaults to 4 in the struct, which would
	// repeat a three-chord progression's first chord.
	if (const Entry* entry = find(progression))
	{
		request.steps = static_cast<int>(entry->degrees.size());
	}
	std::vector<ChordStep> chords;
	QString error;
	if (!chordsFor(request, &chords, &error)) { return QStringLiteral("<%1>").arg(error); }
	QStringList names;
	for (const ChordStep& step : chords) { names << step.name; }
	return names.join(QStringLiteral(","));
}

QString keysOf(const std::vector<NoteSpec>& notes)
{
	QStringList parts;
	for (const NoteSpec& note : notes)
	{
		parts << QStringLiteral("%1@%2:%3/v%4").arg(note.key).arg(note.pos)
			.arg(note.length).arg(note.velocity);
	}
	return parts.join(QStringLiteral(" "));
}

} // namespace


class ChordProgressionTest : public QObject
{
	Q_OBJECT
private slots:

	//! The catalogue is data, so it is checked as data: names are unique and
	//! non-empty, and every degree is inside a seven-tone scale.
	void theCatalogueIsWellFormed()
	{
		QVERIFY(catalogue().size() >= 4);
		QStringList names;
		for (const Entry& entry : catalogue())
		{
			QVERIFY(!entry.name.isEmpty());
			QVERIFY2(!names.contains(entry.name), qPrintable(entry.name + " appears twice"));
			names << entry.name;
			QVERIFY2(!entry.degrees.empty(), qPrintable(entry.name + " walks no degree"));
			QVERIFY(static_cast<int>(entry.degrees.size()) <= MaxDegreeCount);
			for (const int degree : entry.degrees)
			{
				QVERIFY2(degree >= 0 && degree < 7, qPrintable(QStringLiteral("%1 walks degree %2")
					.arg(entry.name).arg(degree)));
			}
		}
		QCOMPARE(progressionNames(), names);
		QVERIFY(find(QStringLiteral("I-V-vi-IV")) != nullptr);
		QVERIFY(find(QStringLiteral("no-such-progression")) == nullptr);
	}

	/*! CLAIM 1: the chords are the SCALE's own, named by the pre-existing
	 *  vocabulary. I-V-vi-IV in C major is Major / Major / minor / Major, at
	 *  the roots C / G / A / F - the walk is the degrees and the quality is
	 *  derived, not tabulated. */
	void aWalkIsNamedFromTheScaleItWalks()
	{
		QCOMPARE(nameOf(QStringLiteral("I-V-vi-IV"), QStringLiteral("Major"), 0),
			QStringLiteral("Major,Major,minor,Major"));
		QCOMPARE(nameOf(QStringLiteral("ii-V-I"), QStringLiteral("Major"), 0),
			QStringLiteral("minor,Major,Major"));
		QCOMPARE(nameOf(QStringLiteral("I-IV-V-I"), QStringLiteral("Major"), 0),
			QStringLiteral("Major,Major,Major,Major"));
		// The SAME entry over another scale is that scale's walk: i-iv-v-i in
		// A minor is three minor triads and the tonic again.
		QCOMPARE(nameOf(QStringLiteral("i-iv-v-i"), QStringLiteral("Minor"), 9),
			QStringLiteral("minor,minor,minor,minor"));

		// And the roots and keys are the scale's own arithmetic.
		Request request;
		request.progression = QStringLiteral("I-V-vi-IV");
		request.scale = QStringLiteral("Major");
		request.root = 0;
		std::vector<ChordStep> chords;
		QString error;
		QVERIFY2(chordsFor(request, &chords, &error), qPrintable(error));
		QCOMPARE(static_cast<int>(chords.size()), 4);
		QCOMPARE(chords[0].degree, 0);
		QCOMPARE(chords[0].root, 0);
		QCOMPARE(chords[0].rootKey, 60);
		QCOMPARE(chords[1].degree, 4);
		QCOMPARE(chords[1].root, 7);
		QCOMPARE(chords[1].rootKey, 67);
		QCOMPARE(chords[2].degree, 5);
		QCOMPARE(chords[2].root, 9);
		QCOMPARE(chords[2].rootKey, 69);
		QCOMPARE(chords[3].degree, 3);
		QCOMPARE(chords[3].root, 5);
		QCOMPARE(chords[3].rootKey, 65);
		QCOMPARE(chords[0].keys.size(), 3u);
		QCOMPARE(chords[0].keys[0], 60);
		QCOMPARE(chords[0].keys[1], 64);
		QCOMPARE(chords[0].keys[2], 67);
	}

	//! The names a request must prove: a scale and a progression this engine
	//! does not have are refusals with the vocabulary's own counts in them.
	void theNamesAndBoundsAreRefused()
	{
		Request request;
		std::vector<ChordStep> chords;
		QString error;

		request.progression = QStringLiteral("nope");
		QVERIFY(!chordsFor(request, &chords, &error));
		QVERIFY(error.contains(QStringLiteral("no progression called")));

		request.progression = QStringLiteral("I-V-vi-IV");
		request.scale = QStringLiteral("nope");
		QVERIFY(!chordsFor(request, &chords, &error));
		QVERIFY(error.contains(QStringLiteral("not a scale")));

		request.scale = QStringLiteral("Major");
		request.steps = 0;
		QVERIFY(!chordsFor(request, &chords, &error));
		request.steps = MaxSteps + 1;
		QVERIFY(!chordsFor(request, &chords, &error));
		request.steps = 4;
		request.stepTicks = 0;
		QVERIFY(!chordsFor(request, &chords, &error));
		request.stepTicks = DefaultTicksPerBar;
		request.velocity = MaxVolume + 1;
		QVERIFY(!chordsFor(request, &chords, &error));
		request.velocity = DefaultVolume;
		request.variation = 2.0f;
		QVERIFY(!chordsFor(request, &chords, &error));
	}

	/*! CLAIM 2, first half: the SAME REQUEST AND SEED IS THE SAME TAKE, note
	 *  for note - positions, lengths, keys and velocities. */
	void theSameSeedIsTheSameTake()
	{
		Request request;
		request.progression = QStringLiteral("I-V-vi-IV");
		request.scale = QStringLiteral("Major");
		request.root = 0;
		request.steps = 4;
		request.stepTicks = 192;
		request.seed = 7;
		request.variation = 0.5f;

		std::vector<NoteSpec> first;
		std::vector<NoteSpec> second;
		QString error;
		QVERIFY2(generateNotes(request, &first, &error), qPrintable(error));
		QVERIFY2(generateNotes(request, &second, &error), qPrintable(error));
		QCOMPARE(static_cast<int>(first.size()), 12);
		QCOMPARE(keysOf(first), keysOf(second));

		// ... and it is a WHOLE VALUE, so the equality above is not two empty
		// vectors agreeing.
		QVERIFY(!keysOf(first).isEmpty());
	}

	/*! CLAIM 2, second half: A DIFFERENT SEED IS A DIFFERENT TAKE, because every
	 *  drawn choice has more than one outcome (a voicing, a position and a
	 *  velocity per chord). */
	void aDifferentSeedIsADifferentTake()
	{
		Request request;
		request.progression = QStringLiteral("I-V-vi-IV");
		request.scale = QStringLiteral("Major");
		request.root = 0;
		request.steps = 4;
		request.stepTicks = 192;
		request.variation = 0.5f;

		request.seed = 7;
		std::vector<NoteSpec> first;
		QString error;
		QVERIFY2(generateNotes(request, &first, &error), qPrintable(error));
		request.seed = 8;
		std::vector<NoteSpec> second;
		QVERIFY2(generateNotes(request, &second, &error), qPrintable(error));

		QVERIFY2(keysOf(first) != keysOf(second),
			qPrintable(QStringLiteral("two seeds produced the same take: %1").arg(keysOf(first))));
	}

	/*! And the seed's REACH is stated rather than implied: with `variation` 0
	 *  nothing is drawn, so two seeds produce the SAME take - the progression
	 *  itself, exactly on the grid. */
	void withNoVariationTheSeedDecidesNothing()
	{
		Request request;
		request.progression = QStringLiteral("I-V-vi-IV");
		request.scale = QStringLiteral("Major");
		request.steps = 4;
		request.variation = 0.0f;

		request.seed = 1;
		std::vector<NoteSpec> first;
		QString error;
		QVERIFY2(generateNotes(request, &first, &error), qPrintable(error));
		request.seed = 2;
		std::vector<NoteSpec> second;
		QVERIFY2(generateNotes(request, &second, &error), qPrintable(error));
		QCOMPARE(keysOf(first), keysOf(second));

		// The unvaried take is the progression itself: three tones per chord,
		// one chord per step, all of them on the grid, all at one velocity.
		QCOMPARE(static_cast<int>(first.size()), 12);
		for (const NoteSpec& note : first)
		{
			QCOMPARE(note.length, DefaultTicksPerBar);
			QCOMPARE(note.velocity, static_cast<int>(DefaultVolume));
			QCOMPARE(note.pos % DefaultTicksPerBar, 0);
		}
	}

	//! The patterns lay the same chord out differently, and every note stays
	//! inside its own step: block is simultaneous, the arpeggios walk the tones.
	void thePatternsLayAChordOut()
	{
		Request request;
		request.progression = QStringLiteral("ii-V-I");
		request.scale = QStringLiteral("Major");
		request.steps = 1;
		request.stepTicks = 96;
		std::vector<NoteSpec> notes;
		QString error;

		request.pattern = Pattern::Block;
		QVERIFY(generateNotes(request, &notes, &error));
		QCOMPARE(static_cast<int>(notes.size()), 3);
		for (const NoteSpec& note : notes)
		{
			QCOMPARE(note.pos, 0);
			QCOMPARE(note.length, 96);
		}

		request.pattern = Pattern::ArpeggioUp;
		QVERIFY(generateNotes(request, &notes, &error));
		QCOMPARE(static_cast<int>(notes.size()), 3);
		QCOMPARE(notes[0].pos, 0);
		QCOMPARE(notes[1].pos, 32);
		QCOMPARE(notes[2].pos, 64);
		QCOMPARE(notes[0].length, 32);
		QVERIFY2(notes[0].key < notes[1].key && notes[1].key < notes[2].key,
			qPrintable(keysOf(notes)));

		request.pattern = Pattern::ArpeggioDown;
		QVERIFY(generateNotes(request, &notes, &error));
		QCOMPARE(notes[0].pos, 0);
		QVERIFY2(notes[0].key > notes[1].key && notes[1].key > notes[2].key,
			qPrintable(keysOf(notes)));

		// Broken: the root held for the whole step, the tones above it in the
		// slots that follow.
		request.pattern = Pattern::Broken;
		QVERIFY(generateNotes(request, &notes, &error));
		QCOMPARE(static_cast<int>(notes.size()), 3);
		QVERIFY2(keysOf(notes).startsWith(QStringLiteral("62@0")), qPrintable(keysOf(notes)));
		QCOMPARE(notes[0].length, 96);
		QCOMPARE(notes[1].pos, 32);
		QCOMPARE(notes[2].pos, 64);
	}

	//! layOutChord is the ONE definition both generators use, so it is asserted
	//! on its own: no note ever runs past the room it is given.
	void layOutChordRespectsItsRoom()
	{
		const std::vector<int> keys{60, 64, 67};
		std::vector<NoteSpec> notes;
		layOutChord(keys, 10, 20, Pattern::Block, 100, &notes);
		QCOMPARE(static_cast<int>(notes.size()), 3);
		for (const NoteSpec& note : notes)
		{
			QCOMPARE(note.pos, 10);
			QCOMPARE(note.length, 20);
		}

		notes.clear();
		layOutChord(keys, 10, 20, Pattern::ArpeggioUp, 100, &notes);
		QCOMPARE(static_cast<int>(notes.size()), 3);
		// One slot per tone: 20 / 3 == 6 ticks each, so the tones start 10, 16
		// and 22 - and every one of them still ends inside the room (30).
		QCOMPARE(notes[0].pos, 10);
		QCOMPARE(notes[1].pos, 16);
		QCOMPARE(notes[2].pos, 22);
		for (const NoteSpec& note : notes)
		{
			QVERIFY2(note.pos + note.length <= 30, qPrintable(keysOf(notes)));
		}

		notes.clear();
		layOutChord({}, 0, 96, Pattern::Block, 100, &notes);
		QVERIFY2(notes.empty(), "an empty chord produced notes");
	}

	//! The progression as TRACK events: the same walk, with the vocabulary's
	//! names and the octave each root actually sounds in.
	void theWalkBecomesTrackEvents()
	{
		Request request;
		request.progression = QStringLiteral("I-V-vi-IV");
		request.scale = QStringLiteral("Major");
		request.steps = 4;
		request.stepTicks = 192;
		std::vector<ChordEvent> events;
		QString error;
		QVERIFY2(trackEventsFor(request, &events, &error), qPrintable(error));
		QCOMPARE(static_cast<int>(events.size()), 4);
		QCOMPARE(events[0].pos, 0);
		QCOMPARE(events[0].chord, QStringLiteral("Major"));
		QCOMPARE(events[0].root, 0);
		QCOMPARE(events[0].octave, 4);
		QCOMPARE(events[0].length, 192);
		QCOMPARE(events[0].scale, QStringLiteral("Major"));
		QCOMPARE(events[1].pos, 192);
		QCOMPARE(events[1].root, 7);
		QCOMPARE(events[1].chord, QStringLiteral("Major"));
		QCOMPARE(events[2].root, 9);
		QCOMPARE(events[2].chord, QStringLiteral("minor"));
		QCOMPARE(events[3].root, 5);
	}
};

QTEST_GUILESS_MAIN(ChordProgressionTest)
#include "ChordProgressionTest.moc"
