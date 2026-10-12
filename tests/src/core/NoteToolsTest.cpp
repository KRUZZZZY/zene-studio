/*
 * NoteToolsTest.cpp - invert, arpeggiate, flam, transpose in key, repeat, generate, move, split, detect, follow chords
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
/*! 0.5.0 L07 (W3): each note tool changes exactly what it says and one undo puts the clip back -
 *  invert (ED-094), arpeggiate (ED-099), flam (ED-100), transpose in key (ED-103), repeat (ED-144),
 *  generate (ED-101); notes move to another track and a clip splits by key (ED-106); the key of a
 *  C-major line is found (ED-114); notes follow the chord track (ED-117); clip positions snap to a
 *  bar (ED-138); a scale of one's own is set by name (ED-115). */

#include <QtTest>

#include <QJsonArray>
#include <QPointer>
#include <algorithm>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QList<int> keysOf(const MidiClip* clip)
{
	QList<int> out;
	for (const Note* note : clip->notes()) { out << note->key(); }
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace

class NoteToolsTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;

	MidiClip* clipWith(std::initializer_list<std::pair<int, int>> keysAt, int length = 24)
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		for (const auto& [key, at] : keysAt) { clip->addNote(Note(TimePos(length), TimePos(at), key), false); }
		return clip;
	}

	QJsonObject self(const MidiClip* clip) const { return {{QStringLiteral("clip"), control::clipIdOf(clip)}}; }

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void invertMirrorsAroundTheMiddle()
	{
		MidiClip* clip = clipWith({{60, 0}, {64, 24}, {67, 48}});
		QVERIFY(run(QStringLiteral("note.invert"), self(clip)).ok);
		QCOMPARE(keysOf(clip), (QList<int>{60, 63, 67}));  // 60 + 67 = 127: 64 -> 63
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(keysOf(clip), (QList<int>{60, 64, 67}));
	}

	void aChordBecomesARun()
	{
		MidiClip* clip = clipWith({{60, 0}, {64, 0}, {67, 0}}, 48);
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("rate"), 12);
		QVERIFY(run(QStringLiteral("note.arpeggiate"), args).ok);
		QCOMPARE(clip->notes().size(), std::size_t{4});  // 48 ticks at 12 a step
		QCOMPARE(clip->notes()[0]->key(), 60);
		QCOMPARE(clip->notes()[1]->key(), 64);
		QCOMPARE(clip->notes()[3]->key(), 60);  // the run wraps
	}

	void flamsRepeatsAndKeyedTransposition()
	{
		MidiClip* clip = clipWith({{60, 24}, {62, 48}});
		QVERIFY(run(QStringLiteral("note.flam"), self(clip)).ok);
		QCOMPARE(clip->notes().size(), std::size_t{4});
		QCOMPARE(clip->notes().front()->pos().getTicks(), tick_t{21});  // 3 ticks before
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QJsonObject repeat = self(clip);
		repeat.insert(QStringLiteral("count"), 2);
		QVERIFY(run(QStringLiteral("note.repeat"), repeat).ok);
		QCOMPARE(clip->notes().size(), std::size_t{6});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("note.transpose_in_key"), self(clip)).errorKind, ControlErrorKind::Refused);  // no scale yet
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), QStringLiteral("C")}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		QVERIFY(run(QStringLiteral("note.transpose_in_key"), self(clip)).ok);
		QCOMPARE(keysOf(clip), (QList<int>{62, 64}));  // C -> D, D -> E: one degree each
	}

	void aEuclideanRhythm()
	{
		MidiClip* clip = clipWith({});
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("steps"), 8);
		args.insert(QStringLiteral("pulses"), 3);
		QVERIFY(run(QStringLiteral("note.generate"), args).ok);
		QCOMPARE(clip->notes().size(), std::size_t{3});
		// Bjorklund's own phase: E(2,5) is x.x.. - hits on steps 0 and 2 (12 ticks a step).
		MidiClip* five = clipWith({});
		QJsonObject e25 = self(five);
		e25.insert(QStringLiteral("steps"), 5);
		e25.insert(QStringLiteral("pulses"), 2);
		QVERIFY(run(QStringLiteral("note.generate"), e25).ok);
		QCOMPARE(five->notes().size(), std::size_t{2});
		QCOMPARE(five->notes()[1]->pos().getTicks(), tick_t{24});
	}

	void notesMoveAndSplit()
	{
		MidiClip* clip = clipWith({{36, 0}, {38, 24}, {36, 48}, {42, 72}});
		auto* other = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult split = run(QStringLiteral("note.split_by_pitch"), self(clip));
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 3);  // 36, 38, 42
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
		QJsonObject move = self(clip);
		move.insert(QStringLiteral("track"), control::trackIdOf(other));
		move.insert(QStringLiteral("scope"), QStringLiteral("clip"));
		QVERIFY(run(QStringLiteral("note.move_to_track"), move).ok);
		QVERIFY(clip->notes().empty());
		QCOMPARE(dynamic_cast<MidiClip*>(other->getClips().front())->notes().size(), std::size_t{4});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->notes().size(), std::size_t{4});
	}

	void theKeyIsFoundAndChordsAreFollowed()
	{
		MidiClip* clip = clipWith({{60, 0}, {62, 24}, {64, 48}, {65, 72}, {67, 96}, {69, 120}, {71, 144}, {60, 168}});
		const ControlResult key = run(QStringLiteral("scale.detect"), self(clip));
		QVERIFY2(key.ok, qPrintable(key.errorMessage));
		QCOMPARE(key.result.value(QStringLiteral("fit")).toDouble(), 1.0);
		QCOMPARE(key.result.value(QStringLiteral("root")).toString(), QStringLiteral("C"));
		QVERIFY(run(QStringLiteral("chord.set"), {{QStringLiteral("pos"), 0}, {QStringLiteral("chord"), QStringLiteral("Major")},
			{QStringLiteral("root"), 0}}).ok);
		QVERIFY(run(QStringLiteral("note.follow_chords"), self(clip)).ok);
		for (const Note* note : clip->notes()) { QVERIFY(QList<int>({0, 4, 7}).contains(note->key() % 12)); }  // C, E, G only
		QVERIFY(run(QStringLiteral("chord.clear")).ok);
	}

	void followingChordsRefusesAnEmptySelection()
	{
		MidiClip* clip = clipWith({{61, 0}});
		QVERIFY(run(QStringLiteral("chord.set"), {{QStringLiteral("pos"), 0},
			{QStringLiteral("chord"), QStringLiteral("Major")}, {QStringLiteral("root"), 0}}).ok);
		QCOMPARE(clip->notes().size(), std::size_t{1});
		QVERIFY(!clip->notes().front()->selected());

		QJsonObject args = self(clip);
		args.insert(QStringLiteral("scope"), QStringLiteral("selection"));
		const ControlResult refused = run(QStringLiteral("note.follow_chords"), args);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(clip->notes().front()->key(), 61);
		QVERIFY(run(QStringLiteral("chord.clear")).ok);
	}

	void clipPositionsAndOwnScales()
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(200)));
		QVERIFY(run(QStringLiteral("clip.quantize"), {{QStringLiteral("clips"), QJsonArray{control::clipIdOf(clip)}}}).ok);
		QCOMPARE(clip->startPosition().getTicks(), tick_t{192});
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Mine")},
			{QStringLiteral("degrees"), QJsonArray{3, 7, 10}}}).ok);
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), QStringLiteral("D")}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Mine")}}).ok);
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Mine")}}).ok);  // removed again
		QCOMPARE(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Bad")},
			{QStringLiteral("degrees"), QJsonArray{14}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	//! Luna cluster 2883: repeatNotes() formed each copy as
	//! note->pos().getTicks() + i * interval with no range check, and MidiClip::addNote
	//! validates nothing (src/tracks/MidiClip.cpp:182), so repeating a note at the right
	//! edge of the song wrote notes whose end is past MaxSongLength - while the reply still
	//! counted them as `added`. (The signed-overflow half of the report is false: interval is
	//! capped at 3072 and count at 64, so i * interval stays far inside tick_t's range.)
	void repeatingANoteAtTheSongsEdgeIsRefusedNotWrittenPastIt()
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		// A legal note whose END is exactly the song limit.
		const tick_t start = MaxSongLength - 10;
		clip->addNote(Note(TimePos(10), TimePos(start), 60), false);
		QCOMPARE(clip->notes().size(), std::size_t{1});

		QJsonObject args = self(clip);
		args.insert(QStringLiteral("count"), 2);
		args.insert(QStringLiteral("interval"), 48);
		const ControlResult refused = run(QStringLiteral("note.repeat"), args);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->notes().size(), std::size_t{1});  // no copy was written past the limit
		QVERIFY(refused.errorMessage.contains(QStringLiteral("song limit")));
	}

	//! Luna cluster 5694: flam() std::clamp-ed the caller's velocity into 0.05..1.0 and
	//! answered ok, so `velocity: 2` reported success while creating grace notes at a value
	//! nobody asked for. The schema takes a bare number (the commands_snapshot canary), so
	//! the range is refused in the handler instead, the way 4e288ecb5 does.
	void aFlamVelocityTheHandlerCannotHonourIsRefused()
	{
		MidiClip* clip = clipWith({{60, 24}});
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("velocity"), 2);
		const ControlResult refused = run(QStringLiteral("note.flam"), args);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->notes().size(), std::size_t{1});  // nothing was created
		// Below the floor the same way.
		args.insert(QStringLiteral("velocity"), 0.0);
		QCOMPARE(run(QStringLiteral("note.flam"), args).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->notes().size(), std::size_t{1});
		// Inside the range it still works, and the default is still 0.6.
		args.insert(QStringLiteral("velocity"), 0.5);
		QVERIFY(run(QStringLiteral("note.flam"), args).ok);
		QCOMPARE(clip->notes().size(), std::size_t{2});
	}

	//! Luna cluster 5691: arpeggiate() collected the voices in a std::map keyed by pitch, so
	//! emplace kept only the FIRST note of two same-pitch chord members while keys kept both -
	//! the second one's velocity, pan and probability were then read as duplicates of the first
	//! and lost when the chord was removed.
	void arpeggiatingAChordKeepsBothVoicesOfTheSamePitch()
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(48), TimePos(0), 64), false);
		// Two 60s, distinguishable only by their state - the clip lists them in the order
		// they were added, so the two of them are picked out by pitch, not by index.
		QVector<Note*> sixties;
		for (Note* note : clip->notes()) { if (note->key() == 60) { sixties << note; } }
		QCOMPARE(sixties.size(), 2);
		sixties[0]->setVolume(static_cast<volume_t>(40));
		sixties[1]->setVolume(static_cast<volume_t>(130));
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("rate"), 12);
		QVERIFY(run(QStringLiteral("note.arpeggiate"), args).ok);
		QCOMPARE(clip->notes().size(), std::size_t{4});
		// Both 60s play, each with its own volume: the run is 60(40), 60(130), 64, 60(40).
		QCOMPARE(clip->notes()[0]->key(), 60);
		QCOMPARE(clip->notes()[1]->key(), 60);
		QVERIFY2(clip->notes()[0]->getVolume() != clip->notes()[1]->getVolume(),
			"two same-pitch voices collapsed into one - the second one's state is gone");
		QCOMPARE(static_cast<int>(clip->notes()[0]->getVolume()), 40);
		QCOMPARE(static_cast<int>(clip->notes()[1]->getVolume()), 130);
	}

	//! Luna cluster 4806: the shared finish() helper advertised the command ITSELF as its
	//! inverse with empty inverse args, so every command that takes required arguments
	//! (note.add_many, note.group, ...) named an inverse that cannot be replayed at all:
	//! control.undo reading it fails validation on the missing 'clip'.
	void anAdvertisedInverseOfANoteEditCanActuallyBeReplayed()
	{
		MidiClip* clip = clipWith({});
		QJsonArray entries;
		entries.append(QJsonObject{{QStringLiteral("key"), 60}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 24}});
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("notes"), entries);
		QVERIFY(run(QStringLiteral("note.add_many"), args).ok);
		QCOMPARE(clip->notes().size(), std::size_t{1});

		const ControlRegistry::Transaction* recorded = ControlRegistry::instance()->lastTransaction();
		QVERIFY(recorded != nullptr);
		QCOMPARE(recorded->inverse.value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
		QCOMPARE(recorded->before.value(QStringLiteral("clip")).toString(), control::clipIdOf(clip));
		// And the advertised pair really does restore the prior state.
		const ControlResult replay = run(recorded->inverse.value(QStringLiteral("op")).toString(),
			recorded->inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replay.ok, qPrintable(replay.errorMessage));
		QVERIFY(replay.result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(clip->notes().size(), std::size_t{0});
	}

	//! Luna cluster 5680: a track holding TWO MIDI clips at the SAME start. keyTrack() picked the
	//! clone to keep by start position alone, so the other clip - also at that start - survived the
	//! cleanup too and had its notes filtered by key as well: every generated track then carried
	//! notes from BOTH clips.
	void aSplitKeepsOnlyTheClonedCounterpartOfTheNamedClip()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		// Two clips, both starting at tick 0 - the arrangement allows it, and it is the
		// case a start-position match cannot tell apart.
		auto* first = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		first->addNote(Note(TimePos(24), TimePos(0), 36), false);
		first->addNote(Note(TimePos(24), TimePos(24), 38), false);
		auto* second = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		second->addNote(Note(TimePos(24), TimePos(0), 40), false);
		second->addNote(Note(TimePos(24), TimePos(24), 42), false);

		const ControlResult split = run(QStringLiteral("note.split_by_pitch"), self(first));
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		const QJsonArray made = split.result.value(QStringLiteral("tracks")).toArray();
		QCOMPARE(made.size(), 2);  // only the NAMED clip's keys

		// Each new track is a copy of the source track holding ONE clip - the twin of
		// 'first' - and that clip's notes are the named key alone. A start-position match
		// would have kept the twin of 'second' as well, and its 40s/42s alongside.
		for (const QJsonValue& value : made)
		{
			Track* madeTrack = nullptr;
			for (Track* candidate : Engine::getSong()->tracks())
			{
				if (control::trackIdOf(candidate) == value.toString()) { madeTrack = candidate; }
			}
			QVERIFY2(madeTrack != nullptr, "the split must report a track that exists");
			QCOMPARE(madeTrack->getClips().size(), std::size_t{1});
			auto* midi = dynamic_cast<MidiClip*>(madeTrack->getClips().front());
			QVERIFY(midi != nullptr);
			QCOMPARE(midi->notes().size(), std::size_t{1});
			QVERIFY2(QList<int>({36, 38}).contains(midi->notes().front()->key()),
				"a generated track holds a note of the OTHER clip's keys");
		}
		// The source track is untouched by all of this.
		QCOMPARE(first->notes().size(), std::size_t{2});
		QCOMPARE(second->notes().size(), std::size_t{2});
	}

	//! Luna cluster 4856: rotate() wrapped every position from ZERO inside the shortened
	//! playable span (length - startTimeOffset), so a note past the span's end landed
	//! before the clip's startTimeOffset - the trimmed region that never sounds.
	void rotatingALeftTrimmedClipKeepsEveryNoteInTheAudibleRegion()
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		clip->setAutoResize(false);  // updateLength() would clear the offset below
		clip->changeLength(TimePos(100));
		clip->setStartTimeOffset(TimePos(20));
		clip->addNote(Note(TimePos(10), TimePos(70), 60), false);
		QCOMPARE(clip->startTimeOffset().getTicks(), tick_t{20});

		QJsonObject args = self(clip);
		args.insert(QStringLiteral("amount"), 20);
		QVERIFY(run(QStringLiteral("note.rotate"), args).ok);
		// 70 + 20 = 90 < 100, so it stays inside the clip's length.
		QCOMPARE(clip->notes()[0]->pos().getTicks(), tick_t{90});

		// A note that really wraps must land inside [offset, length). 90 + 40 = 130 wraps
		// to 50 relative to the offset - but wrapping from zero modulo the 80-tick span
		// would park it at tick 30... and a rotation large enough to cross the offset from
		// the other side would land it below 20, in the silent region.
		args.insert(QStringLiteral("amount"), -60);
		QVERIFY(run(QStringLiteral("note.rotate"), args).ok);
		const tick_t wrapped = clip->notes()[0]->pos().getTicks();
		QVERIFY2(wrapped >= clip->startTimeOffset().getTicks() && wrapped < clip->length().getTicks(),
			qPrintable(QStringLiteral("the wrapped note landed at tick %1 - outside the audible "
				"[%2, %3) region of the trimmed clip")
				.arg(wrapped).arg(clip->startTimeOffset().getTicks()).arg(clip->length().getTicks())));
	}

