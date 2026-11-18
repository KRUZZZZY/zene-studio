/*
 * RecordToolsTest.cpp - comp edges and comp groups, metronome options, punch here, remaining record time
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
/*! 0.5.0 L03 (W3): a comp boundary moves and the growing take keeps its timing (ED-169); one comp
 *  choice lands on several tracks as one undo step, or on none (ED-172); the metronome's options are
 *  kept and undone, and record-only silences it outside a pass but not for a count-in (WF-081); punch
 *  in and out at the play position (WF-079); the remaining record time is read (WF-085). */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QDomDocument>
#include <QJsonArray>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <limits>
#include <sys/stat.h>

#include "AudioEngine.h"
#include "AutomationTrack.h"
#include "Clip.h"
#include "ControlCompSupport.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "MetronomeOptions.h"
#include "Mixer.h"
#include "MountNamespaceTestSupport.h"
#include "RecorderTaps.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLane.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The newest A16 record (SPEC A16 §3): what control.transactions reports for the last undo step.
QJsonObject lastRecord()
{
	const QJsonArray all = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	return all.isEmpty() ? QJsonObject() : all.last().toObject();
}

//! A take clip on @a lane over [@a start, @a start + @a length) ticks.
void addTake(SampleTrack* track, int lane, int start, int length)
{
	Clip* take = track->createClip(TimePos(start));
	take->changeLength(TimePos(length));
	take->setLaneIndex(lane);
}

//! A sample track with two take lanes, composited lane 0 then lane 1 at tick 192. The takes are real clips -
//! lane 0's over [0, 384), lane 1's over [192, 384) - since comp.move_edge asks whether the growing take
//! reaches (0.5.0 E2: a slip is relative to the take, so the takes themselves are what can refuse).
SampleTrack* twoTakeTrack()
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	const QString id = control::trackIdOf(track);
	for (int i = 0; i < 2; ++i) { run(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), id}}); }
	addTake(track, 0, 0, 384);
	addTake(track, 1, 192, 192);
	TakeLaneModel& model = track->takeLanes();
	model.selectSegment(0, 192, 0, 0);
	model.selectSegment(192, 384, 1, 0);
	return track;
}

} // namespace

class RecordToolsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	//! A test that fails leaves its routes armed, and the next test would be refused its arming
	//! because of it: every test starts from a clean recorder.
	void cleanup();

	void invalidStoredLevelUsesDefault();

	//! Cluster 5127: metronome clicks use the preallocated sample voice pool.
	void metronomeClickUsesPreparedVoice();

	void aCompEdgeMoves();

	//! A lane may hold SEVERAL takes, so the two ends of the range an edge move grows into can be
	//! supplied by DIFFERENT clips with a gap between them: asking only the two ends let the
	//! boundary move across the gap and left the composite naming a lane with no take there
	//! (comp.get_state would report it 'unresolved'). Every tick of the range must be covered.
	void anEdgeMoveRefusesAcrossAGapInTheGrowingLane();

	//! 0.5.0 E2: the growing take keeps its SLIP (a slip is the segment's, not a position that grows with the tick),
	//! so the grown range merges back into one segment; and the take refuses where its slipped read runs off its end.
	void aSlippedTakeKeepsItsSlipAcrossAnEdgeMove();

	void oneCompChoiceOnSeveralTracks();

	void aLoopedTakeBecomesPasses();

	//! Only a clip comping can hold as a take (an audio or MIDI take, comp.assign's own rule) may be
	//! cut into passes: splitPasses resolved the clip through the general resolver, so an automation
	//! clip meeting the loop conditions was cut, moved and tagged with a take lane - lanes whose
	//! composite can never resolve to it.
	void splittingPassesRefusesAClipCompingCannotHold();

	//! The last pass can be shorter than the loop, but splitPasses always selected the last lane over
	//! the WHOLE loop, so the composite named a lane with no take behind it over the uncovered tail -
	//! TakeLaneModel::takeAt() reports 'unresolved' there and the engine plays nothing for. Only the
	//! last piece's own duration may be selected.
	void splittingPassesCompsOnlyWhatTheLastPassCovers();

	//! A16 §3 on the split: comp.split_passes cut the clip, added a lane per pass and changed the
	//! composite after a Track checkpoint, but returned no __transaction - so its record carried no
	//! composite before-state and no inverse, unlike its siblings comp.move_edge and comp.select_group.
	void splittingPassesRecordsItsBeforeStateAndInverse();

	//! addLane() returns -1 once the track's lane numbers are used up (a project file can start the
	//! count at the limit). splitPasses ignored that: it cut the clip, tagged the pass past the last
	//! free number with lane -1 (which setLaneIndex clamps to the base lane) and reported success with
	//! a composite it never set. Now the lane numbers are checked before the clip is touched.
	void splittingPassesRefusesWhenTheTrackHasTooFewLaneNumbers();

	//! The same track with two passes: exactly the lane numbers left, so the split still happens and
	//! every pass lands on a lane of its own.
	void splittingPassesUsesTheLastLaneNumbersTheTrackHas();

	//! A16 §3: a mutating command records what it wrote - the composite before the edit and the
	//! inverse control.undo replays. comp.move_edge journalled the track but described nothing, so
	//! the record carried no before-state and no inverse, unlike its sibling comp.select.
	void aMovedEdgeRecordsItsBeforeStateAndInverse();

	//! A16 §3, on the multi-track half: one grouped edit is one record, and its before-state names
	//! EVERY track it wrote - comp.select_group journalled each track but returned no payload, so the
	//! record said nothing about what had changed.
	void aGroupedChoiceRecordsEveryTrackItWrote();

	void theMetronomesOptions();

	//! Luna 3949: 'edge' says which end of the pass this is, so a call without one
	//! must be refused - punchHere read every value that was not "out" as punch-IN,
	//! so {} silently armed a punch region at the play position.
	void punchHereRefusesAMissingEdge();

	void punchAtThePlayPosition();

	void aRouteTakesTheMasterOrAChannel();

	void theRemainingRecordTime();

	//! Luna 4900: the remaining time is the free space WHERE TAKES ARE WRITTEN, but
	//! record.remaining measured the recovery file's directory. An armed route writes to the
	//! take path the caller chose (record.arm_track's 'file'), which can sit on another
	//! filesystem - so the answer must be the directory the armed routes actually write.
	void theRemainingTimeMeasuresWhereTheArmedRoutesWrite();

	//! Luna 4900 (review): routes on ONE volume share its free space. Two armed directories on the same
	//! volume must divide the volume's free bytes by both routes' writing rate, not by one route each.
	void armedRoutesOnOneVolumeShareItsFreeSpace();

	//! Independent mounts that happen to share a device label (e.g. two tmpfs mounts) are distinct
	//! free-space budgets, so their routes are not pooled. (Regression for review-e39984ffb: grouping
	//! by device() merged distinct mounts.)
	void armedRoutesOnIndependentMountsDoNotShare();

	//! Multiple paths onto the same underlying filesystem (e.g. a directory and a link to it) share
	//! one free-space pool, so their routes are pooled. (Regression for review-e39984ffb: grouping
	//! by DIRECTORY split one filesystem into a budget a directory. Qt resolves a link, so a link
	//! and its target are one mount point - the case that tells the mount-point key apart is
	//! armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace.)
	void armedRoutesOnSameFilesystemCoalesce();

	//! One filesystem under TWO mount points (a bind mount, a filesystem mounted twice): one pool of
	//! free bytes under two mount-point keys. The budget belongs to the FILESYSTEM, so both routes
	//! draw on that one pool - a budget keyed by the mount point would give every route its own and
	//! double the answer.
	//! (Regression for review-e39984ffb: the rootPath() key split one filesystem into two budgets.)
	void armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace();

	//! The budget's key is the FILESYSTEM's identity, not the path. Windows names a volume by its
	//! GUID (QStorageInfo::device()), which one volume keeps under every path it is mounted at: the
	//! mount point (QStorageInfo::rootPath()) names the MOUNT, not the volume, so it is the fallback
	//! for a storage info that knows no GUID. POSIX names a filesystem by the device the kernel
	//! mounts it from (st_dev): the device LABEL is not an identity (every tmpfs mount answers
	//! "tmpfs"), so two independent filesystems keep two budgets.
	//! (Regression for review-e39984ffb: the rootPath() key split one volume into two budgets.)
	void theBudgetKeyIsTheFilesystemIdentity();

private:
	float m_levelLoadedFromInvalidConfig = 0.f;
};

#include "RecordToolsTestParts1.h"
#include "RecordToolsTestParts2.h"

int main(int argc, char* argv[])
{
	// The last test needs one filesystem under two mount points, and it makes its own mount
	// namespace for that: the kernel refuses a user namespace for a process that has threads
	// already, and the engine starts its threads right after this - so the namespace is made
	// here, before the application exists.
	testsupport::enterOwnMountNamespace();
	QApplication app(argc, argv);
	RecordToolsTest test;
	QTEST_SET_MAIN_SOURCE_PATH
	return QTest::qExec(&test, argc, argv);
}

#include "RecordToolsTest.moc"
