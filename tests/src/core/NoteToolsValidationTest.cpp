/*
 * NoteToolsValidationTest.cpp - degenerate input the note tools must refuse
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
/*! Regressions for note-tool input the happy-path cases in NoteToolsTest do not
 *  reach, each a refusal the handler must give BEFORE it touches the clip:
 *  - Luna cluster new-p15-17: note.arpeggiate must not delete a chord whose
 *    members all have zero or negative length (there is no run to play over it);
 *  - Luna cluster new-p17-15: note.flam must refuse a NaN 'velocity' ratio (NaN
 *    compares false against BOTH bounds of 0.05..1.0 and used to slip through
 *    into std::lround).
 *  They live here rather than in NoteToolsTest.cpp because the file-length gate
 *  (tests/file-length-gate.sh) holds that file at its 489-line base. */

#include <QtTest>

#include <limits>

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

} // namespace

class NoteToolsValidationTest : public QObject
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

	//! Luna cluster new-p15-17: exceedsArpeggioOutputLimit() started each chord's
	//! length at zero, so a chord whose members all have zero or negative length
	//! counted as ZERO output notes and passed the limit check; arpeggiate() then
	//! removed every member while its generation loop (`at < length`) never ran -
	//! the chord was deleted and nothing replaced it. A chord with no positive
	//! length to play over is refused typed, before the clip is touched.
	void arpeggiatingAChordWithNoPositiveLengthIsRefusedAndKeepsIt()
	{
		// Zero length (Note::setLength is the public writer; the saved-project
		// loader clamps to >= 1, so this state arrives through the API, not a file).
		MidiClip* clip = clipWith({{60, 0}, {64, 0}}, 0);  // a chord of zero-length notes
		QCOMPARE(clip->notes().size(), std::size_t{2});
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("rate"), 12);
		const ControlResult refused = run(QStringLiteral("note.arpeggiate"), args);
		QCOMPARE(clip->notes().size(), std::size_t{2});  // the chord was NOT deleted
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("length")),
			"the refusal must say the chord has no length to play over");

		// Negative lengths are the same non-span: the longest member is still <= 0.
		MidiClip* negative = clipWith({{60, 0}, {64, 0}}, -24);
		QJsonObject negativeArgs = self(negative);
		negativeArgs.insert(QStringLiteral("rate"), 12);
		QCOMPARE(run(QStringLiteral("note.arpeggiate"), negativeArgs).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(negative->notes().size(), std::size_t{2});  // no note removed or added

		// A nonpositive chord beside a playable one: nothing runs at all.
		MidiClip* twoChords = clipWith({{60, 0}, {64, 0}}, 48);
		twoChords->notes()[1]->setPos(TimePos(24));
		twoChords->notes()[1]->setLength(TimePos(0));
		QJsonObject twoArgs = self(twoChords);
		twoArgs.insert(QStringLiteral("rate"), 12);
		QCOMPARE(run(QStringLiteral("note.arpeggiate"), twoArgs).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(twoChords->notes().size(), std::size_t{2});  // the playable chord is untouched too

		// No collateral: a chord whose longest member is positive still becomes a run,
		// even when another member of the same chord is zero-length.
		MidiClip* mixed = clipWith({{60, 0}, {64, 0}}, 48);
		mixed->notes()[1]->setLength(TimePos(0));
		QJsonObject mixedArgs = self(mixed);
		mixedArgs.insert(QStringLiteral("rate"), 12);
		QVERIFY2(run(QStringLiteral("note.arpeggiate"), mixedArgs).ok,
			"a chord with a positive-length member must still arpeggiate");
		QCOMPARE(mixed->notes().size(), std::size_t{4});  // 48 ticks at 12 a step
	}

	//! Luna cluster new-p17-15: flam()'s range check compared the ratio with `<`
	//! and `>`, and BOTH comparisons are false for NaN - so a NaN velocity passed
	//! and the grace notes were written at std::lround(volume * NaN), an
	//! implementation-dependent value, while the command reported success.
	//! Non-finite ratios are refused typed, the way note.randomize refuses
	//! non-finite jitter (ControlCommandsNoteRandom.cpp:212).
	void aFlamVelocityRatioThatIsNotFiniteIsRefused()
	{
		MidiClip* clip = clipWith({{60, 24}});
		QJsonObject args = self(clip);
		args.insert(QStringLiteral("velocity"), std::numeric_limits<double>::quiet_NaN());
		const ControlResult refused = run(QStringLiteral("note.flam"), args);
		QVERIFY2(!refused.ok, "NaN velocity was accepted as a successful flam");
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clip->notes().size(), std::size_t{1});  // no grace note at a garbage velocity
	}

};

QTEST_MAIN(NoteToolsValidationTest)
#include "NoteToolsValidationTest.moc"
