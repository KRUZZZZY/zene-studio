/*
 * ChordTrackTest.cpp - the chord TRACK's own arithmetic: ordering, replace-by-
 *                      position, the bounds, "0 means hold", and the
 *                      <chord-track> element.
 *
 * The surface is held to account by ControlChordCommandsTest.cpp; this file is
 * the engine half, and most of it needs no Engine at all (the track is a value
 * with an XML form, which is the point of it being a plain class rather than a
 * QObject in the Song).
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

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QVector>

#include "ChordTrack.h"
#include "ChordVocabulary.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;

namespace
{

ChordEvent eventAt(tick_t pos, const QString& chord, int root = 0, int octave = 4,
	tick_t length = 0)
{
	ChordEvent event;
	event.pos = pos;
	event.length = length;
	event.root = root;
	event.octave = octave;
	event.chord = chord;
	return event;
}

//! The track as a comparable list of positions, so an ordering assertion prints
//! the order rather than the whole object.
QString orderOf(const ChordTrack& track)
{
	QStringList parts;
	for (const ChordEvent& event : track.events()) { parts << QString::number(event.pos); }
	return parts.join(QStringLiteral(","));
}

} // namespace


class ChordTrackTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! An empty track is the engine as it was before the feature: nothing to
	//! persist, nothing to read.
	void anEmptyTrackPersistsNothing()
	{
		ChordTrack track;
		QVERIFY(track.empty());
		QCOMPARE(track.size(), 0);
		QVERIFY2(!track.shouldPersist(), "an empty track asked to be written to the project");
		QVERIFY(track.events().empty());
	}

	/*! The ordering invariant and the replace rule: events are kept in
	 *  ascending position order whatever order they were set in, and a set at a
	 *  position that already holds a chord REPLACES it - the position is the
	 *  key, exactly as a groove's name is. */
	void setKeepsOrderAndReplacesAtTheSamePosition()
	{
		ChordTrack track;
		bool replaced = true;
		QVERIFY(track.set(eventAt(192, QStringLiteral("minor")), &replaced));
		QVERIFY2(!replaced, "a fresh position reported a replace");
		QVERIFY(track.set(eventAt(0, QStringLiteral("Major")), &replaced));
		QVERIFY(!replaced);
		QVERIFY(track.set(eventAt(96, QStringLiteral("sus4")), &replaced));
		QVERIFY(!replaced);
		QCOMPARE(orderOf(track), QStringLiteral("0,96,192"));
		QCOMPARE(track.size(), 3);

		// The same position again: replaced, and the track does not grow.
		QVERIFY(track.set(eventAt(96, QStringLiteral("7"), 7), &replaced));
		QVERIFY2(replaced, "a set at an occupied position did not report a replace");
		QCOMPARE(track.size(), 3);
		QCOMPARE(orderOf(track), QStringLiteral("0,96,192"));
		QCOMPARE(track.at(1).chord, QStringLiteral("7"));
		QCOMPARE(track.at(1).root, 7);

		QCOMPARE(track.indexAt(96), 1);
		QCOMPARE(track.indexAt(97), -1);
		QVERIFY(track.findAt(96) != nullptr);
		QVERIFY(track.findAt(97) == nullptr);
	}

	/*! "Length 0 means HOLD": an event sounds until the next one, and the LAST
	 *  one needs the caller's own fallback - a read of the track's geometry, not
	 *  of one event. */
	void holdMeansUntilTheNextEvent()
	{
		ChordTrack track;
		QVERIFY(track.set(eventAt(0, QStringLiteral("Major"))));
		QVERIFY(track.set(eventAt(96, QStringLiteral("minor"))));
		QVERIFY(track.set(eventAt(192, QStringLiteral("7"), 7, 4, 48)));
		QCOMPARE(track.effectiveLength(0, 192), 96);
		QCOMPARE(track.effectiveLength(1, 192), 96);
		// The last event's own length when it has one ...
		QCOMPARE(track.effectiveLength(2, 192), 48);
		// ... and the fallback when it does not.
		QVERIFY(track.set(eventAt(288, QStringLiteral("sus2"), 5)));
		QCOMPARE(track.effectiveLength(3, 192), 192);
		QCOMPARE(track.effectiveLength(9, 192), 192);
	}

	/*! A name this engine cannot resolve is REFUSED rather than stored: a chord
	 *  track holds vocabulary names, so nothing downstream has to guess. The
	 *  scale name is optional, and when it is there it must be a SCALE entry -
	 *  "Major" is both a chord and a scale of the table, and is accepted as
	 *  either. */
	void aNameOutsideTheVocabularyIsRefused()
	{
		QString reason;
		QVERIFY2(ChordTrack::isWritable(eventAt(0, QStringLiteral("Maj7")), &reason), qPrintable(reason));
		QVERIFY2(ChordTrack::isWritable(eventAt(0, QStringLiteral("minor")), &reason), qPrintable(reason));

		reason.clear();
		QVERIFY2(!ChordTrack::isWritable(eventAt(0, QStringLiteral("not-a-chord")), &reason),
			"a name outside the vocabulary was accepted");
		QVERIFY(!reason.isEmpty());

		ChordEvent withScale = eventAt(0, QStringLiteral("Major"));
		withScale.scale = QStringLiteral("Dorian");
		QVERIFY2(ChordTrack::isWritable(withScale, &reason), qPrintable(reason));
		withScale.scale = QStringLiteral("no-such-scale");
		QVERIFY2(!ChordTrack::isWritable(withScale, &reason), "a scale outside the vocabulary was accepted");

		// The range checks, one each.
		QVERIFY(!ChordTrack::isWritable(eventAt(-1, QStringLiteral("Major"))));
		QVERIFY(!ChordTrack::isWritable(eventAt(0, QStringLiteral("Major"), 12)));
		QVERIFY(!ChordTrack::isWritable(eventAt(0, QStringLiteral("Major"), 0, 10)));
		QVERIFY(!ChordTrack::isWritable(eventAt(0, QStringLiteral("Major"), 0, 4, -1)));
	}

	//! The bound is on NEW positions only: replacing an existing one is always
	//! allowed, so a full track is still editable.
	void theMaxEventsBoundRefusesNewPositionsOnly()
	{
		ChordTrack track;
		for (int index = 0; index < ChordTrack::MaxEvents; ++index)
		{
			QVERIFY2(track.set(eventAt(index, QStringLiteral("Major"))),
				qPrintable(QStringLiteral("event %1 was refused below the bound").arg(index)));
		}
		QCOMPARE(track.size(), ChordTrack::MaxEvents);
		QVERIFY2(!track.set(eventAt(ChordTrack::MaxEvents, QStringLiteral("Major"))),
			"the track accepted an event past MaxEvents");
		QCOMPARE(track.size(), ChordTrack::MaxEvents);

		bool replaced = false;
		QVERIFY2(track.set(eventAt(3, QStringLiteral("7"), 7), &replaced),
			"a full track refused to REPLACE an existing event");
		QVERIFY(replaced);
		QCOMPARE(track.size(), ChordTrack::MaxEvents);
	}

	//! removeAt is by position, and a position that holds nothing is a false.
	void removeIsByPosition()
	{
		ChordTrack track;
		QVERIFY(track.set(eventAt(0, QStringLiteral("Major"))));
		QVERIFY(track.set(eventAt(96, QStringLiteral("minor"))));
		QVERIFY(!track.removeAt(95));
		QCOMPARE(track.size(), 2);
		QVERIFY(track.removeAt(0));
		QCOMPARE(orderOf(track), QStringLiteral("96"));
	}

	//! The XML form is the whole entity: a round trip restores every field, byte
	//! for byte, which is what the recorded action checkpoint depends on.
	void theXmlRoundTripIsExact()
	{
		ChordTrack track;
		ChordEvent first = eventAt(0, QStringLiteral("Maj7"), 0, 4, 96);
		first.scale = QStringLiteral("Major");
		QVERIFY(track.set(first));
		QVERIFY(track.set(eventAt(96, QStringLiteral("9"), 7, 3, 48)));
		const QString xml = track.toXml();
		QVERIFY(xml.contains(QStringLiteral("<chord-track")));

		ChordTrack restored;
		QVERIFY(restored.fromXml(xml));
		QCOMPARE(restored.size(), 2);
		QCOMPARE(restored.at(0).chord, QStringLiteral("Maj7"));
		QCOMPARE(restored.at(0).root, 0);
		QCOMPARE(restored.at(0).octave, 4);
		QCOMPARE(restored.at(0).length, 96);
		QCOMPARE(restored.at(0).scale, QStringLiteral("Major"));
		QCOMPARE(restored.at(1).chord, QStringLiteral("9"));
		QCOMPARE(restored.at(1).root, 7);
		QCOMPARE(restored.at(1).octave, 3);
		QCOMPARE(restored.toXml(), xml);
	}

	/*! A number that does not parse is not a zero: an unreadable event is
	 *  SKIPPED, like an unresolvable chord name. Defaulting instead would
	 *  silently invent a different, perfectly valid chord - a malformed `pos`
	 *  becomes a chord at tick 0, a malformed octave becomes octave 4. */
	void malformedNumbersAreSkipped()
	{
		ChordTrack track;
		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral(
			"<chord-track><chord chord=\"Major\"/></chord-track>")));  // no pos at all
		QVERIFY(track.loadSettings(document.documentElement()));
		QVERIFY2(track.empty(), "a chord element with no position became a chord at tick 0");

		QDomDocument garbage;
		QVERIFY(garbage.setContent(QStringLiteral(
			"<chord-track><chord pos=\"abc\" chord=\"Major\"/></chord-track>")));
		QVERIFY(track.loadSettings(garbage.documentElement()));
		QVERIFY2(track.empty(), "a malformed position became a chord at tick 0");

		QDomDocument partial;
		QVERIFY(partial.setContent(QStringLiteral(
			"<chord-track><chord pos=\"96\" octave=\"x\" chord=\"Major\"/>"
			"<chord pos=\"192\" root=\"?\" chord=\"minor\"/></chord-track>")));
		QVERIFY(track.loadSettings(partial.documentElement()));
		QVERIFY2(track.empty(),
			"a malformed octave or root became a different valid chord at the right tick");

		// The neighbours of a malformed event are still read: one bad element
		// must not cost the whole track.
		QDomDocument mixed;
		QVERIFY(mixed.setContent(QStringLiteral(
			"<chord-track><chord pos=\"0\" chord=\"Major\"/><chord pos=\"nope\" chord=\"minor\"/>"
			"<chord pos=\"192\" chord=\"minor\"/></chord-track>")));
		QVERIFY(track.loadSettings(mixed.documentElement()));
		QCOMPARE(track.size(), 2);
		QCOMPARE(orderOf(track), QStringLiteral("0,192"));
	}

	/*! CLEARED FIRST, unconditionally: the state a restore must be able to
	 *  reach is "no chord track at all", so an absent or foreign element has to
	 *  leave an EMPTY track rather than the previous project's chords. */
	void loadSettingsClearsFirst()
	{
		ChordTrack track;
		QVERIFY(track.set(eventAt(0, QStringLiteral("Major"))));
		QVERIFY(track.set(eventAt(96, QStringLiteral("minor"))));

		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral("<song><bpm>140</bpm></song>")));
		QVERIFY2(!track.loadSettings(document.documentElement()),
			"a <song> element was accepted as a chord track");
		QVERIFY2(track.empty(), "a foreign element left the previous chords behind");

		QVERIFY(track.set(eventAt(0, QStringLiteral("Major"))));
		QDomDocument foreign;
		QVERIFY(foreign.setContent(QStringLiteral("<chord-track><chord pos=\"0\" chord=\"nope\"/></chord-track>")));
		QVERIFY(track.loadSettings(foreign.documentElement()));
		QVERIFY2(track.empty(),
			"an event whose name this build cannot resolve was kept instead of skipped");
	}

	//! A chord track is project state: the Song holds it, and a new project does
	//! not inherit the previous one's chords.
	void theSongOwnsTheTrackAndANewProjectClearsIt()
	{
		Song* song = Engine::getSong();
		QVERIFY(song != nullptr);
		song->chordTrack().clear();
		QVERIFY(song->chordTrack().empty());

		QVERIFY(song->chordTrack().set(eventAt(0, QStringLiteral("Major"))));
		QCOMPARE(song->chordTrack().size(), 1);

		song->clearProject();
		QVERIFY2(song->chordTrack().empty(),
			"a new project inherited the previous project's chord track");
	}

	/*! A malformed follower track ID is SKIPPED, not track 0: the same rule the
	 *  malformed event numbers follow - a number that does not parse is not a
	 *  zero, and 0 is a perfectly valid track ID, so defaulting would silently
	 *  make the first track follow the scale. */
	void malformedFollowerTrackIdsAreSkipped()
	{
		ChordTrack track;
		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral(
			"<chord-track><follow track=\"garbage\" mode=\"scale\"/></chord-track>")));
		QVERIFY(track.loadSettings(document.documentElement()));
		QVERIFY2(track.followers().empty(),
			"a malformed follower track ID became track 0");
		QCOMPARE(track.followOf(0), ChordFollowMode::Off);

		// The neighbours of a malformed follower are still read: one bad element
		// must not cost the whole track.
		QDomDocument mixed;
		QVERIFY(mixed.setContent(QStringLiteral(
			"<chord-track><follow track=\"3\" mode=\"chord\"/><follow track=\"nope\" mode=\"scale\"/>"
			"<follow track=\"5\" mode=\"scale\"/></chord-track>")));
		QVERIFY(track.loadSettings(mixed.documentElement()));
		QCOMPARE(track.followers().size(), 2);
		QCOMPARE(track.followOf(3), ChordFollowMode::Chord);
		QCOMPARE(track.followOf(5), ChordFollowMode::Scale);
		QCOMPARE(track.followOf(0), ChordFollowMode::Off);
	}

	//! 0.5.0 E23, chord follow: a follower's note moves to the nearest tone (down on a tie) of the chord - or
	//! its scale - sounding where it starts; a track that does not follow, and a silent stretch, play as written.
	void followedKeySnapsToTheSoundingChord()
	{
		ChordTrack track;
		ChordEvent cMajor = eventAt(0, QStringLiteral("Major"), 0, 4, 192);
		cMajor.scale = QStringLiteral("Major");
		QVERIFY(track.set(cMajor));
		QVERIFY(track.set(eventAt(192, QStringLiteral("minor"), 9, 4, 192)));  // A minor (the chord is "minor"; "Minor" is the scale)
		QVERIFY(track.setFollow(7, ChordFollowMode::Chord));
		QVERIFY(track.setFollow(8, ChordFollowMode::Scale));

		QCOMPARE(track.followedKey(7, 61, 10), 60);   // C# -> C, one down
		QCOMPARE(track.followedKey(7, 66, 10), 67);   // F# -> G: the nearest chord tone
		QCOMPARE(track.followedKey(7, 64, 10), 64);   // a chord tone stays
		QCOMPARE(track.followedKey(8, 66, 10), 65);   // scale mode: F# -> F (F and G tie; down wins)
		QCOMPARE(track.followedKey(7, 62, 200), 60);  // A minor {A,C,E}: D -> C (C and E tie two away; down wins)
		QCOMPARE(track.followedKey(9, 61, 10), 61);   // not a follower
		QCOMPARE(track.followedKey(7, 61, 500), 61);  // past the last chord: silent, as written
	}

	//! The followers are part of the chord track: saved as <follow> children, restored, kept by clearEvents,
	//! dropped by clear, and a track that only has followers still persists.
	void followersPersistWithTheTrack()
	{
		ChordTrack track;
		QVERIFY(!track.setFollow(-1, ChordFollowMode::Chord));
		QVERIFY(track.setFollow(4, ChordFollowMode::Scale));
		QVERIFY(track.shouldPersist());
		QVERIFY(track.set(eventAt(0, QStringLiteral("Major"))));

		ChordTrack copy;
		QVERIFY(copy.fromXml(track.toXml()));
		QCOMPARE(copy.followOf(4), ChordFollowMode::Scale);
		QCOMPARE(copy.size(), 1);

		const unsigned revision = copy.revision();
		copy.clearEvents();
		QVERIFY(copy.revision() != revision);
		QCOMPARE(copy.followOf(4), ChordFollowMode::Scale);
		QVERIFY(copy.setFollow(4, ChordFollowMode::Off));
		QCOMPARE(copy.followOf(4), ChordFollowMode::Off);
		QVERIFY(copy.followers().empty());

		track.clear();
		QVERIFY(track.followers().empty());
		QVERIFY(!track.shouldPersist());
		ChordFollowMode mode = ChordFollowMode::Off;
		QVERIFY(ChordTrack::followModeFromName(QStringLiteral("scale"), &mode));
		QCOMPARE(mode, ChordFollowMode::Scale);
		QVERIFY(!ChordTrack::followModeFromName(QStringLiteral("loud"), &mode));
	}

	void vocabularyCountsAreThePianoRollsOwn()
	{
		// The vocabulary is the pre-existing table: 95 entries, split by
		// isScale() (more than six tones is a scale). Asserted here so a change
		// to that table is visible in this feature's own test rather than only
		// in the piano roll's.
		QCOMPARE(ChordVocabulary::chordNames().size() + ChordVocabulary::scaleNames().size(), 95);
		QVERIFY(ChordVocabulary::chordNames().contains(QStringLiteral("Maj7")));
		QVERIFY(ChordVocabulary::scaleNames().contains(QStringLiteral("Dorian")));
		QVERIFY2(!ChordVocabulary::chordNames().contains(QStringLiteral("Dorian")),
			"a scale leaked into the chord list");
		QVERIFY2(!ChordVocabulary::scaleNames().contains(QStringLiteral("Maj7")),
			"a chord leaked into the scale list");
		// "Major" is BOTH: the chord {0,4,7} and the scale of seven tones.
		QVERIFY(ChordVocabulary::chordByName(QStringLiteral("Major")) != nullptr);
		QVERIFY(ChordVocabulary::scaleByName(QStringLiteral("Major")) != nullptr);
		QCOMPARE(ChordVocabulary::chordByName(QStringLiteral("Major"))->size(), 3);
		QCOMPARE(ChordVocabulary::scaleByName(QStringLiteral("Major"))->size(), 7);
		// The key convention: C4 is middle C, the engine's own (Note.h).
		QCOMPARE(ChordVocabulary::keyOf(0, 4), 60);
		QCOMPARE(ChordVocabulary::keyOf(7, 4), 67);
		QCOMPARE(ChordVocabulary::keyOf(0, 5), 72);
	}
};

QTEST_GUILESS_MAIN(ChordTrackTest)
#include "ChordTrackTest.moc"
