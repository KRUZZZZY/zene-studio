/*
 * LinkedClipGroupEditsTest.cpp - the 2026-10-09 owner ruling keeps linked clip geometry
 *                                per-instance: resize, range split, overlap trim and VCA edits
 *                                touch only their selected windows, in one undo step.
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

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QPair>

#include <algorithm>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ClipLinks.h"
#include "ControlClipLinkSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVcaTestSupport.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace revtest;
using namespace vcatest;

namespace
{

Clip* clipOf(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	return control::resolveClip(id, &ref, &error) ? ref.clip : nullptr;
}

//! One clip of \p length at \p position on a fresh instrument track; (track id, clip id).
QPair<QString, QString> clipAt(int position, int length)
{
	const QString track = addTrack(QStringLiteral("instrument"));
	return {track, addClip(track, position, length)};
}

bool link(const QStringList& clips)
{
	QJsonArray others;
	for (int i = 1; i < clips.size(); ++i) { others.append(clips.at(i)); }
	return run(QStringLiteral("clip.link_create"),
		{{QStringLiteral("clip"), clips.first()}, {QStringLiteral("clips"), others}}).ok;
}

int undoDepth() { return Engine::projectJournal()->undoDepth(); }

int totalClips()
{
	int count = 0;
	for (Track* track : Engine::getSong()->tracks()) { count += track->numOfClips(); }
	return count;
}

} // namespace

class LinkedClipGroupEditsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 4) { Engine::mixer()->createChannel(); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		clearGroups();
	}

	//! The owner ruling changes the former apply-to-all assertion: clip.resize changes only the named window.
	void aResizeOfOneMemberChangesOnlyItsWindowInOneStep()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		const QString c = clipAt(1536, 384).second;
		QVERIFY(link({a, b, c}));
		const int depth = undoDepth();
		QVERIFY(run(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), b}, {QStringLiteral("length"), 200}}).ok);
		QCOMPARE(clipOf(a)->length().getTicks(), 384);
		QCOMPARE(clipOf(b)->length().getTicks(), 200);
		QCOMPARE(clipOf(c)->length().getTicks(), 384);
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		for (const QString& id : {a, b, c}) { QCOMPARE(clipOf(id)->length().getTicks(), 384); }
	}

	void aResizeThatWouldHaveEmptiedAPeerChangesOnlyTheTarget()
	{
		const QString a = clipAt(0, 384).second;
		const QString shortOne = clipAt(1000, 100).second;
		QVERIFY(link({a, shortOne}));
		QVERIFY(run(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), a}, {QStringLiteral("length"), 200}}).ok);
		QCOMPARE(clipOf(a)->length().getTicks(), 200);
		QCOMPARE(clipOf(shortOne)->length().getTicks(), 100);
	}

	//! A trim is one undo step and does not edit the other member's window.
	void aTrimIsOneUndoStepAndLeavesThePeerAlone()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		const int depth = undoDepth();
		QVERIFY(run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), a}, {QStringLiteral("start"), 96}}).ok);
		QCOMPARE(undoDepth(), depth + 1);
		QCOMPARE(clipOf(a)->startPosition().getTicks(), 96);
		QCOMPARE(clipOf(a)->length().getTicks(), 288);
		QCOMPARE(clipOf(b)->startPosition().getTicks(), 768);
		QCOMPARE(clipOf(b)->length().getTicks(), 384);
	}

	//! clip.split edits one instance, keeps both pieces in the original group, and remains ONE step.
	void aLinkedSplitIsOneUndoStepAndKeepsBothPiecesLinked()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		const int depth = undoDepth();
		const ControlResult split = run(QStringLiteral("clip.split"), {{QStringLiteral("clip"), a}, {QStringLiteral("position"), 96}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(undoDepth(), depth + 1);
		QCOMPARE(clipOf(a)->linkId(), clipOf(b)->linkId());
		QCOMPARE(ClipLinks::group(clipOf(a)->linkId()).size(), 3);
	}

	//! The owner ruling changes the former peer-refusal assertion: only the named clip moves.
	void aTrimThatWouldPushAFormerPeerBeforeTheSongStartDoesNotMoveIt()
	{
		const QString a = clipAt(200, 384).second;
		const QString b = clipAt(50, 384).second;
		QVERIFY(link({a, b}));
		const ControlResult trimmed = run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), a}, {QStringLiteral("start"), 40}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QCOMPARE(clipOf(a)->startPosition().getTicks(), 40);
		QCOMPARE(clipOf(b)->startPosition().getTicks(), 50);
	}

	//! A trim after a slip changes only this member's source window; the peer's offset stays independent.
	void aTrimAfterASlipLeavesThePeerOffsetAlone()
	{
		const QString a = clipAt(96, 288).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		// a is head-trimmed (offset -96, as if trimmed from 0 to 96); b was never trimmed (offset 0).
		QVERIFY(run(QStringLiteral("clip.slip"), {{QStringLiteral("clip"), a}, {QStringLiteral("offset"), -96}}).ok);
		const ControlResult extend = run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), a}, {QStringLiteral("start"), 0}});
		QVERIFY2(extend.ok, qPrintable(extend.errorMessage));
		QCOMPARE(clipOf(a)->startPosition().getTicks(), 0);
		QCOMPARE(clipOf(a)->startTimeOffset().getTicks(), 0);
		QCOMPARE(clipOf(b)->startTimeOffset().getTicks(), 0);
		QCOMPARE(clipOf(b)->startPosition().getTicks(), 768);
	}

	//! The owner ruling changes the former group-split assertion: range.split cuts selected tracks only.
	void aRangeSplitLeavesLinkedPeersOnOtherTracksWhole()
	{
		const auto [trackA, a] = clipAt(0, 384);
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		Clip* pb = clipOf(b);
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 96}, {QStringLiteral("end"), 200},
			{QStringLiteral("tracks"), QJsonArray{trackA}}}).ok);
		const int depth = undoDepth();
		QVERIFY(run(QStringLiteral("range.split")).ok);
		QCOMPARE(undoDepth(), depth + 1);
		QCOMPARE(totalClips(), 4);  // only a was cut at the range's two edges
		QCOMPARE(pb->length().getTicks(), 384);
		Track* other = pb->getTrack();
		QCOMPARE(other->numOfClips(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(totalClips(), 2);
	}

	//! The owner ruling changes the former group-split assertion: splitClipAt adds only one linked piece.
	void splitClipAtLeavesTheOtherWindowWhole()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		Clip* pb = clipOf(b);
		QVERIFY(arrangement::splitClipAt(clipOf(a), 96) != nullptr);
		QCOMPARE(totalClips(), 3);
		QCOMPARE(pb->length().getTicks(), 384);
		QCOMPARE(ClipLinks::group(clipOf(a)->linkId()).size(), 3);
	}

	//! An overlap trim cuts only the instance under the drop; both surviving pieces keep the shared link.
	void anOverlapTrimLeavesBothPiecesLinkedAndPeersWhole()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		Clip* pa = clipOf(a);
		Clip* pb = clipOf(b);
		const int group = pa->linkId();
		Clip* dropped = pa->getTrack()->createClip(TimePos(100));
		dropped->changeLength(TimePos(100));
		QCOMPARE(arrangement::applyOverlap(dropped, arrangement::Overlap::Trim), 1);
		QCOMPARE(pb->length().getTicks(), 384);
		QCOMPARE(totalClips(), 4);  // a became two pieces around the dropped clip; b is whole
		QCOMPARE(pa->length().getTicks(), 100);
		QCOMPARE(pa->linkId(), group);
		QCOMPARE(ClipLinks::group(group).size(), 3);  // left piece, tail, and b
		Clip* tail = nullptr;
		for (Clip* clip : pa->getTrack()->getClips()) { if (clip->startPosition().getTicks() == 200) { tail = clip; } }
		QVERIFY(tail != nullptr);
		QCOMPARE(tail->linkId(), group);
	}

	//! A locked split cuts clips explicitly in the lock; linked members outside it keep their windows.
	void aLockedSplitLeavesLinkedPeersOutsideTheLockWhole()
	{
		const auto [trackA, a] = clipAt(1000, 500);
		const auto [trackC, c] = clipAt(1000, 500);
		Clip* pb = clipOf(clipAt(4000, 500).second);
		Clip* pd = clipOf(clipAt(6000, 500).second);
		const QString b = control::clipIdOf(pb);
		const QString d = control::clipIdOf(pd);
		QVERIFY(link({a, b}));  // b is outside the lock
		QVERIFY(link({c, d}));  // d is outside the lock too; c and a are both locked
		const QString group = createGroup(QStringLiteral("Lock"));
		for (const QString& track : {trackA, trackC})
		{
			run(QStringLiteral("vca.track_add"), {{QStringLiteral("group"), group}, {QStringLiteral("track"), track}});
		}
		const int depth = undoDepth();
		const ControlResult split = run(QStringLiteral("vca.edit_split"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), a}, {QStringLiteral("position"), 1200}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(undoDepth(), depth + 1);
		QCOMPARE(totalClips(), 6);  // only the two locked clips are cut
		QCOMPARE(pb->length().getTicks(), 500);
		QCOMPARE(pb->startPosition().getTicks(), 4000);
		QCOMPARE(pd->length().getTicks(), 500);
		QCOMPARE(pd->startPosition().getTicks(), 6000);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(totalClips(), 4);
	}

	//! A locked trim changes locked instances, but not linked peers outside the lock.
	void aLockedTrimLeavesLinkedPeersOutsideTheLockAlone()
	{
		const auto [trackA, a] = clipAt(1000, 500);
		const auto [trackC, c] = clipAt(1000, 500);
		Clip* pb = clipOf(clipAt(4000, 500).second);
		Clip* pc = clipOf(c);
		const QString b = control::clipIdOf(pb);
		QVERIFY(link({a, b}));
		const QString group = createGroup(QStringLiteral("Lock"));
		for (const QString& track : {trackA, trackC})
		{
			run(QStringLiteral("vca.track_add"), {{QStringLiteral("group"), group}, {QStringLiteral("track"), track}});
		}
		const ControlResult trimmed = run(QStringLiteral("vca.edit_trim"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), a}, {QStringLiteral("start"), 1100}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QCOMPARE(pb->startPosition().getTicks(), 4000);
		QCOMPARE(pb->length().getTicks(), 500);
		QCOMPARE(pc->startPosition().getTicks(), 1100);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pb->startPosition().getTicks(), 4000);
		QCOMPARE(pb->length().getTicks(), 500);
	}
};

QTEST_MAIN(LinkedClipGroupEditsTest)
#include "LinkedClipGroupEditsTest.moc"
