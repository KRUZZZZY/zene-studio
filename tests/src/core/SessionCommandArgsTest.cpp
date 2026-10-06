/*
 * SessionCommandArgsTest.cpp - what a <session> command accepts, and what it marks
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Three refusals the surface promised but did not keep, and one thing a
 *  <session> edit did not mark.
 *
 *  Each of these is a case where the caller sent something the reply then
 *  contradicted: a field read with a conversion that substitutes a default, a
 *  key treated as absent because its value had the wrong shape, an address
 *  completed with a zero the caller never wrote. In all three the command
 *  answered about state the client had not asked about, which is worse than a
 *  refusal - the caller has no way to tell.
 *
 *  The fourth is the opposite direction: the edit succeeded and told the caller
 *  so, while the project still read as clean, so nothing ever asked to save it.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int kScenes = 3;

QJsonObject audioSlot(int track, int scene)
{
	return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene},
		{QStringLiteral("type"), QStringLiteral("audio")},
		{QStringLiteral("source"), QStringLiteral("kick.wav")},
		{QStringLiteral("loop_length"), 192}};
}

} // namespace


class SessionCommandArgsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The registry only dispatches handlers once startup is announced.
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A <session> block edit is project state, so it is an unsaved change.
	//! SessionModel is not a JournallingObject and holds no Song back-pointer,
	//! so nothing else marked the song: after a set_grid / set_slot / set_scene
	//! the project still read as clean and the dirty-state autosave never ran.
	//! The sibling session paths already knew this - recordBlock() and
	//! captureToSlot() both call setModified().
	void aSessionModelEditMarksTheSongModified()
	{
		using revtest::run;
		Song* song = Engine::getSong();
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY2(!song->isModified(), "a new project starts clean");

		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		QVERIFY2(song->isModified(), "session.set_grid left the project marked clean");

		QVERIFY(run(QStringLiteral("session.set_slot"), audioSlot(0, 0)).ok);
		QVERIFY2(song->isModified(), "session.set_slot left the project marked clean");

		// One undo puts the block back, and the project is still an unsaved change.
		REV_UNDO_OR_FAIL();
		QVERIFY2(song->isModified(), "undoing a session edit cleared the modified flag");
	}
	//! A Follow Action entry whose FIELDS are of the wrong JSON type must be
	//! refused, not converted. QJsonValue::toDouble()/toInt()/toBool() substitute
	//! the field's default for anything they cannot convert, so `jump_to:"oops"`
	//! armed a jump to scene 0 - a well-formed action built out of a value the
	//! client never sent - and `"chance":"loud"` became chance 1, which always
	//! fires. Only the TYPE field was ever checked, so the rest of the entry was
	//! read with a conversion that cannot fail.
	void aWrongTypedActionFieldIsRefused()
	{
		using revtest::run;
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), audioSlot(0, 0)).ok);

		// The string a plain toInt() would have read as scene 0.
		QCOMPARE(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("follow_actions"),
				QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("jump")},
					{QStringLiteral("jump_to"), QStringLiteral("oops")}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);
		// The string a plain toDouble() would have read as chance 1 - always fire.
		QCOMPARE(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("follow_actions"),
				QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")},
					{QStringLiteral("chance"), QStringLiteral("loud")}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);
		// The string a plain toDouble() would have read as time_bars 1.
		QCOMPARE(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("follow_actions"),
				QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")},
					{QStringLiteral("linked"), false},
					{QStringLiteral("time_bars"), QStringLiteral("soon")}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);
		// The number a plain toBool() would have read as linked=false.
		QCOMPARE(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("follow_actions"),
				QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")},
					{QStringLiteral("linked"), 7}}}}})
			.errorKind, ControlErrorKind::InvalidArgs);

		// Nothing was written by any of them: the cell still has no chain.
		QCOMPARE(run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}}).errorKind,
			ControlErrorKind::InvalidArgs);
		// And a correctly typed entry of the same shape is still accepted.
		QVERIFY2(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("follow_actions"),
				QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("jump")},
					{QStringLiteral("jump_to"), 2}}}}}).ok, "a well-typed jump was refused");
	}

	//! A PRESENT `actions` that is not an array is the client's error, and is
	//! refused as one. The handler treats any non-array as if the key were absent
	//! and answers from the slot's own stored chain, so a caller that sent
	//! `actions: null` would arm a chain it never named and could not see in the
	//! reply. It never gets that far: the registry validates the arguments
	//! against the command's schema before dispatch (ControlRegistry::invoke ->
	//! validateArgs) and `session.follow_set` declares `actions` as an array
	//! property, so a non-array is a typed refusal with no arming at all. This
	//! case pins that, so the guarantee is not lost if the schema is edited.
	void aNonArrayActionsValueIsRefused()
	{
		using revtest::run;
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		const QJsonArray stop{QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}}};
		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("audio")},
				{QStringLiteral("source"), QStringLiteral("kick.wav")},
				{QStringLiteral("loop_length"), 192}, {QStringLiteral("follow_actions"), stop}}).ok);

		// A slot WITH a stored chain, so the fallback would have succeeded.
		QCOMPARE(run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("actions"), QStringLiteral("bad")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
				{QStringLiteral("actions"), QJsonValue()}}).errorKind,
			ControlErrorKind::InvalidArgs);

		// An ABSENT key is still the documented fallback to the stored chain.
		const ControlResult absent = run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}});
		QVERIFY2(absent.ok, qPrintable(absent.errorMessage));
		QCOMPARE(absent.result.value(QStringLiteral("source")).toString(), QStringLiteral("slot"));
	}

	//! A cell address is a PAIR. `track` and `scene` were independently
	//! optional, and the omitted one converted to 0, so `{"track":2}` answered
	//! about cell (2,0) - a real cell, with its own chain and its own armed flag
	//! - as though it had been asked for. Both coordinates, or neither.
	void aCellAddressNeedsBothCoordinates()
	{
		using revtest::run;
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2},
			{QStringLiteral("scenes"), kScenes}}).ok);
		const QJsonArray stop{QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}}};
		QVERIFY(run(QStringLiteral("session.set_slot"),
			{{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 0},
				{QStringLiteral("type"), QStringLiteral("audio")},
				{QStringLiteral("source"), QStringLiteral("kick.wav")},
				{QStringLiteral("loop_length"), 192}, {QStringLiteral("follow_actions"), stop}}).ok);
		QVERIFY(run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 0}}).ok);

		// One coordinate on its own is an incomplete address, not cell (x,0).
		QCOMPARE(run(QStringLiteral("session.follow_get_state"),
			{{QStringLiteral("track"), 1}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("session.follow_get_state"),
			{{QStringLiteral("scene"), 0}}).errorKind, ControlErrorKind::InvalidArgs);

		// Neither: the whole-grid reading, which is what the caller asked for.
		QVERIFY(run(QStringLiteral("session.follow_get_state")).ok);
		// Both: the cell, addressed as named.
		const ControlResult cell = run(QStringLiteral("session.follow_get_state"),
			{{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 0}});
		QVERIFY2(cell.ok, qPrintable(cell.errorMessage));
		QCOMPARE(cell.result.value(QStringLiteral("track")).toInt(), 1);
		QCOMPARE(cell.result.value(QStringLiteral("scene")).toInt(), 0);
	}

};

QTEST_GUILESS_MAIN(SessionCommandArgsTest)
#include "SessionCommandArgsTest.moc"
