/*
 * LinkedClipEditsTest.cpp - a trim or a split of one member of a link group
 *                           applies to every member, in one undo step (owner
 *                           decision 2026-10-08, docs/LINKED-CLIPS.md section 2).
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
#include "MidiClip.h"
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

	//! The edit made on one member reaches the others: each peer's start and end move by the same
	//! deltas and its source offset moves against its start, so the audio holds its song position.
	void aTrimOfOneMemberTrimsEveryMember()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		const QString c = clipAt(1536, 384);
		QVERIFY(linkTogether({a, b, c}));
		const int group = clipLinkId(a);

		const ControlResult trimmed = run(QStringLiteral("clip.trim"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("start"), 96}});
		QVERIFY2(trimmed.ok, qPrintable(trimmed.errorMessage));
		QCOMPARE(trimmed.result.value(QStringLiteral("peers_trimmed")).toInt(), 2);

		const int starts[] = {96, 864, 1632};
		const QStringList ids = {a, b, c};
		for (int i = 0; i < 3; ++i)
		{
			const MidiClip* clip = objectOf(ids.at(i));
			QVERIFY(clip != nullptr);
			QCOMPARE(clip->startPosition().getTicks(), starts[i]);
			QCOMPARE(clip->length().getTicks(), 288);
			QCOMPARE(clip->startTimeOffset().getTicks(), -96);
			QCOMPARE(clip->linkId(), group);
		}
	}

	//! One control.undo puts every member back.
	void oneUndoRestoresEveryTrimmedMember()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		QVERIFY(linkTogether({a, b}));
		QVERIFY(run(QStringLiteral("clip.trim"), QJsonObject{{QStringLiteral("clip"), a},
			{QStringLiteral("start"), 96}, {QStringLiteral("end"), 300}}).ok);
		QCOMPARE(objectOf(b)->length().getTicks(), 204);

		QVERIFY(run(QStringLiteral("control.undo"), {}).ok);
		for (const QString& id : {a, b})
		{
			QCOMPARE(objectOf(id)->length().getTicks(), 384);
			QCOMPARE(objectOf(id)->startTimeOffset().getTicks(), 0);
		}
		QCOMPARE(objectOf(a)->startPosition().getTicks(), 0);
		QCOMPARE(objectOf(b)->startPosition().getTicks(), 768);
	}

	//! A trim a peer cannot take is refused whole: no member moves.
	void aTrimThatWouldEmptyAPeerIsRefusedWholeAndMovesNothing()
	{
		const QString a = clipAt(0, 384);
		const QString shortOne = clipAt(1000, 100);
		QVERIFY(linkTogether({a, shortOne}));
		const ControlResult refused = run(QStringLiteral("clip.trim"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("start"), 0}, {QStringLiteral("end"), 200}});
		QVERIFY(!refused.ok);
		QCOMPARE(objectOf(a)->length().getTicks(), 384);
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

	//! Every member is cut at the same clip-relative position; the left halves stay one group and the
	//! right halves form a second.
	void aSplitOfOneMemberSplitsEveryMemberIntoTwoGroups()
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
		QCOMPARE(split.result.value(QStringLiteral("members_split")).toInt(), 3);
		const int rightGroup = split.result.value(QStringLiteral("right_group")).toInt();
		QVERIFY(rightGroup > 0);
		QVERIFY(rightGroup != leftGroup);
		QCOMPARE(split.result.value(QStringLiteral("left_group")).toInt(), leftGroup);
		QCOMPARE(clipCount(), 6);

		const QVector<Clip*> lefts = membersOf(leftGroup);
		const QVector<Clip*> rights = membersOf(rightGroup);
		QCOMPARE(lefts.size(), 3);
		QCOMPARE(rights.size(), 3);
		for (const Clip* left : lefts) { QCOMPARE(left->length().getTicks(), 96); }
		for (const Clip* right : rights)
		{
			QCOMPARE(right->length().getTicks(), 288);
			QCOMPARE(right->startTimeOffset().getTicks(), -96);
		}
		QCOMPARE(rights.at(0)->startPosition().getTicks(), 96);
		QCOMPARE(rights.at(1)->startPosition().getTicks(), 864);
		QCOMPARE(rights.at(2)->startPosition().getTicks(), 1632);
	}

	//! The whole split is one undo step.
	void oneUndoJoinsEverySplitMemberAgain()
	{
		const QString a = clipAt(0, 384);
		const QString b = clipAt(768, 384);
		QVERIFY(linkTogether({a, b}));
		const int group = clipLinkId(a);
		QVERIFY(run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("position"), 96}}).ok);
		QCOMPARE(clipCount(), 4);

		QVERIFY(run(QStringLiteral("control.undo"), {}).ok);
		QCOMPARE(clipCount(), 2);
		QCOMPARE(membersOf(group).size(), 2);
		QCOMPARE(objectOf(a)->length().getTicks(), 384);
		QCOMPARE(objectOf(b)->length().getTicks(), 384);
	}

	//! A member too short to be cut there stays whole in the left group; the right halves that do exist
	//! are a group; a lone right half is not a group.
	void aMemberTooShortToBeCutStaysWholeInTheLeftGroup()
	{
		const QString a = clipAt(0, 384);
		const QString shortOne = clipAt(1000, 50);
		const QString c = clipAt(2000, 384);
		QVERIFY(linkTogether({a, shortOne, c}));
		const int leftGroup = clipLinkId(a);

		const ControlResult split = run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), a}, {QStringLiteral("position"), 96}});
		QVERIFY(split.ok);
		QCOMPARE(split.result.value(QStringLiteral("members_split")).toInt(), 2);
		QCOMPARE(membersOf(leftGroup).size(), 3);
		QCOMPARE(membersOf(split.result.value(QStringLiteral("right_group")).toInt()).size(), 2);
		QCOMPARE(objectOf(shortOne)->length().getTicks(), 50);

		const QString solo = clipAt(0, 384);
		const QString shortTwo = clipAt(1000, 50);
		QVERIFY(linkTogether({solo, shortTwo}));
		const ControlResult alone = run(QStringLiteral("clip.split"),
			QJsonObject{{QStringLiteral("clip"), solo}, {QStringLiteral("position"), 96}});
		QVERIFY(alone.ok);
		QCOMPARE(alone.result.value(QStringLiteral("members_split")).toInt(), 1);
		QCOMPARE(alone.result.value(QStringLiteral("right_group")).toInt(), 0);
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
};

QTEST_MAIN(LinkedClipEditsTest)
#include "LinkedClipEditsTest.moc"
