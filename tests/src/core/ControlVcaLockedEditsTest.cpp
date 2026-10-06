/*
 * ControlVcaLockedEditsTest.cpp - R3.5: the phase-locked split, trim, slip and fade
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

/*! Every media edit, not only the move, is carried by a phase-locked edit group. Each slot
 *  builds two member tracks with one clip each over the same span, locks them, edits ONE
 *  clip through the vca.edit_* verb and asserts on BOTH clips; then ONE control.undo must
 *  put both back. The refusals are held too: an edit that cannot apply to every locked clip
 *  writes nothing on any of them.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "Clip.h"
#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVcaTestSupport.h"
#include "Engine.h"
#include "Mixer.h"

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

struct Pair
{
	QString group, first, second, anchor, other;
};

//! Two member tracks of @a type, one clip each at [1000, 1500), locked into one group.
Pair lockedPair(const QString& type)
{
	clearGroups();
	Pair pair;
	pair.first = addTrack(type);
	pair.second = addTrack(type);
	pair.anchor = addClip(pair.first, 1000, 500);
	pair.other = addClip(pair.second, 1000, 500);
	pair.group = createGroup(QStringLiteral("Lock"));
	for (const QString& track : {pair.first, pair.second})
	{
		run(QStringLiteral("vca.track_add"), {{QStringLiteral("group"), pair.group}, {QStringLiteral("track"), track}});
	}
	return pair;
}

QJsonObject edit(const Pair& pair, QJsonObject args)
{
	args.insert(QStringLiteral("group"), pair.group);
	args.insert(QStringLiteral("clip"), pair.anchor);
	return args;
}

} // namespace

class ControlVcaLockedEditsTest : public QObject
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

	void aLockedSplitCutsEveryMemberAtTheSameTick()
	{
		const Pair pair = lockedPair(QStringLiteral("instrument"));
		const ControlResult split = run(QStringLiteral("vca.edit_split"), edit(pair, {{QStringLiteral("position"), 1200}}));
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(split.result.value(QStringLiteral("edited_count")).toInt(), 2);
		QCOMPARE(clipsOf(pair.first).size(), 2);
		QCOMPARE(clipsOf(pair.second).size(), 2);
		QCOMPARE(clipLength(pair.anchor), 200);
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipsOf(pair.first).size(), 1);
		QCOMPARE(clipsOf(pair.second).size(), 1);
		QCOMPARE(clipLength(pair.anchor), 500);
	}

	void aLockedTrimMovesEveryMembersEdgesByTheAnchorsDeltas()
	{
		const Pair pair = lockedPair(QStringLiteral("instrument"));
		const ControlResult trim = run(QStringLiteral("vca.edit_trim"),
			edit(pair, {{QStringLiteral("start"), 1100}, {QStringLiteral("end"), 1400}}));
		QVERIFY2(trim.ok, qPrintable(trim.errorMessage));
		QCOMPARE(trim.result.value(QStringLiteral("delta_start")).toInt(), 100);
		QCOMPARE(trim.result.value(QStringLiteral("delta_end")).toInt(), -100);
		for (const QString& clip : {pair.anchor, pair.other})
		{
			QCOMPARE(clipPosition(clip), 1100);
			QCOMPARE(clipLength(clip), 300);
		}
		QCOMPARE(clipOf(pair.other)->startTimeOffset().getTicks(), -100);
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipPosition(pair.other), 1000);
		QCOMPARE(clipLength(pair.other), 500);
	}

	void aLockedTrimThatWouldEmptyOneClipWritesNothing()
	{
		const Pair pair = lockedPair(QStringLiteral("instrument"));
		// Shorten the member first, so the anchor's own trim fits and the member's does not.
		QVERIFY(run(QStringLiteral("clip.trim"), {{QStringLiteral("clip"), pair.other},
			{QStringLiteral("start"), 1000}, {QStringLiteral("end"), 1100}}).ok);
		const ControlResult trim = run(QStringLiteral("vca.edit_trim"),
			edit(pair, {{QStringLiteral("start"), 1250}}));
		QCOMPARE(trim.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(clipPosition(pair.anchor), 1000);
		QCOMPARE(clipLength(pair.anchor), 500);
	}

	void aLockedSlipMovesEveryMembersContentByTheSameDelta()
	{
		const Pair pair = lockedPair(QStringLiteral("instrument"));
		const ControlResult slip = run(QStringLiteral("vca.edit_slip"), edit(pair, {{QStringLiteral("offset"), 48}}));
		QVERIFY2(slip.ok, qPrintable(slip.errorMessage));
		QCOMPARE(clipOf(pair.anchor)->startTimeOffset().getTicks(), 48);
		QCOMPARE(clipOf(pair.other)->startTimeOffset().getTicks(), 48);
		QCOMPARE(clipPosition(pair.other), 1000);
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipOf(pair.other)->startTimeOffset().getTicks(), 0);
	}

	void aLockedFadeSetsEveryAudioMemberAndRefusesMidi()
	{
		const Pair audio = lockedPair(QStringLiteral("sample"));
		const ControlResult fade = run(QStringLiteral("vca.edit_fade"),
			edit(audio, {{QStringLiteral("fade_in"), 96}, {QStringLiteral("fade_out"), 48}}));
		QVERIFY2(fade.ok, qPrintable(fade.errorMessage));
		for (const QString& clip : {audio.anchor, audio.other})
		{
			QCOMPARE(clipOf(clip)->clipEdits().fadeInTicks, 96);
			QCOMPARE(clipOf(clip)->clipEdits().fadeOutTicks, 48);
		}
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipOf(audio.other)->clipEdits().fadeInTicks, 0);

		const Pair midi = lockedPair(QStringLiteral("instrument"));
		const ControlResult refused = run(QStringLiteral("vca.edit_fade"), edit(midi, {{QStringLiteral("fade_in"), 96}}));
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
	}

	void anUnlockedGroupRefusesEveryEdit()
	{
		const Pair pair = lockedPair(QStringLiteral("instrument"));
		QVERIFY(run(QStringLiteral("vca.set_phase_lock"), {{QStringLiteral("group"), pair.group},
			{QStringLiteral("locked"), false}}).ok);
		for (const auto& [id, args] : {std::pair{QStringLiteral("vca.edit_split"), QJsonObject{{QStringLiteral("position"), 1200}}},
				std::pair{QStringLiteral("vca.edit_trim"), QJsonObject{{QStringLiteral("start"), 1100}}},
				std::pair{QStringLiteral("vca.edit_slip"), QJsonObject{{QStringLiteral("offset"), 10}}}})
		{
			QCOMPARE(run(id, edit(pair, args)).errorKind, ControlErrorKind::Refused);
		}
		QCOMPARE(clipsOf(pair.first).size(), 1);
	}

	//! Every per-clip entry of a locked edit's reply names the CLIP it describes,
	//! not just the track. One member track can hold several clips in the anchor's
	//! span, and then `moves` holds several entries for that one track: without a
	//! clip id the caller cannot tell which clip an entry moved.
	void everyLockedMoveAndEditEntryNamesItsClip()
	{
		const Pair pair = lockedPair(QStringLiteral("instrument"));
		// A SECOND clip on the peer track, overlapping the anchor's span: the
		// reply then carries two moves for that track, one per clip.
		const QString peerClip = addClip(pair.second, 1200, 500);
		QVERIFY2(!peerClip.isEmpty(), "the second clip on the peer track was not added");

		const ControlResult moved = run(QStringLiteral("vca.edit_move"),
			edit(pair, {{QStringLiteral("position"), 1100}}));
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		const QJsonArray moves = moved.result.value(QStringLiteral("moves")).toArray();
		QCOMPARE(moves.size(), 3);
		QStringList named;
		for (const QJsonValue& value : moves)
		{
			const QJsonObject entry = value.toObject();
			const QString clipId = entry.value(QStringLiteral("clip")).toString();
			QVERIFY2(!clipId.isEmpty(), "a moves entry names no clip");
			// The id has to resolve, or it identifies nothing.
			QVERIFY2(clipOf(clipId) != nullptr, qPrintable("moves names no live clip: " + clipId));
			named.append(clipId);
		}
		// The anchor, the peer clip the fixture made, and the one added above.
		QVERIFY(named.contains(pair.anchor));
		QVERIFY(named.contains(pair.other));
		QVERIFY(named.contains(peerClip));
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipOf(pair.anchor)->startPosition().getTicks(), TimePos(1000));
	}
};

QTEST_GUILESS_MAIN(ControlVcaLockedEditsTest)
#include "ControlVcaLockedEditsTest.moc"
