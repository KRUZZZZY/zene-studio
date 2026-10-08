/*
 * LinkedClipGroupEditsTest.cpp - round 2 of the linked-clip decision (2026-10-08): every trim and every
 *                                split reaches the whole link group, in ONE undo step - clip.resize, the range
 *                                split, the phase-locked splits and trims - and a trim no peer can take is
 *                                refused whole. LinkedClipEditsTest holds clip.trim and clip.split.
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

	//! clip.resize is a trim of the end: every member's length moves by the same delta, as ONE step.
	void aResizeOfOneMemberResizesEveryMemberInOneStep()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		const QString c = clipAt(1536, 384).second;
		QVERIFY(link({a, b, c}));
		const int depth = undoDepth();
		QVERIFY(run(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), b}, {QStringLiteral("length"), 200}}).ok);
		for (const QString& id : {a, b, c}) { QCOMPARE(clipOf(id)->length().getTicks(), 200); }
		QCOMPARE(undoDepth(), depth + 1);  // the clip's and the peers' checkpoints merged into one step
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		for (const QString& id : {a, b, c}) { QCOMPARE(clipOf(id)->length().getTicks(), 384); }
	}

	void aResizeThatWouldEmptyAPeerIsRefusedWholeAndMovesNothing()
	{
		const QString a = clipAt(0, 384).second;
		const QString shortOne = clipAt(1000, 100).second;
		QVERIFY(link({a, shortOne}));
		QVERIFY(!run(QStringLiteral("clip.resize"), {{QStringLiteral("clip"), a}, {QStringLiteral("length"), 200}}).ok);
		QCOMPARE(clipOf(a)->length().getTicks(), 384);
		QCOMPARE(clipOf(shortOne)->length().getTicks(), 100);
	}

	//! The trim command takes a checkpoint on the clip and one on the peers; the registry folds them into ONE step.
	void aTrimIsOneUndoStep()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		const int depth = undoDepth();
		QVERIFY(run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), a}, {QStringLiteral("start"), 96}}).ok);
		QCOMPARE(undoDepth(), depth + 1);
	}

	//! A peer is never left starting before the song start: the whole trim is refused.
	void aTrimThatWouldPushAPeerBeforeTheSongStartIsRefused()
	{
		const QString a = clipAt(200, 384).second;
		const QString b = clipAt(50, 384).second;
		QVERIFY(link({a, b}));
		const ControlResult refused = run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), a}, {QStringLiteral("start"), 40}});
		QVERIFY2(!refused.ok, "the peer would start at -110");
		QCOMPARE(clipOf(a)->startPosition().getTicks(), 200);
		QCOMPARE(clipOf(b)->startPosition().getTicks(), 50);
	}

	//! A peer is never extended before its own content when the edited clip is not: the group's edit is refused
	//! rather than leaving a peer in a state its own drag would have refused (a start offset above 0).
	void aTrimThatWouldExtendAPeerBeforeItsContentIsRefused()
	{
		const QString a = clipAt(96, 288).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		// a is head-trimmed (offset -96, as if trimmed from 0 to 96); b was never trimmed (offset 0).
		QVERIFY(run(QStringLiteral("clip.slip"), {{QStringLiteral("clip"), a}, {QStringLiteral("offset"), -96}}).ok);
		const ControlResult extend = run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), a}, {QStringLiteral("start"), 0}});
		QVERIFY2(!extend.ok, "b would get a start offset of +96");
		QCOMPARE(clipOf(a)->startPosition().getTicks(), 96);
		QCOMPARE(clipOf(b)->startTimeOffset().getTicks(), 0);
		QCOMPARE(clipOf(b)->startPosition().getTicks(), 768);
	}

	//! The range split is a split: the range's tracks are cut at the range edges, and the linked clips on any
	//! other track are cut with them (right halves a new group).
	void aRangeSplitReachesTheLinkedClipsOnOtherTracks()
	{
		const auto [trackA, a] = clipAt(0, 384);
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		Clip* pb = clipOf(b);
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 96}, {QStringLiteral("end"), 200},
			{QStringLiteral("tracks"), QJsonArray{trackA}}}).ok);
		QVERIFY(run(QStringLiteral("range.split")).ok);
		QCOMPARE(totalClips(), 6);  // each of the two clips cut at the range's two edges
		QCOMPARE(pb->length().getTicks(), 96);
		Track* other = pb->getTrack();
		QCOMPARE(other->numOfClips(), 3);
		// The pieces of the other member are the counterparts of the range's: 96, 104 and 184 ticks.
		QVector<int> lengths;
		for (Clip* clip : other->getClips()) { lengths.append(clip->length().getTicks()); }
		std::sort(lengths.begin(), lengths.end());
		QCOMPARE(lengths, (QVector<int>{96, 104, 184}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(totalClips(), 2);
	}

	//! splitClipAt itself is group-aware (the loop slicing and every range edit are built on it).
	void splitClipAtCutsTheGroup()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		Clip* pb = clipOf(b);
		QVERIFY(arrangement::splitClipAt(clipOf(a), 96) != nullptr);
		QCOMPARE(totalClips(), 4);
		QCOMPARE(pb->length().getTicks(), 96);
	}

	//! A drop that trims the clip under it (overlap mode Trim) cuts that clip only: its peers are not under the drop.
	void anOverlapTrimLeavesThePeersWhole()
	{
		const QString a = clipAt(0, 384).second;
		const QString b = clipAt(768, 384).second;
		QVERIFY(link({a, b}));
		Clip* pb = clipOf(b);
		Clip* dropped = clipOf(a)->getTrack()->createClip(TimePos(100));
		dropped->changeLength(TimePos(100));
		QCOMPARE(arrangement::applyOverlap(dropped, arrangement::Overlap::Trim), 1);
		QCOMPARE(pb->length().getTicks(), 384);
		QCOMPARE(totalClips(), 4);  // a became two pieces around the dropped clip; b is whole
	}

	//! A phase-locked split cuts the linked peer that is NOT in the lock, and does not cut twice a peer that is.
	void aLockedSplitCutsTheLinkedPeersOutsideTheLockAndNeverTwice()
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
		const ControlResult split = run(QStringLiteral("vca.edit_split"),
			{{QStringLiteral("group"), group}, {QStringLiteral("clip"), a}, {QStringLiteral("position"), 1200}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(totalClips(), 8);  // four clips, each cut once
		QCOMPARE(pb->length().getTicks(), 200);
		QCOMPARE(pd->length().getTicks(), 200);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(totalClips(), 4);
	}

	//! A locked trim reaches the linked peers outside the lock, and is refused whole when one cannot take it.
	void aLockedTrimReachesTheLinkedPeersOutsideTheLock()
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
		QCOMPARE(pb->startPosition().getTicks(), 4100);
		QCOMPARE(pb->length().getTicks(), 400);
		QCOMPARE(pc->startPosition().getTicks(), 1100);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pb->length().getTicks(), 500);
	}
};

QTEST_MAIN(LinkedClipGroupEditsTest)
#include "LinkedClipGroupEditsTest.moc"
