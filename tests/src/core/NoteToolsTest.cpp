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
#include <algorithm>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
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
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("root"), QStringLiteral("C")}, {QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
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

	void clipPositionsAndOwnScales()
	{
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(200)));
		QVERIFY(run(QStringLiteral("clip.quantize"), {{QStringLiteral("clips"), QJsonArray{control::clipIdOf(clip)}}}).ok);
		QCOMPARE(clip->startPosition().getTicks(), tick_t{192});
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Mine")},
			{QStringLiteral("degrees"), QJsonArray{3, 7, 10}}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("root"), QStringLiteral("D")}, {QStringLiteral("scale"), QStringLiteral("Mine")}}).ok);
		QVERIFY(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Mine")}}).ok);  // removed again
		QCOMPARE(run(QStringLiteral("scale.define"), {{QStringLiteral("name"), QStringLiteral("Bad")},
			{QStringLiteral("degrees"), QJsonArray{14}}}).errorKind, ControlErrorKind::InvalidArgs);
	}
};

QTEST_MAIN(NoteToolsTest)
#include "NoteToolsTest.moc"
