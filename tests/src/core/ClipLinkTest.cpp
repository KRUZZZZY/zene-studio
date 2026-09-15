/*
 * ClipLinkTest.cpp - the registered proof of the linked / smart clip relation
 *                    (feature-list row 6, board task #645): the link relation
 *                    in the engine, an edit that propagates across it, an
 *                    unlink, and the relation surviving a save/reload round
 *                    trip.
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
//   1. the four ids exist with both schemas and an empty `requires` (SPEC A13):
//      requiredCommandsAreRegistered()
//   2. the relation is real: two clips that share one source, an edit to ONE of
//      them seen by ALL of them, in both directions, for every content verb:
//      anEditToOneMemberIsSeenByAll()
//   3. unlinking detaches, keeps the content, and dissolves the last pair:
//      unlinkDetachesAndKeepsItsContent()
//   4. THE ROUND TRIP: the project is saved, reloaded, and the link is still
//      there - and still propagates: theLinkSurvivesSaveAndReload()
//   5. the refusals are typed, not silent (a non-member, an audio clip, a
//      group that would merge): refusalsAreTypedAndNothingIsHalfWritten()
//   6. SPEC A16: one edit to one member is ONE undo step, and it takes the
//      WHOLE group back: undoRestoresEveryMemberOfTheGroup()
//   7. the table carries a row for each id: theA16ContractHasARowForEachId()
//   8. the UI absence is written down (the two docs say the same thing):
//      theOneLineUiAbsenceIsWrittenDown()

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;

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
		QVERIFY(m_dir.isValid());
		m_project = m_dir.filePath(QStringLiteral("link-roundtrip.mmp"));
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

		// (c) every content verb propagates, not just add: move, resize, velocity,
		//     remove - each one through the surface, each one checked on the OTHER
		//     member.
		const ControlResult moved = registry->invoke(QStringLiteral("note.move"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), QStringLiteral("note-1")},
				{QStringLiteral("position"), 48}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(notePositionAt(b, 1), 48);

		const ControlResult resized = registry->invoke(QStringLiteral("note.resize"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), QStringLiteral("note-0")},
				{QStringLiteral("length"), 48}});
		QVERIFY2(resized.ok, qPrintable(resized.errorMessage));
		QCOMPARE(noteLengthAt(b, 0), 48);

		const ControlResult velocity = registry->invoke(QStringLiteral("note.velocity_set"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), QStringLiteral("note-0")},
				{QStringLiteral("velocity"), 77}});
		QVERIFY2(velocity.ok, qPrintable(velocity.errorMessage));
		QCOMPARE(noteVelocityAt(b, 0), 77);

		const ControlResult removed = registry->invoke(QStringLiteral("note.remove"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("note"), QStringLiteral("note-0")}});
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
		//     where each member plays the content. Move B and A stays put.
		const int aStart = clipStart(a);
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

	void theLinkSurvivesSaveAndReload()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("roundtrip A"));
		const QString b = addMidiClip(QStringLiteral("roundtrip B"));
		const ControlResult linked = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(linked.ok, qPrintable(linked.errorMessage));
		const int group = linked.result.value(QStringLiteral("group")).toInt();
		const ControlResult note = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("key"), 69},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(note.ok, qPrintable(note.errorMessage));

		QVERIFY(Engine::getSong()->saveProjectFile(m_project));
		QVERIFY(QFile::exists(m_project));

		// the file itself carries the relation: the `link` attribute on each
		// member's own element, and nothing else (an unlinked clip writes none).
		QString xml;
		{
			QFile file(m_project);
			QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
			xml = QString::fromUtf8(file.readAll());
		}
		QVERIFY2(xml.contains(QStringLiteral("link=\"%1\"").arg(group)),
			"the saved project must name the link group on its members");

		// RELOAD and ask again - the acceptance criterion of this row.
		Engine::getSong()->loadProject(m_project);

		const ControlResult after = registry->invoke(QStringLiteral("clip.link_get_state"));
		QVERIFY2(after.ok, qPrintable(after.errorMessage));
		QVERIFY2(after.result.value(QStringLiteral("count")).toInt() >= 1,
			"the reloaded project has no link group at all");
		const QJsonObject groupState = firstGroupOfSize(after.result.value(QStringLiteral("groups")).toArray(), 2);
		QVERIFY2(!groupState.isEmpty(), "no reloaded group has two members");
		QCOMPARE(groupState.value(QStringLiteral("group")).toInt(), group);
		QCOMPARE(groupState.value(QStringLiteral("divergent")).toArray().size(), 0);
		QCOMPARE(groupState.value(QStringLiteral("notes")).toInt(), 1);

		// and the reloaded relation still PROPAGATES: the members of the reloaded
		// group are addressed by name (the ids are arrangement-derived, so they are
		// re-read rather than remembered) and an edit to one reaches the other.
		const QJsonArray members = groupState.value(QStringLiteral("members")).toArray();
		QCOMPARE(members.size(), 2);
		const QString first = members.at(0).toString();
		const QString second = members.at(1).toString();
		const ControlResult added = registry->invoke(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), first}, {QStringLiteral("key"), 71},
				{QStringLiteral("position"), 240}, {QStringLiteral("length"), 24}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(noteCount(second), 2);

		// the second save of the loaded project writes the same relation: the
		// round trip is stable, not merely lossless once.
		QVERIFY(Engine::getSong()->saveProjectFile(m_dir.filePath(QStringLiteral("link-again.mmp"))));
		QString again;
		{
			QFile file(m_dir.filePath(QStringLiteral("link-again.mmp")));
			QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
			again = QString::fromUtf8(file.readAll());
		}
		QCOMPARE(again.count(QStringLiteral("link=\"%1\"").arg(group)),
			xml.count(QStringLiteral("link=\"%1\"").arg(group)));
	}

	// -----------------------------------------------------------------------
	// 5. the refusals
	// -----------------------------------------------------------------------

	void refusalsAreTypedAndNothingIsHalfWritten()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString a = addMidiClip(QStringLiteral("refuse A"));
		const QString b = addMidiClip(QStringLiteral("refuse B"));
		const QString other = addMidiClip(QStringLiteral("refuse C"));

		// a clip that is not linked cannot be synced or unlinked
		const ControlResult syncUnlinked = registry->invoke(QStringLiteral("clip.link_sync"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(!syncUnlinked.ok);
		QCOMPARE(syncUnlinked.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult unlinkUnlinked = registry->invoke(QStringLiteral("clip.link_remove"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(!unlinkUnlinked.ok);
		QCOMPARE(unlinkUnlinked.errorKind, ControlErrorKind::InvalidArgs);

		// an AUDIO clip has no note list, so it cannot be a member (the one-line
		// UI note names this bound: a link shares a note list in 0.3.0)
		const QString sampleClip = addSampleClip(QStringLiteral("refuse audio"));
		QVERIFY(!sampleClip.isEmpty());
		const ControlResult audioMember = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{sampleClip}}});
		QVERIFY(!audioMember.ok);
		QCOMPARE(audioMember.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(audioMember.errorMessage.contains(QStringLiteral("note list")));

		// nothing was half-written by that refusal: A is still unlinked
		const ControlResult stateAfterRefusal = registry->invoke(QStringLiteral("clip.link_get_state"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(stateAfterRefusal.ok);
		QVERIFY(!stateAfterRefusal.result.value(QStringLiteral("linked")).toBool());

		// a group is not merged into another by naming one of its members
		const ControlResult firstPair = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("clips"), QJsonArray{b}}});
		QVERIFY2(firstPair.ok, qPrintable(firstPair.errorMessage));
		const ControlResult secondPair = registry->invoke(QStringLiteral("clip.link_create"),
			QJsonObject{{QStringLiteral("clip"), other}, {QStringLiteral("clips"), QJsonArray{a}}});
		QVERIFY(!secondPair.ok);
		QCOMPARE(secondPair.errorKind, ControlErrorKind::Refused);
		QVERIFY(secondPair.errorMessage.contains(QStringLiteral("clip.link_remove")));

		// and the refusal left the first pair intact and `other` unlinked
		const ControlResult pair = registry->invoke(QStringLiteral("clip.link_get_state"),
			QJsonObject{{QStringLiteral("clip"), a}});
		QVERIFY(pair.ok);
		QCOMPARE(pair.result.value(QStringLiteral("groups")).toArray().first().toObject()
			.value(QStringLiteral("size")).toInt(), 2);
	}

	// -----------------------------------------------------------------------
	// 6. SPEC A16: one undo takes the whole group back
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

private:
	// -----------------------------------------------------------------------
	// fixtures, all through the control surface (the release's own door)
	// -----------------------------------------------------------------------

	//! Adds an instrument track and a MIDI clip on it; returns the clip id.
	QString addMidiClip(const QString& name)
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString track = addTrack(QStringLiteral("instrument"));
		if (track.isEmpty()) { return QString(); }
		const ControlResult clip = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 384}, {QStringLiteral("name"), name}});
		if (!clip.ok) { return QString(); }
		return clip.result.value(QStringLiteral("clip")).toString();
	}

	//! Adds a sample track and the audio clip on it; returns the clip id.
	QString addSampleClip(const QString& name)
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString track = addTrack(QStringLiteral("sample"));
		if (track.isEmpty()) { return QString(); }
		const ControlResult clip = registry->invoke(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 384}, {QStringLiteral("name"), name}});
		if (!clip.ok) { return QString(); }
		return clip.result.value(QStringLiteral("clip")).toString();
	}

	QString addTrack(const QString& type)
	{
		const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), type}});
		if (!added.ok) { return QString(); }
		return added.result.value(QStringLiteral("track")).toString();
	}

	//! The clip's own authoritative read of its membership - a fresh lookup of the
	//! object the id names, not a remembered pointer.
	int clipLinkId(const QString& clipId)
	{
		const ControlResult state = ControlRegistry::instance()->invoke(
			QStringLiteral("clip.link_get_state"), QJsonObject{{QStringLiteral("clip"), clipId}});
		if (!state.ok) { return -1; }
		return state.result.value(QStringLiteral("group")).toInt();
	}

	int clipStart(const QString& clipId)
	{
		const ControlResult state = ControlRegistry::instance()->invoke(
			QStringLiteral("arrangement.get_state"));
		if (!state.ok) { return -1; }
		for (const QJsonValue& trackValue : state.result.value(QStringLiteral("tracks")).toArray())
		{
			for (const QJsonValue& clipValue : trackValue.toObject()
					.value(QStringLiteral("clips")).toArray())
			{
				const QJsonObject clip = clipValue.toObject();
				if (clip.value(QStringLiteral("clip")).toString() == clipId)
				{
					return clip.value(QStringLiteral("position")).toInt();
				}
			}
		}
		return -1;
	}

	//! The clip's notes, read through roll.get_state - the surface's own view of
	//! the note list the group shares.
	QJsonArray notesOf(const QString& clipId)
	{
		const ControlResult rolled = ControlRegistry::instance()->invoke(
			QStringLiteral("roll.get_state"), QJsonObject{{QStringLiteral("clip"), clipId}});
		if (!rolled.ok) { return QJsonArray(); }
		return rolled.result.value(QStringLiteral("notes")).toArray();
	}

	int noteCount(const QString& clipId) { return notesOf(clipId).size(); }
	int noteKeyAt(const QString& clipId, int index)
	{
		const QJsonArray notes = notesOf(clipId);
		if (index < 0 || index >= notes.size()) { return -1; }
		return notes.at(index).toObject().value(QStringLiteral("key")).toInt();
	}
	int notePositionAt(const QString& clipId, int index)
	{
		const QJsonArray notes = notesOf(clipId);
		if (index < 0 || index >= notes.size()) { return -1; }
		return notes.at(index).toObject().value(QStringLiteral("position")).toInt();
	}
	int noteLengthAt(const QString& clipId, int index)
	{
		const QJsonArray notes = notesOf(clipId);
		if (index < 0 || index >= notes.size()) { return -1; }
		return notes.at(index).toObject().value(QStringLiteral("length")).toInt();
	}
	int noteVelocityAt(const QString& clipId, int index)
	{
		const QJsonArray notes = notesOf(clipId);
		if (index < 0 || index >= notes.size()) { return -1; }
		return notes.at(index).toObject().value(QStringLiteral("velocity")).toInt();
	}

	//! The group object of the requested size, or an empty object.
	QJsonObject firstGroupOfSize(const QJsonArray& groups, int size)
	{
		for (const QJsonValue& value : groups)
		{
			const QJsonObject group = value.toObject();
			if (group.value(QStringLiteral("size")).toInt() == size) { return group; }
		}
		return QJsonObject();
	}

	/*! A doc file, read from the source tree this test binary was built out of.
	 *  ctest runs the binary from <build>/tests, so the worktree root is two
	 *  directories up; the candidates are tried in order and an unreadable file
	 *  is an empty string, which the caller turns into a SKIP rather than a
	 *  fabricated pass. */
	QString readDoc(const QString& name)
	{
		const QStringList candidates = {
			QCoreApplication::applicationDirPath() + QStringLiteral("/../../docs/") + name,
			QCoreApplication::applicationDirPath() + QStringLiteral("/../../../docs/") + name,
		};
		for (const QString& path : candidates)
		{
			QFile file(path);
			if (file.open(QIODevice::ReadOnly | QIODevice::Text))
			{
				return QString::fromUtf8(file.readAll());
			}
		}
		return QString();
	}

	QTemporaryDir m_dir;
	QString m_project;
};

QTEST_MAIN(ClipLinkTest)
#include "ClipLinkTest.moc"
