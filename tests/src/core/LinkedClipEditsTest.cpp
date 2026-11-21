/*
 * LinkedClipEditsTest.cpp - trims and splits edit one linked clip's window only
 *                           (owner decision 2026-10-09, docs/LINKED-CLIPS.md section 2).
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

#include "ClipLinkTestSupport.h"
#include "ClipLinks.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "LinkedClipEdits.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace cliplinktest;

namespace
{

//! A MIDI clip of \p length at \p position on a fresh instrument track; its id.
QString clipAt(int position, int length)
{
	ControlRegistry* registry = ControlRegistry::instance();
	const QString track = addTrack(QStringLiteral("instrument"));
	const ControlResult clip = registry->invoke(QStringLiteral("clip.add"),
		QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), position},
			{QStringLiteral("length"), length}});
	return clip.ok ? clip.result.value(QStringLiteral("clip")).toString() : QString();
}

MidiClip* objectOf(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	return control::resolveMidiClip(id, &ref, &error);
}

bool linkTogether(const QStringList& ids)
{
	QJsonArray others;
	for (int i = 1; i < ids.size(); ++i) { others.append(ids.at(i)); }
	return ControlRegistry::instance()->invoke(QStringLiteral("clip.link_create"),
		QJsonObject{{QStringLiteral("clip"), ids.first()}, {QStringLiteral("clips"), others}}).ok;
}

ControlResult run(const QString& command, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(command, args);
}

//! Every clip of the song that carries link group \p group.
QVector<Clip*> membersOf(int group) { return ClipLinks::group(group); }

int clipCount()
{
	int count = 0;
	for (Track* track : Engine::getSong()->tracks()) { count += track->numOfClips(); }
	return count;
}

} // namespace

class LinkedClipEditsTest : public QObject
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

	void init() { Engine::getSong()->clearProject(); }

	//! The 2026-10-09 owner ruling changes the former apply-to-all assertion: a trim edits only this instance's window.
	void aTrimOfOneMemberChangesOnlyItsWindow()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		const QString c = clipAt(1536, 384);
		QVERIFY(linkTogether({a, b, c}));
		const int group = clipLinkId(a);

		const ControlResult trimmed = run(QStringLiteral("clip.trim"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("start"), 96}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QCOMPARE(trimmed.result.value(QStringLiteral("peers_trimmed")).toInt(), 0);

		const int starts[] = {96, 768, 1536};
		const int lengths[] = {288, 384, 384};
		const int offsets[] = {-96, 0, 0};
		const QStringList ids = {a, b, c};
		for (int i = 0; i < 3; ++i)
		{
			const MidiClip* clip = objectOf(ids.at(i));
			QVERIFY(clip != nullptr);
			QCOMPARE(clip->startPosition().getTicks(), starts[i]);
			QCOMPARE(clip->length().getTicks(), lengths[i]);
			QCOMPARE(clip->startTimeOffset().getTicks(), offsets[i]);
			QCOMPARE(clip->linkId(), group);
		}
	}

	//! One control.undo restores the edited instance and leaves its peer's window intact.
	void oneUndoRestoresOnlyTheTrimmedWindow()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		QVERIFY(linkTogether({a, b}));
		QVERIFY(run(QStringLiteral("clip.trim"), QJsonObject{{QStringLiteral("clip"), a},
			{QStringLiteral("start"), 96}, {QStringLiteral("end"), 300}}).ok);
		QCOMPARE(objectOf(a)->length().getTicks(), 204);
		QCOMPARE(objectOf(b)->length().getTicks(), 384);

		QVERIFY(run(QStringLiteral("control.undo"), {}).ok);
		for (const QString& id : {a, b})
		{
			QCOMPARE(objectOf(id)->length().getTicks(), 384);
			QCOMPARE(objectOf(id)->startTimeOffset().getTicks(), 0);
		}
		QCOMPARE(objectOf(a)->startPosition().getTicks(), 0);
		QCOMPARE(objectOf(b)->startPosition().getTicks(), 768);
	}

	//! The 2026-10-09 owner ruling changes the former peer-refusal assertion: a short peer cannot block this trim.
	void aTrimThatWouldEmptyAFormerPeerStillChangesOnlyTheTarget()
	{
		const QString a = clipAt(0, 384);
		const QString shortOne = clipAt(1000, 100);
		QVERIFY(linkTogether({a, shortOne}));
		const ControlResult trimmed = run(QStringLiteral("clip.trim"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("start"), 0}, {QStringLiteral("end"), 200}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QCOMPARE(objectOf(a)->length().getTicks(), 200);
		QCOMPARE(objectOf(shortOne)->length().getTicks(), 100);
		QCOMPARE(objectOf(shortOne)->startPosition().getTicks(), 1000);
	}

	//! An unlinked clip is trimmed alone, as before.
	void anUnlinkedTrimTouchesOnlyItself()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		const ControlResult trimmed = run(QStringLiteral("clip.trim"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("start"), 96}});
		QVERIFY(trimmed.ok);
		QCOMPARE(trimmed.result.value(QStringLiteral("peers_trimmed")).toInt(), 0);
		QCOMPARE(objectOf(b)->length().getTicks(), 384);
	}

	//! The 2026-10-09 owner ruling changes the former group-split assertion: both selected pieces keep the original group.
	void aSplitKeepsBothPiecesLinkedAndLeavesOtherWindowsAlone()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		const QString c = clipAt(1536, 384);
		QVERIFY(linkTogether({a, b, c}));
		const int leftGroup = clipLinkId(a);
		QCOMPARE(clipCount(), 3);

		const ControlResult split = run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), b}, {QStringLiteral("position"), 768 + 96}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(split.result.value(QStringLiteral("members_split")).toInt(), 1);
		const int rightGroup = split.result.value(QStringLiteral("right_group")).toInt();
		QCOMPARE(rightGroup, leftGroup);
		QCOMPARE(split.result.value(QStringLiteral("left_group")).toInt(), leftGroup);
		QCOMPARE(clipCount(), 4);

		const QVector<Clip*> members = membersOf(leftGroup);
		QCOMPARE(members.size(), 4);
		QCOMPARE(objectOf(a)->length().getTicks(), 384);
		QCOMPARE(objectOf(c)->length().getTicks(), 384);
		QCOMPARE(objectOf(b)->length().getTicks(), 96);
		const MidiClip* right = objectOf(split.result.value(QStringLiteral("right")).toString());
		QVERIFY(right != nullptr);
		QCOMPARE(right->length().getTicks(), 288);
		QCOMPARE(right->startPosition().getTicks(), 768 + 96);
		QCOMPARE(right->startTimeOffset().getTicks(), -96);
		QCOMPARE(right->linkId(), leftGroup);
	}

	//! The split is one undo step and restores just the edited instance.
	void oneUndoRestoresTheSplitWindow()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		QVERIFY(linkTogether({a, b}));
		const int group = clipLinkId(a);
		QVERIFY(run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("position"), 96}}).ok);
		QCOMPARE(clipCount(), 3);  // The 2026-10-09 ruling changes the old group split to one selected instance.

		QVERIFY(run(QStringLiteral("control.undo"), {}).ok);
		QCOMPARE(clipCount(), 2);
		QCOMPARE(membersOf(group).size(), 2);
		QCOMPARE(objectOf(a)->length().getTicks(), 384);
		QCOMPARE(objectOf(b)->length().getTicks(), 384);
	}

	//! A selected split does not cut a short or long linked peer, and the right piece remains linked.
	void aSplitDoesNotCutPeersOfAnyLength()
	{
		const QString a = clipAt(0, 384);
		const QString shortOne = clipAt(1000, 50);
		const QString c = clipAt(2000, 384);
		QVERIFY(linkTogether({a, shortOne, c}));
		const int leftGroup = clipLinkId(a);

		const ControlResult split = run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("position"), 96}});
		QVERIFY(split.ok);
		QCOMPARE(split.result.value(QStringLiteral("members_split")).toInt(), 1);
		QCOMPARE(split.result.value(QStringLiteral("right_group")).toInt(), leftGroup);
		QCOMPARE(membersOf(leftGroup).size(), 4);
		QCOMPARE(objectOf(shortOne)->length().getTicks(), 50);
		QCOMPARE(objectOf(c)->length().getTicks(), 384);
	}

	//! An unlinked clip splits as before: no group fields, one cut.
	void anUnlinkedSplitIsUnchanged()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		const ControlResult split = run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("position"), 96}});
		QVERIFY(split.ok);
		QVERIFY(!split.result.contains(QStringLiteral("members_split")));
		QCOMPARE(clipCount(), 3);
		QCOMPARE(objectOf(b)->length().getTicks(), 384);
	}

	//! The trimmed member is never its own peer: adoptTrimmedContent() keeps its notes and gives each other member a copy.
	void aTrimmedMemberKeepsItsOwnNotesWhileThePeersTakeACopy()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		const QString c = clipAt(1536, 384);
		MidiClip* trimmed = objectOf(a);
		QVERIFY(trimmed != nullptr);
		trimmed->addNote(Note(TimePos(96), TimePos(0), 60), false);
		trimmed->addNote(Note(TimePos(96), TimePos(192), 64), false);
		QVERIFY(linkTogether({a, b, c}));

		int written = 0;
		{
			ClipLinks::MirroringSuspension noMirroring;
			written = ClipLinks::adoptTrimmedContent(trimmed);
		}
		QCOMPARE(written, 2);
		QCOMPARE(trimmed->notes().size(), 2);
		QCOMPARE(objectOf(b)->notes().size(), 2);
		QCOMPARE(objectOf(c)->notes().size(), 2);
	}
};

QTEST_MAIN(LinkedClipEditsTest)
#include "LinkedClipEditsTest.moc"