//! Luna cluster 5693: generate() clamped `pulses` to `steps` and answered ok, so
	//! `steps: 4, pulses: 7` succeeded as if four pulses had been asked for.
	void morePulsesThanStepsIsRefusedNotSilentlyReduced()
	{
		MidiClip* clip = clipWith({});
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("steps"), 4);
		args.insert(QStringLiteral("pulses"), 7);
		const ControlResult refused = run(QStringLiteral("note.generate"), args);
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("pulses")), "the refusal must say which argument");
		QCOMPARE(clip->notes().size(), std::size_t{0});  // nothing was written
		// pulses == steps is still legal, and so is pulses below it.
		args.insert(QStringLiteral("pulses"), 4);
		QVERIFY(run(QStringLiteral("note.generate"), args).ok);
		QCOMPARE(clip->notes().size(), std::size_t{4});
	}

//! Luna cluster 1154: generate() took the MidiClip checkpoint before the random draw, and a
	//! draw that hits nothing still answered ok - so a roll that wrote no note pushed an undo step.
	void aRandomRhythmThatHitsNothingTakesNoUndoStep()
	{
		MidiClip* clip = clipWith({});
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("kind"), QStringLiteral("random"));
		args.insert(QStringLiteral("steps"), 128);
		args.insert(QStringLiteral("pulses"), 1);
		args.insert(QStringLiteral("seed"), 0);  // this seed draws 128 times and never hits
		const ControlResult generated = run(QStringLiteral("note.generate"), args);
		QVERIFY2(generated.ok, qPrintable(generated.errorMessage));
		QCOMPARE(generated.result.value(QStringLiteral("added")).toInt(), 0);
		QCOMPARE(clip->notes().size(), std::size_t{0});
		QCOMPARE(journal->undoDepth(), depthBefore);

		// A seed that does hit still takes its one step.
		args.insert(QStringLiteral("seed"), 1);
		QVERIFY(run(QStringLiteral("note.generate"), args).ok);
		QVERIFY(clip->notes().size() > std::size_t{0});
		QCOMPARE(journal->undoDepth(), depthBefore + 1);
	}

