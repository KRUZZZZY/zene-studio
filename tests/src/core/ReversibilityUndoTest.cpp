/*
 * ReversibilityUndoTest.cpp - the UNDO half of the SPEC A16 acceptance tests:
 *                            every reversible class is applied, undone and read
 *                            back, with the model state asserted against the
 *                            pre-command state.
 *
 * The contract itself is data (src/core/ControlReversibilityTable.cpp); this
 * file holds it to account in both directions:
 *   - every registered command has a row, and every row names a real command;
 *   - for every command the contract calls reversible, applying it and then
 *     control.undo returns the model to the pre-command state;
 *   - for a command with no inverse, control.undo FAILS with the typed
 *     'irreversible' error naming the command and its documented fallback, and
 *     does NOT quietly undo an older step instead.
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

#include <memory>

#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectControls.h"
#include "EffectChain.h"
#include "Engine.h"
#include "MasterLoudnessTap.h"
#include "Plugin.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

class ReversibilityUndoTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		// The built-in device modules live in the build tree, and the plugin
		// factory finds them through LMMS_PLUGIN_DIR - the same setup
		// ControlDeviceCatalogueTest uses, because the device commands these
		// tests exercise (plugin.load/param_set/bypass, automation.add_point)
		// need a real device to drive.
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}


	//! track.add: apply, undo, read back. ONE control.undo removes the track
	//! again, through the product's own removal path.
	void trackAddIsOneUndoableStep()
	{
		const int before = trackCount();
		const QString track = addTrack();
		QCOMPARE(trackCount(), before + 1);
		QCOMPARE(stateOf(QStringLiteral("track.add")).value(QStringLiteral("class")).toString(),
			QStringLiteral("true_inverse"));
		QCOMPARE(stateOf(QStringLiteral("track.add")).value(QStringLiteral("reversible")).toBool(), true);

		REV_UNDO_OR_FAIL();
		QCOMPARE(trackCount(), before);
		QVERIFY(!hasTrack(track));
	}


	//! track.remove: the track's own XML is captured before the delete and the
	//! undo recreates it - with its clip and its note. Doing it the other way
	//! round (remove, then look for the id) is the defect this proves absent.
	void trackRemoveRestoresItsClips()
	{
		const QString track = addTrack();
		const ControlResult clip = run(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		const QString clipId = clip.result.value(QStringLiteral("clip")).toString();
		QVERIFY(run(QStringLiteral("note.add"),
			QJsonObject{{QStringLiteral("clip"), clipId}, {QStringLiteral("key"), 64},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}}).ok);
		QCOMPARE(rollNoteCount(clipId), 1);

		const ControlResult removed = run(QStringLiteral("track.remove"),
			QJsonObject{{QStringLiteral("track"), track}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QVERIFY(!hasTrack(track));

		const QJsonObject tx = stateOf(QStringLiteral("track.remove"));
		QCOMPARE(tx.value(QStringLiteral("reversible")).toBool(), true);
		QCOMPARE(tx.value(QStringLiteral("class")).toString(), QStringLiteral("true_inverse"));

		REV_UNDO_OR_FAIL();
		QVERIFY2(hasTrack(track), qPrintable(track + " was not recreated by the undo"));
		// The recreated track carries its clips and their notes, not just its
		// name: the assertion is on the MUSIC, which is what a lost track costs.
		const QJsonArray clips = clipsOf(track);
		QCOMPARE(clips.size(), 1);
		QCOMPARE(rollNoteCount(clips.first().toString()), 1);
		QCOMPARE(noteKey(clips.first().toString(), 0), 64);
	}


	//! SPEC A16 deliverable 3, the aggregation proof. track.set_solo writes the
	//! solo flag AND every other track's mute (Track::toggleSolo, driven from
	//! the solo model). One control.undo must restore ALL of it: the assertion
	//! is on every track's mute and solo, not on the one the command named.
	void trackSetSoloIsOneUndoableStepForEveryTrack()
	{
		const QStringList tracks = {addTrack(), addTrack(), addTrack()};
		QCOMPARE(tracks.size(), 3);
		const QJsonArray pre = muteSoloState();
		const QByteArray preJson = QJsonDocument(pre).toJson(QJsonDocument::Compact);

		const ControlResult soloed = run(QStringLiteral("track.set_solo"),
			QJsonObject{{QStringLiteral("track"), tracks[1]}, {QStringLiteral("solo"), true}});
		QVERIFY2(soloed.ok, qPrintable(soloed.errorMessage));
		QVERIFY(soloed.result.value(QStringLiteral("soloed")).toBool());
		// The action really did write more than one object, or this test is
		// proving nothing.
		QVERIFY2(!muteSoloState().isEmpty(), "no tracks to observe");
		QVERIFY2(QJsonDocument(muteSoloState()).toJson(QJsonDocument::Compact) != preJson,
			"track.set_solo changed nothing observable");

		const QJsonObject tx = stateOf(QStringLiteral("track.set_solo"));
		QCOMPARE(tx.value(QStringLiteral("reversible")).toBool(), true);
		QCOMPARE(tx.value(QStringLiteral("class")).toString(), QStringLiteral("true_inverse"));

		REV_UNDO_OR_FAIL();
		QCOMPARE(QJsonDocument(muteSoloState()).toJson(QJsonDocument::Compact), preJson);
	}


	//! The scalars and model writes whose inverse is a single object
	//! checkpoint: tempo, seek, rename, mute, fader - and the clip/note flow.
	void scalarsAndFlowsUndoToTheirPreCommandState()
	{
		const int tempoBefore = run(QStringLiteral("transport.get_state"))
			.result.value(QStringLiteral("tempo")).toInt();
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), tempoBefore + 7}}).ok);
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(),
			tempoBefore + 7);
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(),
			tempoBefore);

		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 960}}).ok);
		QCOMPARE(run(QStringLiteral("transport.get_state"))
			.result.value(QStringLiteral("position_ticks")).toInt(), 960);
		REV_UNDO_OR_FAIL();
		QCOMPARE(run(QStringLiteral("transport.get_state"))
			.result.value(QStringLiteral("position_ticks")).toInt(), 0);

		const QString track = addTrack();
		QVERIFY(run(QStringLiteral("track.rename"),
			{{QStringLiteral("track"), track}, {QStringLiteral("name"), QStringLiteral("renamed")}}).ok);
		QCOMPARE(trackName(track), QStringLiteral("renamed"));
		REV_UNDO_OR_FAIL();
		QVERIFY(trackName(track) != QStringLiteral("renamed"));

		QVERIFY(run(QStringLiteral("track.set_mute"),
			{{QStringLiteral("track"), track}, {QStringLiteral("muted"), true}}).ok);
		QVERIFY(trackMuted(track));
		REV_UNDO_OR_FAIL();
		QVERIFY(!trackMuted(track));

		const ControlResult clip = run(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 192}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		const QString clipId = clip.result.value(QStringLiteral("clip")).toString();
		const ControlResult note = run(QStringLiteral("note.add"),
			{{QStringLiteral("clip"), clipId}, {QStringLiteral("key"), 60},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 24}});
		QVERIFY2(note.ok, qPrintable(note.errorMessage));
		QCOMPARE(rollNoteCount(clipId), 1);
		// The note's OWN id, from note.add's own result - not the "note-0"
		// literal this slot used before: the number is a project-wide
		// ProjectIds allocation shared by every id family (SPEC-stable-ids.md
		// R1), so it is not the note's index and not 0 in a binary that has
		// already built anything.
		const QString noteId = note.result.value(QStringLiteral("note")).toString();
		QVERIFY2(!noteId.isEmpty(), "note.add must report the added note's own id");

		QVERIFY(run(QStringLiteral("note.move"),
			{{QStringLiteral("clip"), clipId}, {QStringLiteral("note"), noteId},
				{QStringLiteral("position"), 48}}).ok);
		REV_UNDO_OR_FAIL();
		QCOMPARE(notePosition(clipId, 0), 0);

		QVERIFY(run(QStringLiteral("note.velocity_set"),
			{{QStringLiteral("clip"), clipId}, {QStringLiteral("note"), noteId},
				{QStringLiteral("velocity"), 42}}).ok);
		REV_UNDO_OR_FAIL();
		QCOMPARE(noteVelocity(clipId, 0), 100.0);

		QVERIFY(run(QStringLiteral("clip.move"),
			{{QStringLiteral("clip"), clipId}, {QStringLiteral("position"), 192}}).ok);
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipPosition(clipId), 0);

		QVERIFY(run(QStringLiteral("clip.resize"),
			{{QStringLiteral("clip"), clipId}, {QStringLiteral("length"), 384}}).ok);
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipLength(clipId), 192);

		// clip.set_mute (0.5.0 Q050-040): the mute BoolModel checkpoint, the same
		// class the track's mute rides - set, read back, undo, read back.
		QVERIFY(run(QStringLiteral("clip.set_mute"),
			{{QStringLiteral("clip"), clipId}, {QStringLiteral("muted"), true}}).ok);
		QVERIFY(clipMuted(clipId));
		QCOMPARE(stateOf(QStringLiteral("clip.set_mute")).value(QStringLiteral("class")).toString(),
			QStringLiteral("true_inverse"));
		REV_UNDO_OR_FAIL();
		QVERIFY(!clipMuted(clipId));
	}


	//! mixer.set_volume is a model checkpoint; mixer.add_channel is the created
	//! object whose inverse is the operation. Both are ONE step.
	void mixerWritesUndoToTheirPreCommandState()
	{
		const int channelsBefore = mixerChannelCount();
		const ControlResult added = run(QStringLiteral("mixer.add_channel"));
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString channel = added.result.value(QStringLiteral("channel")).toString();
		QCOMPARE(mixerChannelCount(), channelsBefore + 1);
		QCOMPARE(stateOf(QStringLiteral("mixer.add_channel"))
			.value(QStringLiteral("reversible")).toBool(), true);
		REV_UNDO_OR_FAIL();
		QCOMPARE(mixerChannelCount(), channelsBefore);

		// The fader: set, read back, undo, read back to the value it had.
		const QString survivor = channelId(0);
		const double before = channelVolume(survivor);
		const double wanted = before > 0.5 ? before - 0.25 : before + 0.25;
		QVERIFY(run(QStringLiteral("mixer.set_volume"),
			{{QStringLiteral("channel"), survivor}, {QStringLiteral("volume"), wanted}}).ok);
		QCOMPARE(channelVolume(survivor), wanted);
		REV_UNDO_OR_FAIL();
		QCOMPARE(channelVolume(survivor), before);
		Q_UNUSED(channel);
	}


	//! The FIRST automation.add_point creates the AutomationTrack as well as the
	//! point. It is still ONE undoable step: the recorded step removes the track
	//! it created.
	void firstAutomationPointIsOneUndoableStep()
	{
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		const QString parameter = firstAutomationParameter(track);
		QVERIFY2(!parameter.isEmpty(),
			"the loaded instrument exposes no automatable parameter to place a point on");

		const int tracksBefore = trackCount();
		const ControlResult added = run(QStringLiteral("automation.add_point"),
			{{QStringLiteral("track"), track}, {QStringLiteral("parameter"), parameter},
				{QStringLiteral("ticks"), 0}, {QStringLiteral("value"), 0.5}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("created_automation_track")).toBool(), true);
		QCOMPARE(trackCount(), tracksBefore + 1);
		QCOMPARE(stateOf(QStringLiteral("automation.add_point"))
			.value(QStringLiteral("reversible")).toBool(), true);

		REV_UNDO_OR_FAIL();
		QCOMPARE(trackCount(), tracksBefore);
	}


	//! An instrument replacement is snapshot-only. Any inverse it advertises
	//! must replay through the command registry's schema validation.
	void instrumentLoadTransactionOnlyAdvertisesAReplayableInverse()
	{
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		const QString device = firstLoadable(QStringLiteral("instrument"));
		if (device.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }

		const ControlResult replaced = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
		QVERIFY2(replaced.ok, qPrintable(replaced.errorMessage));
		const QJsonObject transaction = stateOf(QStringLiteral("plugin.load"));
		QVERIFY(!transaction.isEmpty());
		QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), false);
		const QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
		if (inverse.isEmpty()) { return; }

		const ControlResult replayed = run(inverse.value(QStringLiteral("op")).toString(),
			inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replayed.ok, qPrintable(replayed.errorMessage));
	}

	//! Luna cluster 5723: a state load that FAILS must leave no undo step.
	//!
	//! The handler called addUndoStep() with the captured previous settings
	//! BEFORE handing the bytes to controlRestoreDeviceState(), which is the
	//! call that can refuse them. A readable file whose content is not a device
	//! state document therefore failed the command and still left a step whose
	//! undo "restores" the device it never changed - so the caller's next
	//! control.undo consumed a step for an operation that did not happen, and
	//! the real step before it became unreachable from the keyboard.
	void aFailedStateLoadRecordsNoUndoStep()
	{
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		// The instrument-track device is addressed as 'inst'
		// (ControlDeviceState.cpp:46), not as an fx-<n> effect id.
		const QString pluginId = QStringLiteral("inst");

		// Readable, and NOT a state document for this device.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bogus = dir.path() + QStringLiteral("/not-a-state.xpf");
		QFile out(bogus);
		QVERIFY(out.open(QIODevice::WriteOnly));
		out.write("this is not an instrument-track preset document");
		out.close();

		const ControlResult depth = run(QStringLiteral("control.undo_depth"));
		QVERIFY(depth.ok);
		const int before = depth.result.value(QStringLiteral("depth")).toInt();

		const ControlResult failed = run(QStringLiteral("plugin.state_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("path"), bogus}});
		QVERIFY2(!failed.ok, "the device accepted a document that is not its state");

		const ControlResult after = run(QStringLiteral("control.undo_depth"));
		QVERIFY(after.ok);
		QCOMPARE(after.result.value(QStringLiteral("depth")).toInt(), before);
	}


	//! Luna cluster 2050: plugin.state_load advertised an inverse that could not be
	//! replayed. The command is an ACTION checkpoint - the table
	//! (ControlReversibilityTableAction.cpp) and its own `mechanism` both say the
	//! recorded undo step restores the captured settings XML - but the transaction
	//! named plugin.state_save and supplied only a `note`. That command requires
	//! target, plugin and path, and saving a device's CURRENT state would not
	//! restore the previous one anyway. A client that FOLLOWS the returned inverse
	//! - which is what `reversible: true` promises - could not undo the load.
	void theStateLoadInverseIsTheCheckpointItActuallyRecorded()
	{
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		const QString pluginId = QStringLiteral("inst");

		// The state load is only an INVERSE if replaying it restores what the
		// load REPLACED. Saving and loading the same document happens to leave
		// the device where it already was, so that shape passes even over an
		// inverse that restores the wrong state. Move the device first, then
		// load the saved document, then follow the inverse and read back the
		// moved state - the state the load destroyed.
		int index = -1;
		double before = 0.0;
		double wanted = 0.0;
		for (int candidate = 0; candidate < 256 && index < 0; ++candidate)
		{
			const ControlResult read = run(QStringLiteral("plugin.param_get"),
				{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
					{QStringLiteral("index"), candidate}});
			if (!read.ok) { break; }
			const QJsonObject parameter = read.result.value(QStringLiteral("parameter")).toObject();
			const double low = parameter.value(QStringLiteral("min")).toDouble();
			const double high = parameter.value(QStringLiteral("max")).toDouble();
			if (parameter.value(QStringLiteral("type")).toString() != QLatin1String("number")
				|| high <= low)
			{
				continue;
			}
			index = candidate;
			before = parameter.value(QStringLiteral("value")).toDouble();
			wanted = before > low ? low : high;  // a value the device is not at
		}
		QVERIFY2(index >= 0, "the loaded instrument exposes no numeric parameter to move");

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.path() + QStringLiteral("/state.xml");
		QVERIFY(run(QStringLiteral("plugin.state_save"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("path"), file}}).ok);

		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("index"), index}, {QStringLiteral("value"), wanted}}).ok);
		QVERIFY(qAbs(deviceParameterValue(track, pluginId, index) - wanted) < 1e-6);

		QVERIFY(run(QStringLiteral("plugin.state_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("path"), file}}).ok);
		// The load really replaced the moved state with the saved document.
		QVERIFY2(qAbs(deviceParameterValue(track, pluginId, index) - before) < 1e-6,
			"plugin.state_load did not apply the saved document, so the replay below "
			"would prove nothing");

		const QJsonObject recorded = stateOf(QStringLiteral("plugin.state_load"));
		QVERIFY(!recorded.isEmpty());
		QCOMPARE(recorded.value(QStringLiteral("reversible")).toBool(), true);
		const QJsonObject inverse = recorded.value(QStringLiteral("inverse")).toObject();
		const QString op = inverse.value(QStringLiteral("op")).toString();
		QVERIFY2(!op.isEmpty(), "plugin.state_load recorded an inverse with no op");
		QVERIFY2(ControlRegistry::instance()->command(op) != nullptr,
			qPrintable(QStringLiteral("the recorded inverse names '%1', which is not a "
				"command of the surface").arg(op)));

		// The promise `reversible: true` makes: a caller that follows the inverse
		// must be able to run it - AND the run must restore the state the load
		// replaced, not the state it installed.
		const ControlResult replay = run(op, inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replay.ok, qPrintable(QStringLiteral("plugin.state_load advertised the "
			"replayable inverse %1%2, which the registry refused: %3").arg(op,
				QString::fromUtf8(QJsonDocument(inverse).toJson(QJsonDocument::Compact)),
				replay.errorMessage)));
		QVERIFY2(qAbs(deviceParameterValue(track, pluginId, index) - wanted) < 1e-6,
			qPrintable(QStringLiteral("replaying plugin.state_load's inverse left parameter "
				"%1 at %2; the load replaced %3, so the inverse restored the wrong state")
				.arg(index).arg(deviceParameterValue(track, pluginId, index)).arg(wanted)));
	}


	//! The last two `true_inverse` rows that had no behavioural proof anywhere
	//! (cluster new-p8-205): `plugin.midi_part_set` and `rack.layer_set`. The
	//! contract calls both reversible through a recorded action checkpoint, but
	//! an undo acceptance suite that only names them in the table cannot tell a
	//! missing inverse from a working one. Each is applied, read back, undone
	//! once and read back again - the pre-command state, which is what A16
	//! promises and what the row's mechanism claims.
	void partAndLayerEditsUndoToTheirPreCommandState()
	{
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		const ControlResult added = run(QStringLiteral("plugin.midi_part_add"),
			{{QStringLiteral("target"), track}, {QStringLiteral("midi_channel"), 3},
				{QStringLiteral("low_key"), 40}, {QStringLiteral("high_key"), 80}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(added.result.value(QStringLiteral("part_count")).toInt(), 1);

		// plugin.midi_part_set: change the channel and the range, then undo.
		QVERIFY(run(QStringLiteral("plugin.midi_part_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("part"), QStringLiteral("part-0")},
				{QStringLiteral("midi_channel"), 7}, {QStringLiteral("low_key"), 12},
				{QStringLiteral("high_key"), 24}}).ok);
		QJsonObject part = run(QStringLiteral("plugin.midi_parts"),
			{{QStringLiteral("target"), track}}).result.value(QStringLiteral("parts"))
			.toArray().at(0).toObject();
		QCOMPARE(part.value(QStringLiteral("midi_channel")).toInt(), 7);
		QCOMPARE(part.value(QStringLiteral("low_key")).toInt(), 12);
		QCOMPARE(stateOf(QStringLiteral("plugin.midi_part_set"))
			.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(),
			QStringLiteral("plugin.midi_part_set"));
		REV_UNDO_OR_FAIL();
		part = run(QStringLiteral("plugin.midi_parts"),
			{{QStringLiteral("target"), track}}).result.value(QStringLiteral("parts"))
			.toArray().at(0).toObject();
		QCOMPARE(part.value(QStringLiteral("midi_channel")).toInt(), 3);
		QCOMPARE(part.value(QStringLiteral("low_key")).toInt(), 40);
		QCOMPARE(part.value(QStringLiteral("high_key")).toInt(), 80);

		// rack.layer_set: layer-0 is the track's own instrument, so its level is
		// the track's volume and only mute/solo are settable. Both are read back.
		const QJsonObject beforeLayer = run(QStringLiteral("rack.layers"),
			{{QStringLiteral("target"), track}}).result.value(QStringLiteral("layers"))
			.toArray().at(0).toObject();
		QVERIFY(!beforeLayer.value(QStringLiteral("muted")).toBool());
		QVERIFY(!beforeLayer.value(QStringLiteral("solo")).toBool());
		QVERIFY(run(QStringLiteral("rack.layer_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("layer"), QStringLiteral("layer-0")},
				{QStringLiteral("muted"), true}, {QStringLiteral("solo"), true}}).ok);
		QJsonObject layer = run(QStringLiteral("rack.layers"),
			{{QStringLiteral("target"), track}}).result.value(QStringLiteral("layers"))
			.toArray().at(0).toObject();
		QVERIFY(layer.value(QStringLiteral("muted")).toBool());
		QVERIFY(layer.value(QStringLiteral("solo")).toBool());
		QCOMPARE(stateOf(QStringLiteral("rack.layer_set"))
			.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString(),
			QStringLiteral("rack.layer_set"));
		REV_UNDO_OR_FAIL();
		layer = run(QStringLiteral("rack.layers"),
			{{QStringLiteral("target"), track}}).result.value(QStringLiteral("layers"))
			.toArray().at(0).toObject();
		QVERIFY(!layer.value(QStringLiteral("muted")).toBool());
		QVERIFY(!layer.value(QStringLiteral("solo")).toBool());
	}


	//! SPEC A16 deliverable 4: project.save keeps a recoverable previous
	//! revision, the retention policy is named, the set is bounded, and the
	//! restore path is reachable through the control surface.
	void projectSaveKeepsARecoverableRevision()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.path() + QStringLiteral("/revision-test.mmp");

		QVERIFY(run(QStringLiteral("project.save"),
			{{QStringLiteral("path"), file}}).ok);
		const QByteArray firstSave = readBytes(file);
		QVERIFY(!firstSave.isEmpty());

		// A second save over the same file: the FIRST bytes must be recoverable.
		const QString track = addTrack();
		QVERIFY(!track.isEmpty());
		const ControlResult saved = run(QStringLiteral("project.save"),
			{{QStringLiteral("path"), file}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("revision_kept")).toBool(), true);

		const QJsonObject revisions = saved.result.value(QStringLiteral("revisions")).toObject();
		QCOMPARE(revisions.value(QStringLiteral("policy")).toString(), QStringLiteral("keep-3"));
		QCOMPARE(revisions.value(QStringLiteral("count")).toInt(), 1);
		QCOMPARE(revisions.value(QStringLiteral("max_total_bytes")).toInt(), 3 * 8 * 1024 * 1024);
		QVERIFY(revisions.value(QStringLiteral("retained_bytes")).toInt() > 0);

		// The restore path is a command of the surface, and control.undo
		// dispatches exactly it for the save it recorded.
		REV_UNDO_OR_FAIL();
		QCOMPARE(readBytes(file), firstSave);

		// And it is a first-class command of the surface, exercised on its own
		// file so the assertion is about the COMMAND, not about the undo path.
		const QString direct = dir.path() + QStringLiteral("/direct.mmp");
		QVERIFY(run(QStringLiteral("project.save"),
			QJsonObject{{QStringLiteral("path"), direct}}).ok);
		const QByteArray directFirst = readBytes(direct);
		QVERIFY(!directFirst.isEmpty());
		QVERIFY(!addTrack().isEmpty());
		QVERIFY(run(QStringLiteral("project.save"),
			QJsonObject{{QStringLiteral("path"), direct}}).ok);
		QVERIFY(readBytes(direct) != directFirst);
		const ControlResult restored = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), direct}, {QStringLiteral("revision"), 0}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(readBytes(direct), directFirst);
		QCOMPARE(restored.result.value(QStringLiteral("restored")).toBool(), true);

		// Out-of-range revisions are refused rather than guessed.
		const ControlResult beyond = run(QStringLiteral("project.restore_revision"),
			QJsonObject{{QStringLiteral("path"), direct}, {QStringLiteral("revision"), 9}});
		QVERIFY(!beyond.ok);
		QCOMPARE(beyond.errorKind, ControlErrorKind::InvalidArgs);
	}


	//! A state load's REDO is covered in UndoHonestyTest
	//! (theStateLoadRedoReplaysTheBytesItLoaded, Luna cluster 5724): it needs the
	//! revtest:: fixture helpers and the file-length ratchet leaves no room here.

	/*! An instrument-track state restore accepted ANY parseable XML: the only
	 *  check was that the parsed content was non-null, then loadPreset was
	 *  called and `restored: true` reported unconditionally - a document the
	 *  loader ignores was still reported as applied. track.preset_load (the
	 *  file-based sibling, ControlCommandsTrackPresets.cpp:85-90) already
	 *  refuses a content element whose `type` is missing or is another kind of
	 *  track; plugin.state_load must refuse it too.
	 */
	void anInstrumentStateRestoreRefusesAForeignDocument()
	{
		const QString track = addTrack();
		QVERIFY2(!track.isEmpty(), "this build exposes no instrument track to restore into");
		ControlTarget target; ControlResult error;
		QVERIFY2(resolveControlTarget(track, &target, &error), qPrintable(error.errorMessage));
		QVERIFY(target.instrumentTrack != nullptr);
		ControlDeviceHandle handle;
		handle.track = target.instrumentTrack;
		handle.pluginName = QStringLiteral("bundle-test");

		// The happy path is untouched: the engine's own captured state restores.
		const QByteArray own = controlDeviceStateBytes(handle);
		QVERIFY2(!own.isEmpty(), "the instrument track captured no state to restore");
		QVERIFY2(controlRestoreDeviceState(handle, own).ok,
			"the instrument's own captured state was refused");

		// The defect: a document that parses and DOES carry the content element
		// DataFile selects, but is not an instrument-track preset - a saved
		// project, whose root type names a `song` content element. loadPreset was
		// handed that element and `restored: true` was reported regardless of the
		// fact that it applied nothing.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = dir.path() + QStringLiteral("/foreign.mmp");
		QVERIFY2(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok,
			"this build could not save a project document to stand in as the foreign state");
		ControlResult ignored;
		QByteArray foreign;
		QVERIFY(controlReadFileBytes(project, &foreign, &ignored));
		QVERIFY2(!foreign.isEmpty(), "the saved project document is empty");

		const ControlResult restored = controlRestoreDeviceState(handle, foreign);
		QVERIFY2(!restored.ok,
			"controlRestoreDeviceState reported SUCCESS over a saved PROJECT document "
			"(BUG): the content element exists, so the null check passed, loadPreset was "
			"handed a <song> element it ignores, and restored:true was returned");
		QVERIFY2(!restored.result.value(QStringLiteral("restored")).toBool(),
			"the refusal still carried restored:true");
	}

	//! A failed meter.arm action restore must be reported by control.undo rather
	//! than claimed as undone while the live tap remains armed.
	void meterArmUndoReportsQuietWaitFailure()
	{
		Engine::audioEngine()->audioDev()->stopProcessing();
		MasterLoudnessTap* tap = Engine::audioEngine()->masterLoudness();
		QVERIFY(tap != nullptr);
		QVERIFY(tap->setEnabled(false));

		const ControlResult armed = run(QStringLiteral("meter.arm"),
			{{QStringLiteral("enabled"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(tap->enabled());

		setMasterLoudnessTapNextQuietWaitResultForTesting(false);
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(!undone.ok, "control.undo claimed the meter.arm action was undone after the tap timed out");
		QCOMPARE(undone.errorKind, ControlErrorKind::Refused);
		QVERIFY(tap->enabled());

		setMasterLoudnessTapNextQuietWaitResultForTesting(false);
		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY2(!redone.ok, "control.redo claimed the meter.arm action was reapplied after the tap timed out");
		QCOMPARE(redone.errorKind, ControlErrorKind::Refused);
		QVERIFY(tap->enabled());
	}
};

QTEST_GUILESS_MAIN(ReversibilityUndoTest)
#include "ReversibilityUndoTest.moc"
