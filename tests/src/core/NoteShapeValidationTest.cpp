/*
 * NoteShapeValidationTest.cpp - what the note-shape verbs REFUSE: a note that would end past
 *                               the song limit, a filter field of the wrong type, an amount the
 *                               handler cannot hold, and the selection store after select_where.
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
 */

/*! Four inputs the note-shape verbs accepted and could not honour:
 *
 *  - note.add_many checked 'position >= 0' and 'length >= 1' one at a time, so a note could be
 *    written ending past MaxSongLength (the note.edit half is NoteEditRangeTest);
 *  - the filter's 'scale' and 'invert' were read with QJsonValue::toString()/toBool(), which turn
 *    a value of the WRONG type into "no scale filter" and "do not invert" - so a filter the
 *    caller meant to narrow matched every note, and 'action: remove' then removed them all;
 *  - 'amount' had no bound, and the handler narrowed it with toInt(), so a value outside int
 *    became 0 and the transform silently did nothing;
 *  - note.select_where set the notes' own selection flags but never wrote the selection INDEX
 *    store control::selectNotes owns, which note.select writes as well - so the two disagreed.
 *
 *  The commands themselves (scale, ratchet, list, the actions) are NoteEditingCommandsTest. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
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

class NoteShapeValidationTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	QString clip() const { return control::clipIdOf(m_clip); }

	//! Three notes at distinct keys, positions and velocities, so a filter that matches nothing
	//! is visibly different from one that matches everything.
	void freshClip()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(12), TimePos(48), 61, 40), false);
		m_clip->addNote(Note(TimePos(12), TimePos(96), 67, 160), false);
		m_clip->rearrangeAllNotes();
	}

	//! The error kind a transform_where raises; Ok (None) when it was accepted. An 'amount' left
	//! out entirely (an Undefined sentinel, not a JSON null) is the "not given" case.
	ControlErrorKind transform(const QJsonObject& filter, const char* action,
		const QJsonValue& amount = QJsonValue(QJsonValue::Undefined))
	{
		QJsonObject args{{QStringLiteral("clip"), clip()}, {QStringLiteral("filter"), filter},
			{QStringLiteral("action"), QLatin1String(action)}};
		if (!amount.isUndefined()) { args.insert(QStringLiteral("amount"), amount); }
		const ControlResult result = run(QStringLiteral("note.transform_where"), args);
		return result.ok ? ControlErrorKind::None : result.errorKind;
	}

	//! The ids of every note of the clip, in the clip's own order.
	QStringList noteIds()
	{
		QStringList ids;
		for (const Note* note : m_clip->notes()) { ids << control::noteIdOf(note); }
		return ids;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! Luna cluster 4816: note.add_many validated position and length one at a time, so their
	//! SUM - the note's end - was never checked and a note could be written past MaxSongLength.
	void addManyRefusesANoteThatEndsPastTheSongLimit()
	{
		freshClip();
		const QStringList before = noteIds();
		const auto entry = [](int position, int length) {
			return QJsonObject{{QStringLiteral("key"), 60}, {QStringLiteral("position"), position},
				{QStringLiteral("length"), length}};
		};

		// (a) each value on its own is a legal int, the sum is not inside the song.
		QCOMPARE(run(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("notes"), QJsonArray{entry(MaxSongLength, 100)}}}).errorKind, ControlErrorKind::InvalidArgs);
		// (b) the sum overflows the signed 32-bit tick range outright.
		QCOMPARE(run(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("notes"), QJsonArray{entry(2147483600, 100)}}}).errorKind, ControlErrorKind::InvalidArgs);
		// ...and nothing was added: the whole list is checked before the first note is.
		QCOMPARE(noteIds(), before);

		// (c) the limit itself stays reachable: ending exactly at MaxSongLength is accepted.
		QVERIFY2(run(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("notes"), QJsonArray{entry(MaxSongLength - 48, 48)}}}).ok,
			"a note ending exactly at the song limit must still be addable");
		const Note* added = m_clip->notes().back();
		QCOMPARE(added->pos().getTicks() + added->length().getTicks(), static_cast<int>(MaxSongLength));
	}

	//! Luna cluster 2873: filter.scale and filter.invert were read through toString()/toBool(),
	//! which read a value of the wrong type as the empty choice - so a malformed filter matched
	//! every note and 'action: remove' removed all of them.
	void aMalformedFilterFieldIsRefusedNotIgnored()
	{
		freshClip();
		const QStringList before = noteIds();

		// (a) a non-string scale is not "no scale filter"; it is a value the caller got wrong.
		QCOMPARE(transform({{QStringLiteral("scale"), 42}}, "remove"), ControlErrorKind::InvalidArgs);
		QCOMPARE(transform({{QStringLiteral("scale"), true}}, "remove"), ControlErrorKind::InvalidArgs);
		// (b) neither is a non-boolean invert.
		QCOMPARE(transform({{QStringLiteral("key_max"), 61}, {QStringLiteral("invert"), 1}}, "remove"),
			ControlErrorKind::InvalidArgs);
		// (c) and the notes are all still there: a refused filter removed nothing.
		QCOMPARE(noteIds(), before);

		// (d) the well-formed filter of the same shape still works - the check is on the type,
		//     not on the field being present. key_max 61 matches BOTH notes below it (60 and
		//     61), so only the note above survives.
		QCOMPARE(transform({{QStringLiteral("key_max"), 61}}, "remove"), ControlErrorKind::None);
		const QStringList survivors{before.at(2)};
		QCOMPARE(noteIds(), survivors);
	}

	//! Luna cluster 2877: 'amount' carried no bound, so a value outside int was narrowed to 0
	//! by toInt() and the transform silently did nothing.
	void aTransformAmountMustBeAWholeNumberTheHandlerCanHold()
	{
		freshClip();

		// (a) a value past the schema integer does not silently become a zero transpose.
		QCOMPARE(transform({}, "transpose", 2147483648.0), ControlErrorKind::InvalidArgs);
		// (b) a fractional amount is not a semitone count either.
		QCOMPARE(transform({}, "transpose", 2.5), ControlErrorKind::InvalidArgs);
		QCOMPARE(transform({}, "velocity_offset", 2.5), ControlErrorKind::InvalidArgs);
		// ...and no note moved.
		QCOMPARE(m_clip->notes()[0]->key(), 60);
		QCOMPARE(m_clip->notes()[0]->getVolume(), 100);

		// (c) a legal amount still transposes - the refusal is on the value, not the verb.
		QCOMPARE(transform({}, "transpose", 12), ControlErrorKind::None);
		QCOMPARE(m_clip->notes()[0]->key(), 72);
	}

	//! Luna cluster 3898: note.select_where wrote the notes' selection flags but not the
	//! selection index store control::selectNotes owns, which note.select writes as well - so
	//! the store still reported the selection note.select had made before.
	void select_whereWritesTheSelectionStoreToo()
	{
		freshClip();
		QVERIFY2(run(QStringLiteral("note.select"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("notes"), QJsonArray{noteIds().at(2)}}}).ok, "note.select refused");
		QVERIFY(!m_clip->notes()[0]->selected());

		// select_where replaces the selection with the notes matching key_max 61.
		QVERIFY2(run(QStringLiteral("note.select_where"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("filter"), QJsonObject{{QStringLiteral("key_max"), 61}}}}).ok,
			"note.select_where refused");

		// The store is the one note.select reports as its previous selection, and it must now
		// hold the two notes select_where chose - not the one that was selected before.
		const ControlResult cleared = run(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("notes"), QJsonArray{}}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(cleared.result.value(QStringLiteral("previous_notes")).toArray().size(), 2);

		// A second select_where then reports the right previous selection too.
		QVERIFY2(run(QStringLiteral("note.select_where"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("filter"), QJsonObject{{QStringLiteral("key_min"), 61}}}}).ok,
			"the second note.select_where refused");
		const ControlResult readBack = run(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("notes"), QJsonArray{}}});
		QCOMPARE(readBack.result.value(QStringLiteral("previous_notes")).toArray().size(), 2);
	}

	//! Luna cluster new-p8-38: matching one member of a note group selects the other members too.
	void select_whereBringsAlongGroupMates()
	{
		freshClip();
		NoteConditions grouped;
		grouped.group = 7;
		m_clip->notes()[0]->setConditions(grouped);
		m_clip->notes()[1]->setConditions(grouped);

		const ControlResult selected = run(QStringLiteral("note.select_where"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("filter"), QJsonObject{{QStringLiteral("key_max"), 60}}}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
		QCOMPARE(selected.result.value(QStringLiteral("matched")).toInt(), 1);
		QVERIFY(m_clip->notes()[0]->selected());
		QVERIFY(m_clip->notes()[1]->selected());
		QVERIFY(!m_clip->notes()[2]->selected());
		const QVector<int> expectedIndices{0, 1};
		QCOMPARE(control::selectedNoteIndices(clip()), expectedIndices);
		QJsonArray expectedSelected{noteIds().at(0), noteIds().at(1)};
		QCOMPARE(selected.result.value(QStringLiteral("selected")).toArray(), expectedSelected);
	}
};

QTEST_MAIN(NoteShapeValidationTest)
#include "NoteShapeValidationTest.moc"