//! The split's undo finds the tracks it made by id: track.remove and its undo rebuild one as a new
	//! object, which a kept Track* does not name.
	void aSplitTrackIsFoundAgainAfterItsRemovalIsUndone()
	{
		MidiClip* clip = clipWith({{36, 0}, {38, 24}});
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult split = run(QStringLiteral("note.split_by_pitch"), self(clip));
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		const QJsonArray made = split.result.value(QStringLiteral("tracks")).toArray();
		QVERIFY(!made.isEmpty());
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(made.first().toString()));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);  // every track it made is gone
	}

	//! The split's redo names the source by its ids: undoing track.remove rebuilds the track as a new
	//! object, so a kept Track* or MidiClip* is null by then and the redo would build nothing.
	void aSplitIsRebuiltAfterItsSourceTrackIsRemovedAndUndone()
	{
		const QPointer<InstrumentTrack> sharedTrack(m_track);
		// Removing and undoing the source builds a new object. Use a private source:
		// later cases still use m_track, which must not become a dangling pointer.
		auto* source = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(source != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(source->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addNote(Note(TimePos(24), TimePos(0), 36), false);
		clip->addNote(Note(TimePos(24), TimePos(24), 38), false);
		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		QVERIFY(run(QStringLiteral("note.split_by_pitch"), self(clip)).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before + 2);  // one per key
		const auto undone = []() { return run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool(); };
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(source)}}).ok);
		QVERIFY(undone());
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before + 2);  // the source track is back
		QVERIFY(undone());  // the split goes: its tracks are gone again
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before);
		QVERIFY(run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool());
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before + 2);
		QVERIFY(undone());
		QVERIFY2(sharedTrack, "the destructive regression must leave the shared fixture track alive");
	}
	//! Luna cluster 2870: note.randomize rolled its notes' positions in place and never
	//! called clip->updateLength(), so an auto-resizing clip kept its old length when a
	//! note was rolled past the old end - the note then lay outside the clip.
	void aRandomizedClipGrowsToCoverTheNoteItMoved()
	{
		constexpr int kJitter = 1024;
		const tick_t barTicks = TimePos::ticksPerBar();
		// Its own track: a roll that grows a clip must not depend on which case ran before it.
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		MidiClip* clip = nullptr;
		int seed = 0;
		for (int candidate = 1; candidate <= 30 && seed == 0; ++candidate)
		{
			clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
			clip->addNote(Note(TimePos(24), TimePos(0), 60), false);
			QJsonObject args = self(clip);
			args.insert(QStringLiteral("velocity_jitter"), 0.5);
			args.insert(QStringLiteral("position_jitter"), static_cast<double>(kJitter));
			args.insert(QStringLiteral("seed"), static_cast<double>(candidate));
			QVERIFY2(run(QStringLiteral("note.randomize"), args).ok, "note.randomize refused the roll");
			if (clip->notes()[0]->endPos().getTicks() > barTicks) { seed = candidate; }
		}
		QVERIFY2(seed != 0, "no seed in 1..30 rolled the note past the clip's first bar");
		QVERIFY2(clip->getAutoResize(), "the fixture's clip must auto-resize for this to matter");
		// The clip must now be long enough to hold the note, and long enough to have
		// grown: the note was rolled out of the single bar it started in.
		QVERIFY2(clip->length().getTicks() >= clip->notes()[0]->endPos().getTicks(),
			"note.randomize left a note outside the auto-resizing clip");
		QVERIFY2(clip->length().getTicks() > barTicks,
			"note.randomize moved a note past the clip's end but did not grow the clip");
	}

	//! Luna cluster 4814: note.remove advertised note.add as its inverse, passing only key,
	//! position, length and velocity. Everything else the note carried - its pan, its
	//! probability, its id - could not be restored by replaying that pair, so the advertised
	//! inverse silently produced a DIFFERENT note. The clip checkpoint is what really reverses
	//! the removal, so the inverse is control.undo, which takes no arguments.
	void theAdvertisedInverseOfRemovingANoteRestoresTheWholeNote()
	{
		MidiClip* clip = clipWith({{60, 0}}, 48);
		Note* note = clip->notes().front();
		note->setPanning(30);  // panning_t is an int8 in the -64..64 range, not a fraction
		note->setProbability(0.5f);
		const QString id = control::noteIdOf(note);
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("note"), id);
		QVERIFY2(run(QStringLiteral("note.remove"), args).ok, "note.remove refused");
		QCOMPARE(clip->notes().size(), std::size_t{0});

		const ControlRegistry::Transaction* recorded = ControlRegistry::instance()->lastTransaction();
		QVERIFY(recorded != nullptr);
		QCOMPARE(recorded->inverse.value(QStringLiteral("op")).toString(), QStringLiteral("control.undo"));
		// The before-payload still carries the full note state a client needs...
		QVERIFY(!recorded->before.isEmpty());
		// ...and replaying the advertised pair really does bring the note back whole.
		const ControlResult replay = run(recorded->inverse.value(QStringLiteral("op")).toString(),
			recorded->inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replay.ok, qPrintable(replay.errorMessage));
		QCOMPARE(clip->notes().size(), std::size_t{1});
		QVERIFY2(static_cast<int>(clip->notes().front()->getPanning()) == 30,
			"the inverse restored the note with its default pan, not the pan it had");
		QVERIFY2(qFuzzyCompare(clip->notes().front()->probability(), 0.5f),
			"the inverse restored the note with its default probability");
	}

	//! Luna cluster 4812: the selection store holds note-list POSITIONS, but a move re-sorts
	//! the list, so a stored position then names a different note and `previous_notes` reports
	//! the wrong selection. The notes' own selection flags travel with the note objects
	//! through a re-sort, so they are what the previous selection is read from.
	void selectingAfterAMoveReportsTheNoteThatWasActuallySelected()
	{
		MidiClip* clip = clipWith({{60, 0}, {64, 24}});
		const QString first = control::noteIdOf(clip->notes()[0]);
		const QString second = control::noteIdOf(clip->notes()[1]);

		QVERIFY2(run(QStringLiteral("note.select"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("notes"), QJsonArray{first}}}).ok, "note.select refused");

		// Move the selected note (at position 0) behind the other one: the list re-sorts, so
		// the stored position 0 now points at the note that was NOT selected.
		QJsonObject move = self(clip);
		move.insert(QStringLiteral("note"), first);
		move.insert(QStringLiteral("position"), 48);
		QVERIFY2(run(QStringLiteral("note.move"), move).ok, "note.move refused");
		QVERIFY2(clip->notes()[0]->key() == 64, "the fixture's move must re-sort the list");

		const ControlResult selected = run(QStringLiteral("note.select"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("notes"), QJsonArray{second}}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		const QJsonArray previous = selected.result.value(QStringLiteral("previous_notes")).toArray();
		QCOMPARE(previous.size(), 1);
		QCOMPARE(previous.first().toString(), first);
	}

};

QTEST_MAIN(NoteToolsTest)
#include "NoteToolsTest.moc"
