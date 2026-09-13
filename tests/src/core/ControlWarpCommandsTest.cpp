/*
 * ControlWarpCommandsTest.cpp - the warp.* command group (SPEC-zene-studio.md
 *                                A11-A16): the warp-marker editing surface.
 *
 * The warp ENGINE landed with #597 (include/WarpMarkers.h, the <warp> child
 * element, docs/WARP.md). What this file holds to account is the SURFACE the
 * release contract section 3.1 requires of it: five registered commands with
 * argument and result schemas, typed refusals, and - the part that is not
 * optional - a reversibility class whose inverse actually works.
 *
 * The load-bearing case is `markerEditsAreReversibleThroughTheJournal`. A warp
 * edit's checkpoint is the CLIP's state at the moment before the edit, and #597
 * writes the <warp> element only for a clip that is warped - so the checkpoint
 * of the FIRST marker added carries no <warp> element at all. If restoring that
 * state does not clear the map, control.undo reports success and leaves the
 * marker in place. That is what this test measures, and it is why
 * SampleClip::loadSettings resets the warp when the element is absent.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

//! Invoke a command through the registry, exactly as the socket does.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

/*! A fresh SAMPLECLIP, created through the product's own commands.
 *
 * track.add type=sample + clip.add is how an agent creates the clip in the
 * first place, so the fixture needs no project file and the clip id comes back
 * from the command rather than from a position this test guesses.
 */
QString makeWarpClip()
{
	const ControlResult track = run(QStringLiteral("track.add"),
		{{QStringLiteral("type"), QStringLiteral("sample")},
			{QStringLiteral("name"), QStringLiteral("Warp Target")}});
	if (!track.ok) { return QString(); }
	const ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track.result.value(QStringLiteral("track"))},
			{QStringLiteral("position"), 0}});
	if (!clip.ok) { return QString(); }
	return clip.result.value(QStringLiteral("clip")).toString();
}

QJsonArray markersOf(const QString& clip)
{
	return run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
		.result.value(QStringLiteral("markers")).toArray();
}

int markerCountOf(const QString& clip)
{
	return run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
		.result.value(QStringLiteral("marker_count")).toInt();
}

//! The offset the marker pinned to \a sourceFrame reports, or -1 when absent.
qint64 offsetOf(const QString& clip, qint64 sourceFrame)
{
	for (const QJsonValue& value : markersOf(clip))
	{
		const QJsonObject marker = value.toObject();
		if (marker.value(QStringLiteral("source_frame")).toDouble() == static_cast<double>(sourceFrame))
		{
			return static_cast<qint64>(marker.value(QStringLiteral("offset_ticks")).toDouble());
		}
	}
	return -1;
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

} // namespace

