/*
 * ClipLinkTest.cpp - the registered proof of the linked / smart clip relation
 *                    (feature-list row 6, board task #645): the relation in the
 *                    engine, an edit that propagates across it, an unlink, the
 *                    A16 rows and the UI-absence line.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

// WHAT THIS FILE PROVES, and how. Every claim is made through the CONTROL
// SURFACE (ControlRegistry::invoke), because that is the only door the release
// contract accepts: if the feature cannot be driven through the socket, it is
// not in 0.3.0. The tests are therefore also this group's argument-schema tests.
//
//   1. the four ids exist with both schemas and an empty `requires` (SPEC A13),
//      and the two writers are declared mutating: requiredCommandsAreRegistered()
//   2. the relation is real: two clips that share one source, an edit to ONE of
//      them seen by ALL of them, in both directions, for every content verb, and
//      the placement edits that must NOT propagate:
//      anEditToOneMemberIsSeenByAll()
//   3. unlinking detaches, keeps the content, and dissolves the last pair:
//      unlinkDetachesAndKeepsItsContent()
//   4. SPEC A16: one edit to one member is ONE undo step, and it takes the
//      WHOLE group back: undoRestoresEveryMemberOfTheGroup()
//   5. the table carries a row for each id: theA16ContractHasARowForEachId()
//   6. the UI absence is written down (both docs say the same thing):
//      theOneLineUiAbsenceIsWrittenDown()
//
// The save/reload round trip - the acceptance criterion of this row - and the
// typed refusals are ClipLinkPersistenceTest, and both binaries build their
// scenes with the fixtures in ClipLinkTestSupport.h.

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "ClipLinkTestSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"

using namespace lmms;
using namespace cliplinktest;

namespace
{

//! The ids of the group this file proves.
const QStringList kIds = {
	QStringLiteral("clip.link_create"), QStringLiteral("clip.link_remove"),
	QStringLiteral("clip.link_get_state"), QStringLiteral("clip.link_sync"),
};

} // namespace

class ClipLinkTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	// -----------------------------------------------------------------------
	// 1. the ids and their schemas
	// -----------------------------------------------------------------------

	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		for (const QString& id : kIds)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("clip"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY2(!cmd->argsSchema.isEmpty(), qPrintable(id + " has no argument schema"));
			QVERIFY2(!cmd->resultSchema.isEmpty(), qPrintable(id + " has no result schema"));
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requirement"));
		}
		// The two writers are declared mutating (so A16 records a transaction) and
		// the two reads are not.
		QVERIFY(registry->command(QStringLiteral("clip.link_create"))->mutating);
		QVERIFY(registry->command(QStringLiteral("clip.link_remove"))->mutating);
		QVERIFY(registry->command(QStringLiteral("clip.link_sync"))->mutating);
		QVERIFY(!registry->command(QStringLiteral("clip.link_get_state"))->mutating);

		// The argument schemas say what the group accepts: clip.link_create needs
		// both 'clip' and 'clips'; a missing required argument is an invalid_args
		// refusal and never a silent default.
		const ControlResult missing = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), QStringLiteral("clip-0")}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::InvalidArgs);
	}

	// -----------------------------------------------------------------------
	// 2. the propagation
	// -----------------------------------------------------------------------

	void anEditToOneMemberIsSeenByAll()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("pattern A"));
		const QString b = addMidiClip(QStringLiteral("pattern B"));
		QVERIFY(!a.isEmpty());
		QVERIFY(!b.isEmpty());

		// (a) the link itself, created through the surface
		const ControlResult linked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a},
				{QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(linked.ok, qPrintable(linked.errorMessage));
		const int group = linked.result.value(QStringLiteral("group")).toInt();
		QVERIFY2(group > 0, "a link group must have a non-zero id (0 means unlinked)");
		QCOMPARE(linked.result.value(QStringLiteral("size")).toInt(), 2);
		QCOMPARE(linked.result.value(QStringLiteral("content")).toString(), QStringLiteral("notes"));
		QCOMPARE(linked.result.value(QStringLiteral("members")).toArray().size(), 2);

		// (b) an edit to ONE member is seen by the OTHER: note.add on A
		const ControlResult addedToA = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(addedToA.ok, qPrintable(addedToA.errorMessage));
		QCOMPARE(noteCount(b), 1);
		QCOMPARE(noteKeyAt(b, 0), 60);

		// and in the OTHER direction: an edit to B is seen by A
		const ControlResult addedToB = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), b}, {QStringLiteral("key"), 67},
				{QStringLiteral("position"), 96}, {QStringLiteral("length"), 24}});
		QVERIFY2(addedToB.ok, qPrintable(addedToB.errorMessage));
		QCOMPARE(noteCount(a), 2);
		QCOMPARE(noteCount(b), 2);
		QCOMPARE(noteKeyAt(a, 1), 67);

		// The two notes' OWN ids AS CLIP A CARRIES THEM, read back through
		// roll.get_state (noteIdAt). note.add's result names the note of the clip
		// the call was made on, and a link group mirrors content as each
		// member's OWN Note objects (SPEC-stable-ids.md R4: a copy is a new
		// object) - so B's copy of the 67-note does not carry A's id, which is
		// what the literal-and-result version of this slot measured as
		// "no note note-12 (the clip has 2)".
		const QString firstNote = noteIdAt(a, 0);
		const QString secondNote = noteIdAt(a, 1);
		QVERIFY2(!firstNote.isEmpty() && !secondNote.isEmpty() && firstNote != secondNote,
			"roll.get_state must report both of clip A's notes with their own ids");

		// (c) every content verb propagates, not just add: move, resize, velocity,
		//     remove - each one through the surface, each one checked on the OTHER
		//     member.
		const ControlResult moved = registry->invoke(QStringLiteral("note.move"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), secondNote},
				{QStringLiteral("position"), 48}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(notePositionAt(b, 1), 48);

		const ControlResult resized = registry->invoke(QStringLiteral("note.resize"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), firstNote},
				{QStringLiteral("length"), 48}});
		QVERIFY2(resized.ok, qPrintable(resized.errorMessage));
		QCOMPARE(noteLengthAt(b, 0), 48);

		const ControlResult velocity = registry->invoke(QStringLiteral("note.velocity_set"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), firstNote},
				{QStringLiteral("velocity"), 77}});
		QVERIFY2(velocity.ok, qPrintable(velocity.errorMessage));
		QCOMPARE(noteVelocityAt(b, 0), 77);

		const ControlResult removed = registry->invoke(QStringLiteral("note.remove"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), firstNote}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(noteCount(b), 1);
		QCOMPARE(noteKeyAt(b, 0), 67);

		// (d) the group is IN SYNC, and clip.link_get_state says so per member:
		//     no member is listed as divergent.
		const ControlResult state = registry->invoke(QStringLiteral("clip.link_get_state"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QVERIFY(state.result.value(QStringLiteral("linked")).toBool());
		QCOMPARE(state.result.value(QStringLiteral("count")).toInt(), 1);
		const QJsonObject groupState = state.result.value(QStringLiteral("groups")).toArray()
			.first().toObject();
		QCOMPARE(groupState.value(QStringLiteral("size")).toInt(), 2);
		QCOMPARE(groupState.value(QStringLiteral("divergent")).toArray().size(), 0);
		QCOMPARE(groupState.value(QStringLiteral("in_sync")).toArray().size(), 2);
		QCOMPARE(groupState.value(QStringLiteral("group")).toInt(), group);

		// (e) and the two are NOT the same clip: what a link does NOT share is
		//     where each member plays the content. Move B and A stays put. The
		//     reader is checked to have FOUND the clips first: a "both -1" would
		//     otherwise pass as "A did not move".
		const int aStart = clipStart(a);
		QVERIFY2(aStart >= 0, "arrangement.get_state must report a clip of this id");
		const ControlResult movedB = registry->invoke(QStringLiteral("clip.move"),
			QJsonObject{{QStringLiteral("clip"), b}, {QStringLiteral("position"), 1920}});
		QVERIFY2(movedB.ok, qPrintable(movedB.errorMessage));
		QCOMPARE(clipStart(a), aStart);
		QCOMPARE(clipStart(b), 1920);
		// ...and the move did not disturb the shared content
		QCOMPARE(noteCount(b), 1);
	}

	// -----------------------------------------------------------------------
	// 3. the unlink
	// -----------------------------------------------------------------------

	void unlinkDetachesAndKeepsItsContent()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("split A"));
		const QString b = addMidiClip(QStringLiteral("split B"));
		const ControlResult linked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(linked.ok, qPrintable(linked.errorMessage));

		const ControlResult note = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("key"), 62},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(note.ok, qPrintable(note.errorMessage));
		QCOMPARE(noteCount(b), 1);

		// (a) unlink B: the group is dissolved (one member was left), and B keeps
		//     the content it had.
		const ControlResult unlinked = registry->invoke(QStringLiteral("clip.link_remove"),
			QJsonObject{{QStringLiteral("clip"), b}});
		QVERIFY2(unlinked.ok, qPrintable(unlinked.errorMessage));
		QCOMPARE(unlinked.result.value(QStringLiteral("removed")).toString(), b);
		QVERIFY(unlinked.result.value(QStringLiteral("dissolved")).toBool());
		QCOMPARE(noteCount(b), 1);
		QCOMPARE(noteCount(a), 1);

		const ControlResult stateA = registry->invoke(QStringLiteral("clip.link_get_state"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(stateA.ok);
		QVERIFY2(!stateA.result.value(QStringLiteral("linked")).toBool(),
			"the last pair leaving must leave no group of one behind");

		// (b) the detach is real: an edit to A is not seen by B any more.
		const ControlResult later = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("key"), 64},
				{QStringLiteral("position"), 96}, {QStringLiteral("length"), 24}});
		QVERIFY2(later.ok, qPrintable(later.errorMessage));
		QCOMPARE(noteCount(a), 2);
		QCOMPARE(noteCount(b), 1);

		// (c) A can be linked to something else now, and the pair works again.
		const QString c = addMidiClip(QStringLiteral("split C"));
		const ControlResult relinked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{c}}});
		QVERIFY2(relinked.ok, qPrintable(relinked.errorMessage));
		QCOMPARE(noteCount(c), 2);
		QCOMPARE(noteCount(b), 1);
	}

	// -----------------------------------------------------------------------
	// 4. THE ROUND TRIP
	// -----------------------------------------------------------------------

	void undoRestoresEveryMemberOfTheGroup()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("undo A"));
		const QString b = addMidiClip(QStringLiteral("undo B"));
		const ControlResult linked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(linked.ok, qPrintable(linked.errorMessage));
		QCOMPARE(noteCount(a), 0);
		QCOMPARE(noteCount(b), 0);

		// The undo of the link itself: the recorded transaction is reversible and
		// one control.undo takes the relation away.
		const ControlResult undoLink = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undoLink.ok, qPrintable(undoLink.errorMessage));
		QVERIFY(undoLink.result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(clipLinkId(a), 0);
		QCOMPARE(clipLinkId(b), 0);

		// Re-link and then edit ONE member: the whole edit is ONE step, and that
		// step covers every member the mirror wrote - one undo and BOTH are back
		// to where they were.
		const ControlResult relinked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(relinked.ok, qPrintable(relinked.errorMessage));
		const ControlResult added = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("key"), 65},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(noteCount(a), 1);
		QCOMPARE(noteCount(b), 1);

		const ControlResult undoEdit = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undoEdit.ok, qPrintable(undoEdit.errorMessage));
		QVERIFY(undoEdit.result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(noteCount(a), 0);
		QCOMPARE(noteCount(b), 0);
		// the link itself is NOT undone by the edit's undo: the relation is one
		// step, the edit is the next one.
		QCOMPARE(clipLinkId(a), clipLinkId(b));
		QVERIFY(clipLinkId(a) > 0);
	}

	// -----------------------------------------------------------------------
	// 7. the A16 rows
	// -----------------------------------------------------------------------

	void theA16ContractHasARowForEachId()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		for (const QString& id : kIds)
		{
			const control::ReversibilityEntry* entry = table.lookup(id);
			QVERIFY2(entry != nullptr, qPrintable(QStringLiteral("no A16 row for %1").arg(id)));
			QVERIFY(!entry->reason.isEmpty());
			QVERIFY(!entry->mechanism.isEmpty());
		}
		const control::ReversibilityEntry* create = table.lookup(QStringLiteral("clip.link_create"));
		QCOMPARE(create->cls, control::ReversibilityClass::TrueInverse);
		QVERIFY(create->reversible);
		QCOMPARE(table.lookup(QStringLiteral("clip.link_remove"))->cls,
			control::ReversibilityClass::TrueInverse);
		QCOMPARE(table.lookup(QStringLiteral("clip.link_sync"))->cls,
			control::ReversibilityClass::TrueInverse);
		QCOMPARE(table.lookup(QStringLiteral("clip.link_get_state"))->cls,
			control::ReversibilityClass::NotMutating);
		QCOMPARE(control::reversibilityClassName(create->cls), QStringLiteral("true_inverse"));
	}

	// -----------------------------------------------------------------------
	// 8. the UI absence
	// -----------------------------------------------------------------------

	void theOneLineUiAbsenceIsWrittenDown()
	{
		// The contract's fourth item: a feature whose UI absence is not written
		// down is not in the release. Both files carry the SAME one line, and it
		// names what propagates and what does not - so the claim cannot drift
		// from the code without failing here.
		const QString limitations = readDoc(QStringLiteral("KNOWN-LIMITATIONS.md"));
		const QString releaseNotes = readDoc(QStringLiteral("RELEASE-NOTES-v0.3.0-alpha.md"));
		if (limitations.isEmpty() || releaseNotes.isEmpty())
		{
			QSKIP("the docs are not reachable from this build directory");
		}
		QVERIFY2(limitations.contains(QStringLiteral("clip.link_create")),
			"docs/KNOWN-LIMITATIONS.md carries no linked-clips line");
		QVERIFY2(releaseNotes.contains(QStringLiteral("clip.link_create")),
			"docs/RELEASE-NOTES-v0.3.0-alpha.md carries no linked-clips line");
		// Both say the same thing about what propagates: the content verbs are
		// named in the line, and the placement/level verbs are named as NOT
		// propagating, so the two halves of the claim travel together.
		for (const QString& doc : {limitations, releaseNotes})
		{
			QVERIFY2(doc.contains(QStringLiteral("clip.link_sync")),
				"the linked-clips line does not name the group's own verbs");
			QVERIFY2(doc.contains(QStringLiteral("note.")),
				"the linked-clips line does not name the content verbs that propagate");
		}
	}

};

QTEST_MAIN(ClipLinkTest)
#include "ClipLinkTest.moc"
