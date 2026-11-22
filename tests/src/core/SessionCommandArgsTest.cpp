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
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "Track.h"

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
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		// What a save does to the session state, without a save command: project.save is itself an
		// undoable record, so control.undo after it would undo the save, not the edit. The save's
		// own state change is Song::noteProjectSaved, which clears the flag.
		const auto saveTo = [song, &dir](const QString& name) {
			song->noteProjectSaved(dir.filePath(name));
		};

		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		QVERIFY2(song->isModified(), "session.set_grid left the project marked clean");

		QVERIFY(run(QStringLiteral("session.set_slot"), audioSlot(0, 0)).ok);
		QVERIFY2(song->isModified(), "session.set_slot left the project marked clean");

		// A save clears the flag (Song::noteProjectSaved). The flag can only come back on the undo
		// through the undo's own mark, so this check fails if the restore does not mark the song.
		saveTo(QStringLiteral("first.mmpz"));
		QVERIFY(!song->isModified());
		REV_UNDO_OR_FAIL();
		QVERIFY2(song->isModified(), "undoing a session edit left the project marked clean");

		// The same for the redo: save to clear the flag, redo, and the flag must be back.
		saveTo(QStringLiteral("second.mmpz"));
		QVERIFY(!song->isModified());
		QVERIFY2(run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool(),
			"nothing was redone");
		QVERIFY2(song->isModified(), "redoing a session edit left the project marked clean");
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

		// Numbers a plain toInt() would have truncated or wrapped: 0.5 read as scene 0 and 1e30
		// read as garbage. A whole number inside the int range is the only jump_to that is read.
		for (const double jumpTo : {0.5, 1e30})
		{
			QCOMPARE(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
				{QStringLiteral("scene"), 0}, {QStringLiteral("follow_actions"),
					QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("jump")},
						{QStringLiteral("jump_to"), jumpTo}}}}})
				.errorKind, ControlErrorKind::InvalidArgs);
		}

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

	//! A scene's tempo override is a Song tempo, so session.set_scene takes only the engine's own
	//! range (MinTempo..MaxTempo, Song.h). The handler used to write any number: `tempo: 0` or 1e30
	//! was stored, saved and reported as an override that nothing could play. A refused value writes
	//! nothing, whether or not the override is switched on in the same call; the bounds themselves
	//! and a fractional tempo inside them are the control.
	void aSceneTempoOutsideTheEngineRangeIsRefused()
	{
		using revtest::run;
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		const auto sceneOf = [](const ControlResult& reply) {
			return reply.result.value(QStringLiteral("scene")).toObject();
		};
		const auto setTempo = [](double tempo) {
			return run(QStringLiteral("session.set_scene"),
				{{QStringLiteral("scene"), 0}, {QStringLiteral("tempo"), tempo}});
		};

		// Inside the range: both bounds, and a fractional tempo. Accepted and read back.
		for (const double accepted : {static_cast<double>(MinTempo), static_cast<double>(MaxTempo), 128.5})
		{
			const ControlResult ok = setTempo(accepted);
			QVERIFY2(ok.ok, qPrintable(ok.errorMessage));
			QCOMPARE(sceneOf(ok).value(QStringLiteral("tempo")).toDouble(), accepted);
		}

		// Outside it: refused as InvalidArgs.
		for (const double refused : {0.0, 9.5, 1000.0, 1e30, -120.0})
		{
			QCOMPARE(setTempo(refused).errorKind, ControlErrorKind::InvalidArgs);
		}
		// The same with the override switched off in the call: the value is still stored, so still refused.
		QCOMPARE(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 0},
			{QStringLiteral("tempo"), 0}, {QStringLiteral("tempo_override"), false}}).errorKind,
			ControlErrorKind::InvalidArgs);

		// No refusal wrote anything: the scene still holds the last accepted tempo, override on.
		const ControlResult readback = run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 0}});
		QVERIFY2(readback.ok, qPrintable(readback.errorMessage));
		QCOMPARE(sceneOf(readback).value(QStringLiteral("tempo")).toDouble(), 128.5);
		QVERIFY(sceneOf(readback).value(QStringLiteral("tempo_enabled")).toBool());
	}

	//! A midi slot names a PatternStore id, and that id must exist. set_slot took
	//! any integer: with no pattern in the store, pattern 0 was stored into a
	//! midi slot and a type-less {pattern} was accepted with nothing to check it
	//! against. A PatternTrack is what makes pattern 0 exist, so the checks run
	//! through session.set_slot before and after the track that holds it.
	void aMidiSlotMustNameAPatternTheStoreHolds()
	{
		using revtest::run;
		Song* song = Engine::getSong();
		song->clearProject();
		QCOMPARE(Engine::patternStore()->numOfPatterns(), 0);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), 2}}).ok);

		// The slot at (track, scene) as session.get_state reports it; empty when the cell is not a slot.
		const auto slotAt = [](int track, int scene) {
			const QJsonArray cells = run(QStringLiteral("session.get_state")).result
				.value(QStringLiteral("slots")).toArray();
			for (const QJsonValue& value : cells)
			{
				const QJsonObject slot = value.toObject();
				if (slot.value(QStringLiteral("track")).toInt() == track
					&& slot.value(QStringLiteral("scene")).toInt() == scene)
				{
					return slot;
				}
			}
			return QJsonObject();
		};

		// Refused: a midi slot naming pattern 0 while the store holds none, and the type-less form of it.
		const ControlResult midi = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("type"), QStringLiteral("midi")},
			{QStringLiteral("pattern"), 0}});
		QVERIFY2(!midi.ok, "a midi slot named a pattern the PatternStore does not hold");
		QCOMPARE(midi.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(midi.errorMessage.contains(QLatin1String("pattern"), Qt::CaseInsensitive),
			qPrintable(midi.errorMessage));

		const ControlResult typeless = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 1}, {QStringLiteral("pattern"), 0}});
		QVERIFY2(!typeless.ok, "a type-less set_slot named a pattern the PatternStore does not hold");

		// Neither refusal left a slot behind.
		QVERIFY2(slotAt(0, 0).isEmpty() && slotAt(0, 1).isEmpty(),
			"a refused set_slot still left a slot behind");

		// Normal and exhaustion: one pattern track makes pattern 0 exist; pattern 1 is still refused.
		PatternTrack* patternTrack = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
		QVERIFY(patternTrack != nullptr);
		QCOMPARE(Engine::patternStore()->numOfPatterns(), 1);

		const ControlResult accepted = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("type"), QStringLiteral("midi")},
			{QStringLiteral("pattern"), 0}});
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QVERIFY2(!slotAt(0, 0).isEmpty(), "an accepted midi slot is missing from session.get_state");
		QCOMPARE(slotAt(0, 0).value(QStringLiteral("pattern")).toInt(), 0);

		const ControlResult beyondStore = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 1}, {QStringLiteral("type"), QStringLiteral("midi")},
			{QStringLiteral("pattern"), 1}});
		QVERIFY2(!beyondStore.ok, "a midi slot named pattern 1 when the store holds only pattern 0");
		QCOMPARE(beyondStore.errorKind, ControlErrorKind::InvalidArgs);

		// Reconfiguration: removing the track removes its pattern, so the old reference is refused again.
		const ControlResult removed = run(QStringLiteral("track.remove"),
			{{QStringLiteral("track"), control::trackIdOf(patternTrack)}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(Engine::patternStore()->numOfPatterns(), 0);
		QVERIFY2(slotAt(0, 0).isEmpty(), "the slot that named the removed pattern survived the track's removal");

		const ControlResult stale = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("type"), QStringLiteral("midi")},
			{QStringLiteral("pattern"), 0}});
		QVERIFY2(!stale.ok, "a midi slot named pattern 0 after its track was removed");

		PatternTrack* replacement = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
		QVERIFY(replacement != nullptr);
		QCOMPARE(Engine::patternStore()->numOfPatterns(), 1);
		const ControlResult again = run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("type"), QStringLiteral("midi")},
			{QStringLiteral("pattern"), 0}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
	}

};

QTEST_GUILESS_MAIN(SessionCommandArgsTest)
#include "SessionCommandArgsTest.moc"