class ControlWarpCommandsTest : public QObject
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

	//! Every command of the group declares the contract's parts: a group.verb
	//! id, both schemas, a description and an empty `requires` (headless-safe).
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList mutating = {
			QStringLiteral("warp.add"), QStringLiteral("warp.move"),
			QStringLiteral("warp.remove"), QStringLiteral("warp.set")};
		const QStringList all = QStringList{ QStringLiteral("warp.list") } + mutating;
		for (const QString& id : all)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("warp"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			// SPEC A13 headless parity: the declaration exists and is empty.
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QCOMPARE(cmd->mutating, mutating.contains(id));
		}
	}

	//! A clip no marker has touched reports an empty map AND the defaults the
	//! engine's own header documents: follow the project tempo, no source tempo.
	void listReportsAnUnwarpedClip()
	{
		const QString clip = makeWarpClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		const ControlResult listed = run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("clip")).toString(), clip);
		QCOMPARE(listed.result.value(QStringLiteral("marker_count")).toInt(), 0);
		QCOMPARE(listed.result.value(QStringLiteral("warped")).toBool(), false);
		QCOMPARE(listed.result.value(QStringLiteral("tempo_mode")).toString(), QStringLiteral("follow"));
		QCOMPARE(listed.result.value(QStringLiteral("source_tempo")).toDouble(), 0.0);
		QCOMPARE(listed.result.value(QStringLiteral("max_markers")).toInt(), WarpMarkers::MaxMarkers);
		QCOMPARE(listed.result.value(QStringLiteral("markers")).toArray().size(), 0);
	}

	//! add -> the map in source-frame order, with the literal values given;
	//! move -> the same source frame at a new offset; remove -> the map minus it.
	void addMoveRemoveEditsTheMap()
	{
		const QString clip = makeWarpClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");

		// Added OUT of order on purpose: the engine holds the set sorted by
		// source frame, so the read-back order is the engine's, not the call's.
		QVERIFY2(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400},
				{QStringLiteral("offset_ticks"), 96}}).ok, "warp.add refused the second marker");
		QVERIFY2(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 24}}).ok, "warp.add refused the first marker");

		const QJsonArray markers = markersOf(clip);
		QCOMPARE(markers.size(), 2);
		QCOMPARE(markers.at(0).toObject().value(QStringLiteral("source_frame")).toInt(), 44100);
		QCOMPARE(markers.at(0).toObject().value(QStringLiteral("offset_ticks")).toInt(), 24);
		QCOMPARE(markers.at(0).toObject().value(QStringLiteral("index")).toInt(), 0);
		QCOMPARE(markers.at(1).toObject().value(QStringLiteral("source_frame")).toInt(), 176400);
		QCOMPARE(markers.at(1).toObject().value(QStringLiteral("offset_ticks")).toInt(), 96);
		QCOMPARE(markerCountOf(clip), 2);
		QCOMPARE(run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
			.result.value(QStringLiteral("warped")).toBool(), true);

		const ControlResult moved = run(QStringLiteral("warp.move"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400},
				{QStringLiteral("offset_ticks"), 60}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(offsetOf(clip, 176400), qint64(60));
		QCOMPARE(offsetOf(clip, 44100), qint64(24));
		QCOMPARE(moved.result.value(QStringLiteral("moved")).toObject()
			.value(QStringLiteral("previous_offset_ticks")).toInt(), 96);

		const ControlResult removed = run(QStringLiteral("warp.remove"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("was_last_marker")).toBool(), false);
		QCOMPARE(markerCountOf(clip), 1);
		QCOMPARE(offsetOf(clip, 44100), qint64(-1));

		// The last marker out leaves the clip unwarped: the engine's empty map is
		// what makes the mapping the linear one again (docs/WARP.md section 2.3).
		QVERIFY2(run(QStringLiteral("warp.remove"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400}}).ok,
			"warp.remove refused the last marker");
		QCOMPARE(markerCountOf(clip), 0);
		QCOMPARE(run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
			.result.value(QStringLiteral("warped")).toBool(), false);
	}

	//! warp.set is the whole-map call: a marker list in one argument, the tempo
	//! mode, and the source tempo the 'source' mode leads with.
	void setReplacesTheMapAndTheTempoMode()
	{
		const QString clip = makeWarpClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");

		const ControlResult set = run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip},
				{QStringLiteral("markers"), QJsonArray{
					QJsonObject{{QStringLiteral("source_frame"), 88200},
						{QStringLiteral("offset_ticks"), 48}}}}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(markerCountOf(clip), 1);
		QCOMPARE(offsetOf(clip, 88200), qint64(48));

		const ControlResult leader = run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("source")},
				{QStringLiteral("source_tempo"), 128.5}});
		QVERIFY2(leader.ok, qPrintable(leader.errorMessage));
		QCOMPARE(leader.result.value(QStringLiteral("tempo_mode")).toString(), QStringLiteral("source"));
		QCOMPARE(leader.result.value(QStringLiteral("source_tempo")).toDouble(), 128.5);
		// The marker list is untouched by a mode-only call.
		QCOMPARE(markerCountOf(clip), 1);

		// An empty list is how a caller clears the map without deleting the clip.
		const ControlResult cleared = run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip},
				{QStringLiteral("markers"), QJsonArray()}});
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QCOMPARE(markerCountOf(clip), 0);
		QCOMPARE(cleared.result.value(QStringLiteral("tempo_mode")).toString(), QStringLiteral("source"));
	}

	//! Every refusal is a typed error with a message, and it changes nothing.
	void refusalsAreTypedAndChangeNothing()
	{
		const QString clip = makeWarpClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 24}}).ok);

		const ControlResult duplicate = run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 96}});
		QCOMPARE(duplicate.ok, false);
		QCOMPARE(duplicate.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!duplicate.errorMessage.isEmpty());

		// A frame BETWEEN the existing ones with an offset below it: sorted into
		// the set it would put the map out of order, so the engine refuses it.
		const ControlResult disorder = run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 88200},
				{QStringLiteral("offset_ticks"), 1}});
		QCOMPARE(disorder.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult missing = run(QStringLiteral("warp.move"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 999999},
				{QStringLiteral("offset_ticks"), 5}});
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("warp.remove"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 999999}}).errorKind,
			ControlErrorKind::NotFound);

		// A move must keep the marker strictly between its neighbours: 12 is
		// below the marker that precedes it at 44100/24.
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400},
				{QStringLiteral("offset_ticks"), 96}}).ok);
		const ControlResult crossing = run(QStringLiteral("warp.move"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400},
				{QStringLiteral("offset_ticks"), 12}});
		QCOMPARE(crossing.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(crossing.errorMessage.contains(QStringLiteral("strictly between")));

		// 'mode' is a closed set: anything but 'follow' or 'source' is refused.
		const ControlResult badMode = run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("tempo")}});
		QCOMPARE(badMode.errorKind, ControlErrorKind::InvalidArgs);

		// A warp.set with nothing to set is refused rather than silently doing
		// nothing (an edit that changes nothing is not an edit - I4).
		const ControlResult empty = run(QStringLiteral("warp.set"), {{QStringLiteral("clip"), clip}});
		QCOMPARE(empty.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult negative = run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), -1},
				{QStringLiteral("offset_ticks"), 5}});
		QCOMPARE(negative.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult malformed = run(QStringLiteral("warp.list"),
			{{QStringLiteral("clip"), QStringLiteral("not-a-clip")}});
		QCOMPARE(malformed.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("warp.list"),
			{{QStringLiteral("clip"), QStringLiteral("clip-9999")}}).errorKind,
			ControlErrorKind::NotFound);

		// Nothing above wrote anything: the map is exactly what the two accepted
		// adds left behind.
		QCOMPARE(markerCountOf(clip), 2);
		QCOMPARE(offsetOf(clip, 44100), qint64(24));
		QCOMPARE(offsetOf(clip, 176400), qint64(96));
	}

	/*! A clip that is not a SampleClip is refused with the typed `refused`, not
	 *  with an empty success: a MIDI clip has no audio for a marker to pin.
	 */
	void aNonSampleClipIsRefusedTyped()
	{
		const ControlResult track = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const ControlResult clip = run(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), track.result.value(QStringLiteral("track"))},
				{QStringLiteral("position"), 0}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		const QString id = clip.result.value(QStringLiteral("clip")).toString();

		const ControlResult listed = run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), id}});
		QCOMPARE(listed.ok, false);
		QCOMPARE(listed.errorKind, ControlErrorKind::Refused);
		QVERIFY(listed.errorMessage.contains(QStringLiteral("not a sample clip")));
	}

	//! The contract table classifies the group, and the class the handlers claim
	//! is the class the registry stamps (SPEC A16: one definition, no drift).
	void contractRowsClassifyTheGroup()
	{
		const QStringList mutating = {
			QStringLiteral("warp.add"), QStringLiteral("warp.move"),
			QStringLiteral("warp.remove"), QStringLiteral("warp.set")};
		for (const QString& id : mutating)
		{
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
			QVERIFY(row->reversible);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}
		const control::ReversibilityEntry* listed = contractRow(QStringLiteral("warp.list"));
		QVERIFY(listed != nullptr);
		QCOMPARE(control::reversibilityClassName(listed->cls), QStringLiteral("not_mutating"));

		const QString clip = makeWarpClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 24}}).ok);
		const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
		QVERIFY(tx != nullptr);
		QCOMPARE(tx->command, QStringLiteral("warp.add"));
		QCOMPARE(tx->cls, QStringLiteral("true_inverse"));
		QCOMPARE(tx->reversible, true);
		QVERIFY2(tx->mechanism.contains(QStringLiteral("Clip checkpoint")),
			qPrintable(tx->mechanism));
		// The recorded inverse names a real command a reader can re-issue by hand.
		QCOMPARE(tx->inverse.value(QStringLiteral("op")).toString(), QStringLiteral("warp.set"));
	}

	/*! THE A16 PROOF, and the reason SampleClip::loadSettings resets the warp
	 *  when the clip carries no <warp> element. Each case applies an edit and
	 *  then asks control.undo to take it back, reading the map through warp.list
	 *  afterwards - the same behaviour an agent gets over the socket.
	 */
	void markerEditsAreReversibleThroughTheJournal()
	{
		const QString clip = makeWarpClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");

		// (1) The FIRST marker: its checkpoint is an unwarped clip, i.e. a state
		// with no <warp> element at all.
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 24}}).ok);
		QCOMPARE(markerCountOf(clip), 1);
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the warp edit");
		QCOMPARE(markerCountOf(clip), 0);

		// (2) A second marker is taken back to the first, not to nothing.
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 24}}).ok);
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400},
				{QStringLiteral("offset_ticks"), 96}}).ok);
		QCOMPARE(markerCountOf(clip), 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(markerCountOf(clip), 1);
		QCOMPARE(offsetOf(clip, 44100), qint64(24));

		// (3) A move, and a remove.
		QVERIFY(run(QStringLiteral("warp.move"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100},
				{QStringLiteral("offset_ticks"), 48}}).ok);
		QCOMPARE(offsetOf(clip, 44100), qint64(48));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(offsetOf(clip, 44100), qint64(24));
		QVERIFY(run(QStringLiteral("warp.add"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 176400},
				{QStringLiteral("offset_ticks"), 96}}).ok);
		QVERIFY(run(QStringLiteral("warp.remove"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("source_frame"), 44100}}).ok);
		QCOMPARE(markerCountOf(clip), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(markerCountOf(clip), 2);
		QCOMPARE(offsetOf(clip, 44100), qint64(24));

		// (4) One warp.set writes the map, the mode and the tempo; ONE undo
		// restores all three (SPEC A16 deliverable 3: one command, one step).
		QVERIFY(run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("source")},
				{QStringLiteral("source_tempo"), 140.0}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const ControlResult back = run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}});
		QCOMPARE(back.result.value(QStringLiteral("tempo_mode")).toString(), QStringLiteral("follow"));
		QCOMPARE(back.result.value(QStringLiteral("source_tempo")).toDouble(), 0.0);

		// (5) Undo all the way back to the unwarped clip, so the chain is the
		// inverse of every step and not only of the last one.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(markerCountOf(clip), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(markerCountOf(clip), 0);
	}
};

QTEST_GUILESS_MAIN(ControlWarpCommandsTest)
#include "ControlWarpCommandsTest.moc"
