/*
 * ControlEditCommandsTest.cpp - unit tests for the notes / clips / tracks command
 *                               group (SPEC-zene-studio.md A11-A16).
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

#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QScopeGuard>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlEdit.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "Note.h"

using namespace lmms;

//! The track's own mute flag, as track.get_state reports it.
static bool muted(const QString& track)
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("track.get_state"),
		QJsonObject{{QStringLiteral("track"), track}});
	return state.ok && state.result.value(QStringLiteral("muted")).toBool();
}

class ControlEditCommandsTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase();

	void cleanupTestCase();

	//! Every command of the group is registered with the contract's parts: a
	//! group.verb id, both schemas and a requires declaration (empty = headless).
	void requiredCommandsAreRegistered();

	//! Out-of-range note values are rejected by the schema, before any handler.
	void outOfRangeNoteValuesAreInvalidArgs();

	//! note.move's schema bounds the new START (0..MaxSongLength), but the note's
	//! END must fit inside the song too: a positive-length note moved to MaxSongLength
	//! would end past the limit. The move is refused, typed, with nothing changed.
	void noteMovePastSongLimitIsRefused();

	//! The end guard must survive lengths the schema never sees: Note::loadSettings
	//! reads a note's length straight from XML without a MaxSongLength clamp, so a
	//! loaded/model note can carry a positive length near INT_MAX. Adding that to a
	//! MaxSongLength start in 32-bit wraps negative and slips past an int32 check;
	//! the handler compares in a wide type, so the move is refused and the note is
	//! untouched.
	void noteMoveWithHugeLoadedLengthIsRefused();

	void selectionSaveRejectsNonStringClipIds();

	//! A malformed id and an absent id are different, typed failures.
	void malformedAndAbsentIdsAreTyped();

	//! The whole editing flow, in process: add a track, a clip, notes, edit one,
	//! read it back, delete it and undo. The engine paths are the real ones.
	void editingFlowAddsEditsAndUndoes();

	/*! BEFORE the fix, the selection was stored as POSITIONS in the note list
	 *  (control::selectNotes, src/core/ControlEditSupport.cpp) and roll.get_state
	 *  reported the ids of the notes AT those positions. Removing a note BEFORE a
	 *  selected one shifted every later note down one, so the stored position named
	 *  a different note: after removing note A, `selected_notes` named B before the
	 *  remove and named C after. The store keeps the selected notes' ids now.
	 */
	void aNoteRemovalDoesNotShiftTheSelectionOntoAnotherNote();

	/*! The same defect as the removal case above, reached the other way.
	 *  BEFORE the fix, note.move re-sorted the clip (clip->rearrangeAllNotes())
	 *  without remapping the stored positions, so a selected index silently
	 *  started naming a DIFFERENT note: selecting the first note and moving it
	 *  past the second left the selection pointing at position 0, which was
	 *  then the second note.
	 */
	void aNoteMoveDoesNotShiftTheSelectionOntoAnotherNote();

	//! track.set_arm is a REAL arm verb now (0.3.0, feature row 14): it starts a
	//! capture on the record route the track's position maps to, writes the take
	//! and journals it. This replaces the old "honest typed refusal" assertion
	//! that used to sit here - the refusal was correct about the tree it
	//! described and was itself the defect the feature list recorded.
	void trackSetArmStartsARealCapture();
	//! track.set_mute is a plain model write; track.set_solo is the product's whole
	//! solo action, so it unmutes the soloed track (Track::toggleSolo). Asserting
	//! both keeps the semantics of the two commands pinned.
	void trackMuteAndSoloReadBack();

	//! In a headless process the surface can name one clip, but the arranger selection did not come back.
	void headlessSelectionApplyReportsClipsSkipped();

	//! track.add's undo finds its track by id when it runs: track.remove and its undo rebuild the
	//! track as a new object, and the step used to remove the freed one it kept a pointer to.
	void trackAddUndoFindsItsTrackAfterARemovalIsUndone();

	//! Headless, track.set_solo has to drive Track::toggleSolo() itself (a display-ful
	//! instance gets it from TrackView's dataChanged hook). A request that changes
	//! nothing must not run it again: the second solo re-captured the ALREADY SOLOED
	//! mute states as the "before solo" snapshot, so the unsolo that follows restored
	//! them instead of the pre-solo ones.
	void trackSetSoloRepeatedInHeadlessModeIsIdempotent();

	//! track.set_arm_many answers 'tracks' = the ones it CHANGED. Disarming is
	//! not a refusal when the route is already disarmed, so the batch has to
	//! recognise that state itself - and a repeated id names one track, so it can
	//! be reported once at most.
	void trackSetArmManyReportsOnlyTheTracksItChanged();

	//! Batch arming rejects a non-string ID before changing any record route.
	void trackSetArmManyRejectsNonStringIdsBeforeArming();

	//! An idempotent track.set_solo records no change, so its inverse has to name the
	//! state it found - not !value, which would unsolo the track the no-op request
	//! asked to keep soloed.
	void trackSetSoloNoOpRequestInvertsToTheStateItFound();

	//! A folder track is a container with no clips of its own: Track::createClip()
	//! returns null for it. clip.add used to dereference that null (a crash). It must
	//! refuse the add instead, and the refusal must leave no undo step behind - the
	//! checkpoint is taken only for a track that can hold a clip.
	void clipAddOnAFolderTrackIsRefusedWithoutAnUndoStep();
};

#include "ControlEditCommandsTestParts1.h"
#include "ControlEditCommandsTestParts2.h"

QTEST_GUILESS_MAIN(ControlEditCommandsTest)
#include "ControlEditCommandsTest.moc"
