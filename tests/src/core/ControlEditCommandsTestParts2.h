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

// Complete fixture method definitions; included once after the fixture declaration.

void ControlEditCommandsTest::headlessSelectionApplyReportsClipsSkipped() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult addedTrack = registry->invoke(QStringLiteral("track.add"),
		QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")}});
	QVERIFY2(addedTrack.ok, qPrintable(addedTrack.errorMessage));
	const QString track = addedTrack.result.value(QStringLiteral("track")).toString();

	const ControlResult firstAdded = registry->invoke(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 192}});
	QVERIFY2(firstAdded.ok, qPrintable(firstAdded.errorMessage));
	const ControlResult secondAdded = registry->invoke(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 384},
			{QStringLiteral("length"), 192}});
	QVERIFY2(secondAdded.ok, qPrintable(secondAdded.errorMessage));
	const QJsonArray clips{firstAdded.result.value(QStringLiteral("clip")),
		secondAdded.result.value(QStringLiteral("clip"))};

	const ControlResult saved = registry->invoke(QStringLiteral("selection.save"),
		QJsonObject{{QStringLiteral("name"), QStringLiteral("headless-multiple-clips")},
			{QStringLiteral("clips"), clips}});
	QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
	const ControlResult applied = registry->invoke(QStringLiteral("selection.apply"),
		QJsonObject{{QStringLiteral("name"), QStringLiteral("headless-multiple-clips")}});
	QVERIFY2(applied.ok, qPrintable(applied.errorMessage));

	QCOMPARE(applied.result.value(QStringLiteral("clips")).toArray(), clips);
	QCOMPARE(applied.result.value(QStringLiteral("missing")).toInt(), 0);
	QVERIFY(!applied.result.value(QStringLiteral("restored")).toArray().contains(QStringLiteral("clips")));
	QCOMPARE(applied.result.value(QStringLiteral("skipped")).toArray(), QJsonArray{QStringLiteral("clips")});

	const ControlResult state = registry->invoke(QStringLiteral("arrangement.get_state"));
	QVERIFY2(state.ok, qPrintable(state.errorMessage));
	QCOMPARE(state.result.value(QStringLiteral("selected_clip")).toString(), clips.first().toString());
}
void ControlEditCommandsTest::trackAddUndoFindsItsTrackAfterARemovalIsUndone() {
	const QString track = revtest::addTrack(QStringLiteral("sample"));
	QVERIFY(!track.isEmpty());
	QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(track));
}
void ControlEditCommandsTest::trackSetSoloRepeatedInHeadlessModeIsIdempotent() {
	ControlRegistry* registry = ControlRegistry::instance();
	const QString lead = revtest::addTrack();
	const QString other = revtest::addTrack();
	QVERIFY(!lead.isEmpty());
	QVERIFY(!other.isEmpty());

	// Pre-solo: both unmuted, so the unsolo at the end has something to restore.
	const ControlResult mutedOther = registry->invoke(QStringLiteral("track.set_mute"),
		{{QStringLiteral("track"), other}, {QStringLiteral("muted"), false}});
	QVERIFY2(mutedOther.ok, qPrintable(mutedOther.errorMessage));

	const ControlResult soloed = registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), lead}, {QStringLiteral("solo"), true}});
	QVERIFY2(soloed.ok, qPrintable(soloed.errorMessage));
	QVERIFY(muted(other));
	QVERIFY(!muted(lead));

	// The same request again: the track is already soloed, so nothing may change.
	const ControlResult again = registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), lead}, {QStringLiteral("solo"), true}});
	QVERIFY2(again.ok, qPrintable(again.errorMessage));

	const ControlResult unsoloed = registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), lead}, {QStringLiteral("solo"), false}});
	QVERIFY2(unsoloed.ok, qPrintable(unsoloed.errorMessage));
	QVERIFY2(!muted(other), "the second solo recaptured the post-solo mute states as "
		"the before-solo snapshot, so unsoloing could not restore them");
}
void ControlEditCommandsTest::trackSetArmManyReportsOnlyTheTracksItChanged() {
	QTemporaryDir recoveryDir;
	QVERIFY(recoveryDir.isValid());
	const QString oldWorkingDir = ConfigManager::inst()->workingDir();
	const auto restoreWorkingDir = qScopeGuard([oldWorkingDir] {
		ConfigManager::inst()->setWorkingDir(oldWorkingDir);
	});
	ConfigManager::inst()->setWorkingDir(recoveryDir.path());
	ControlRegistry* registry = ControlRegistry::instance();
	const QString first = revtest::addTrack();
	const QString second = revtest::addTrack();
	QVERIFY(!first.isEmpty());
	QVERIFY(!second.isEmpty());
	const QJsonArray both{first, second};

	const ControlResult armed = registry->invoke(QStringLiteral("track.set_arm_many"),
		{{QStringLiteral("tracks"), both}, {QStringLiteral("armed"), true}});
	QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
	QCOMPARE(armed.result.value(QStringLiteral("tracks")).toArray().size(), 2);

	const ControlResult disarmed = registry->invoke(QStringLiteral("track.set_arm_many"),
		{{QStringLiteral("tracks"), both}, {QStringLiteral("armed"), false}});
	QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
	QCOMPARE(disarmed.result.value(QStringLiteral("tracks")).toArray().size(), 2);

	// Both routes are disarmed already: nothing changed, so nothing may be
	// listed as changed.
	const ControlResult noop = registry->invoke(QStringLiteral("track.set_arm_many"),
		{{QStringLiteral("tracks"), both}, {QStringLiteral("armed"), false}});
	QVERIFY2(!noop.ok, "a disarm of already-disarmed routes reported changed tracks");
	QCOMPARE(noop.errorKind, ControlErrorKind::Busy);

	// A repeated id is one track: armed once, disarmed once, reported once.
	const QString third = revtest::addTrack();
	QVERIFY(!third.isEmpty());
	const QJsonArray twice{third, third};
	QVERIFY(registry->invoke(QStringLiteral("track.set_arm_many"),
		{{QStringLiteral("tracks"), twice}, {QStringLiteral("armed"), true}}).ok);
	const ControlResult once = registry->invoke(QStringLiteral("track.set_arm_many"),
		{{QStringLiteral("tracks"), twice}, {QStringLiteral("armed"), false}});
	QVERIFY2(once.ok, qPrintable(once.errorMessage));
	const QJsonArray reported = once.result.value(QStringLiteral("tracks")).toArray();
	QCOMPARE(reported.size(), 1);
	QCOMPARE(reported.first().toString(), third);
}
void ControlEditCommandsTest::trackSetArmManyRejectsNonStringIdsBeforeArming() {
	ControlRegistry* registry = ControlRegistry::instance();
	const QString track = revtest::addTrack();
	QVERIFY(!track.isEmpty());
	QTemporaryDir recoveryDir;
	QVERIFY(recoveryDir.isValid());
	const QString oldWorkingDir = ConfigManager::inst()->workingDir();
	ConfigManager::inst()->setWorkingDir(recoveryDir.path());

	const ControlResult malformed = registry->invoke(QStringLiteral("track.set_arm_many"),
		QJsonObject{{QStringLiteral("tracks"), QJsonArray{track, 7}},
			{QStringLiteral("armed"), true}});
	// Capture whether the malformed call armed the valid first entry, then clean up
	// before asserting so the RED run leaves the recorder disarmed as well.
	const ControlResult cleanup = registry->invoke(QStringLiteral("track.set_arm"),
		QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("armed"), false}});
	ConfigManager::inst()->setWorkingDir(oldWorkingDir);

	QVERIFY2(!malformed.ok, qPrintable(QStringLiteral("malformed batch unexpectedly succeeded; valid route was armed before cleanup: %1")
		.arg(cleanup.result.value(QStringLiteral("armed_before")).toBool()
			? QStringLiteral("yes") : QStringLiteral("no"))));
	QCOMPARE(malformed.errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY2(cleanup.ok, qPrintable(cleanup.errorMessage));
	QVERIFY(!cleanup.result.value(QStringLiteral("armed_before")).toBool());
}
void ControlEditCommandsTest::trackSetSoloNoOpRequestInvertsToTheStateItFound() {
	ControlRegistry* registry = ControlRegistry::instance();
	const QString track = revtest::addTrack();
	QVERIFY(!track.isEmpty());
	QVERIFY(registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), track}, {QStringLiteral("solo"), true}}).ok);

	const ControlResult repeat = registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), track}, {QStringLiteral("solo"), true}});
	QVERIFY2(repeat.ok, qPrintable(repeat.errorMessage));
	// The reply itself never carries the transaction (finishResult strips
	// __transaction), so the recorded one is what a client reads back.
	QCOMPARE(revtest::stateOf(QStringLiteral("track.set_solo"))
		.value(QStringLiteral("inverse")).toObject()
		.value(QStringLiteral("args")).toObject()
		.value(QStringLiteral("solo")).toBool(), true);

	const ControlResult unsoloed = registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), track}, {QStringLiteral("solo"), false}});
	QVERIFY(unsoloed.ok);
	const ControlResult repeatUnsolo = registry->invoke(QStringLiteral("track.set_solo"),
		{{QStringLiteral("track"), track}, {QStringLiteral("solo"), false}});
	QVERIFY2(repeatUnsolo.ok, qPrintable(repeatUnsolo.errorMessage));
	QCOMPARE(revtest::stateOf(QStringLiteral("track.set_solo"))
		.value(QStringLiteral("inverse")).toObject()
		.value(QStringLiteral("args")).toObject()
		.value(QStringLiteral("solo")).toBool(), false);
}
void ControlEditCommandsTest::clipAddOnAFolderTrackIsRefusedWithoutAnUndoStep() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult addedFolder = registry->invoke(QStringLiteral("track.add"),
		QJsonObject{{QStringLiteral("type"), QStringLiteral("folder")}});
	QVERIFY2(addedFolder.ok, qPrintable(addedFolder.errorMessage));
	const QString folder = addedFolder.result.value(QStringLiteral("track")).toString();

	const ControlResult depthBefore = registry->invoke(QStringLiteral("control.undo_depth"), QJsonObject{});
	QVERIFY2(depthBefore.ok, qPrintable(depthBefore.errorMessage));

	// A name and a length are the handler's own dereferences (setName, changeLength):
	// without them the null clip is never touched and the old code only got as far
	// as a NotFound reply.
	const ControlResult added = registry->invoke(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), folder}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 192}, {QStringLiteral("name"), QStringLiteral("Kick")}});
	QVERIFY2(!added.ok, "a clip was added to a folder track");
	QCOMPARE(added.errorKind, ControlErrorKind::Refused);

	const ControlResult depthAfter = registry->invoke(QStringLiteral("control.undo_depth"), QJsonObject{});
	QVERIFY2(depthAfter.ok, qPrintable(depthAfter.errorMessage));
	QCOMPARE(depthAfter.result.value(QStringLiteral("depth")).toInt(),
		depthBefore.result.value(QStringLiteral("depth")).toInt());
}
