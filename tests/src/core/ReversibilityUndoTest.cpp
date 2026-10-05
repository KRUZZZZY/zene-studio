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

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "ProjectJournal.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

/*! A real Effect, built here rather than LOADED from the device catalogue.
 *
 *  plugin.bypass addresses an Effect by its `fx-<n>` id and calls
 *  setEnabled()/isEnabled() on it (ControlCommandsPlugin.cpp's
 *  registerPluginBypass), so the catalogue is not involved - only the chain and
 *  the id. This build exposes no loadable built-in effect (the device modules
 *  are found through LMMS_PLUGIN_DIR, which these tests must not export), so
 *  firstLoadableEffect() is empty here and every slot built on it SKIPs. The
 *  enabled flag is the whole subject of the case below, and this effect owns one
 *  exactly as a loaded device does.
 */
class RevtestBypassEffect : public Effect
{
public:
	RevtestBypassEffect(Model* parent) : Effect{&s_descriptor, parent, nullptr} {}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		(void)buf;
		(void)frames;
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor RevtestBypassEffect::s_descriptor
{
	"revtestbypasseffect",
	"ReversibilityUndoTest bypass flag effect",
	"A pass-through effect whose only state is the enabled flag plugin.bypass drives",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

} // namespace

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


	//! The device commands: bypass and param_set are model checkpoints, and a
	//! state load is an action step that restores the captured XML.
	void deviceCommandsUndoToTheirPreCommandState()
	{
		const QString device = firstLoadableEffect();
		if (device.isEmpty()) { QSKIP("this build exposes no loadable built-in effect"); }
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }

		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(loaded.result.value(QStringLiteral("kind")).toString(), QStringLiteral("effect"));
		const QString fx = loaded.result.value(QStringLiteral("id")).toString();
		const int devicesAfterLoad = deviceCount(track);

		QVERIFY(run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), true}}).ok);
		QVERIFY(!deviceEnabled(track, fx));
		REV_UNDO_OR_FAIL();
		QVERIFY(deviceEnabled(track, fx));

		// The param write is only meaningful when the device exposes one.
		const QJsonArray parameters = deviceParameters(track, fx);
		if (parameters.isEmpty()) { QSKIP("the loaded effect exposes no parameter"); }
		const QJsonObject parameter = parameters.first().toObject();
		const double previous = parameter.value(QStringLiteral("value")).toDouble();
		const double wanted = parameter.value(QStringLiteral("min")).toDouble() != previous
			? parameter.value(QStringLiteral("min")).toDouble()
			: parameter.value(QStringLiteral("max")).toDouble();
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("index"), parameter.value(QStringLiteral("index")).toInt()},
				{QStringLiteral("value"), wanted}}).ok);
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), wanted);
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), previous);

		// The append itself is one step: undo unloads the device it created.
		QCOMPARE(deviceCount(track), devicesAfterLoad);
		const QJsonObject loadTx = stateOf(QStringLiteral("plugin.load"));
		QCOMPARE(loadTx.value(QStringLiteral("reversible")).toBool(), true);

		// ... and the state load restores the settings it replaced.
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.path() + QStringLiteral("/state.xml");
		QVERIFY(run(QStringLiteral("plugin.state_save"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("path"), file}}).ok);
		QVERIFY(QFileInfo::exists(file));
		QVERIFY(run(QStringLiteral("plugin.param_set"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("index"), parameter.value(QStringLiteral("index")).toInt()},
				{QStringLiteral("value"), wanted}}).ok);
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), wanted);
		QVERIFY(run(QStringLiteral("plugin.state_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("path"), file}}).ok);
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), previous);
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceParameterValue(track, fx, parameter.value(QStringLiteral("index")).toInt()), wanted);
	}


	/*! Luna cluster 4865: plugin.bypass's recorded INVERSE must be replayable, and
	 *  replaying it must restore the state the command found.
	 *
	 *  The two flags have OPPOSITE meanings and the inverse had them the wrong way
	 *  round: the handler applies `setEnabled(!bypass)`, but the inverse stored
	 *  `previouslyEnabled` AS the `bypass` argument. So an effect that was enabled
	 *  and got bypassed recorded an inverse that bypasses it again, and one that
	 *  was disabled recorded an inverse that leaves it enabled - the inverse
	 *  re-applied the change instead of undoing it. control.undo never noticed,
	 *  because this command's record is journal-class (an Effect enabled-model
	 *  checkpoint) and undoes through the ProjectJournal, not through these
	 *  args: the payload was only ever wrong for a caller that FOLLOWS it, which
	 *  is exactly what `__transaction.inverse` promises a caller it can.
	 */
	void theBypassInverseReplaysToTheStateBeforeTheCommand()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		QVERIFY2(!track.isEmpty(), "track.add refused: this case cannot be measured");
		ControlTarget target;
		ControlResult error;
		QVERIFY2(resolveControlTarget(track, &target, &error),
			qPrintable(error.errorMessage));
		Effect* effect = new RevtestBypassEffect(target.chain);
		target.chain->appendEffect(effect);
		const QString fx = control::effectId(effect->id());

		// (a) An ENABLED effect, bypassed: the inverse must re-enable it.
		QVERIFY(effect->isEnabled());
		const ControlResult bypassed = run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), true}});
		QVERIFY2(bypassed.ok, qPrintable(bypassed.errorMessage));
		QVERIFY(!effect->isEnabled());

		const QJsonObject inverse = stateOf(QStringLiteral("plugin.bypass"))
			.value(QStringLiteral("inverse")).toObject();
		QCOMPARE(inverse.value(QStringLiteral("op")).toString(), QStringLiteral("plugin.bypass"));
		const QJsonObject inverseArgs = inverse.value(QStringLiteral("args")).toObject();
		QVERIFY2(!inverseArgs.isEmpty(), "plugin.bypass recorded an inverse with no args");
		QVERIFY2(inverseArgs.value(QStringLiteral("bypass")).toBool() == false,
			"the inverse of bypassing an enabled effect asks to bypass it AGAIN "
			"(bypass=true re-applies the change instead of undoing it)");
		QVERIFY2(run(inverse.value(QStringLiteral("op")).toString(), inverseArgs).ok,
			"the recorded inverse is not replayable");
		QVERIFY2(effect->isEnabled(),
			"replaying the recorded inverse did not put the effect back in the state "
			"plugin.bypass found it in");

		// (b) The other direction: un-bypassing a DISABLED effect records an inverse
		// that bypasses it again, and the replay puts it back to disabled. The
		// effect is disabled first on purpose - (a) left it enabled, and running
		// bypass=false from there is a no-op that records nothing about the disabled
		// state this half is about.
		QVERIFY2(run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), true}}).ok, "probe (b)");
		QVERIFY(!effect->isEnabled());
		QVERIFY(run(QStringLiteral("plugin.bypass"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx},
				{QStringLiteral("bypass"), false}}).ok);
		QVERIFY(effect->isEnabled());
		const QJsonObject undoArgs = stateOf(QStringLiteral("plugin.bypass"))
			.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("args")).toObject();
		QVERIFY2(undoArgs.value(QStringLiteral("bypass")).toBool() == true,
			"the inverse of un-bypassing a disabled effect asks to un-bypass it AGAIN "
			"(bypass=false leaves the effect enabled instead of restoring it disabled)");
		QVERIFY(run(QStringLiteral("plugin.bypass"), undoArgs).ok);
		QVERIFY2(!effect->isEnabled(),
			"replaying the second inverse did not restore the disabled state");

		target.chain->removeEffect(effect);
		effect->deleteLater();
	}


	/*! BUG-CTL-1 (SPEC A16): the undo of plugin.unload must NOT report success when
	 *  the recreated device could not be given its captured settings. Before the fix
	 *  recreateEffectFromState() discarded controlRestoreEffectState()'s result, so
	 *  the undo answered `undone: true` over a device sitting at its DEFAULTS.
	 *
	 *  The refusal is driven through the PRODUCTION recreate path with a document whose
	 *  body element is removed: one controlEffectStateXml() captured from a live device
	 *  is always restorable by that same engine, so plugin.unload cannot reach it. The
	 *  step is pushed with the API journalEffectRemoval() uses.
	 */
	void unloadUndoRefusesWhenTheDeviceCannotBeRestored()
	{
		const QString device = firstLoadableEffect();
		if (device.isEmpty()) { QSKIP("this build exposes no loadable built-in effect"); }
		const QString track = addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		ControlTarget target; ControlResult error;
		QVERIFY2(resolveControlTarget(track, &target, &error), qPrintable(error.errorMessage));

		const ControlResult loaded = run(QStringLiteral("plugin.load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		const QString fx = loaded.result.value(QStringLiteral("id")).toString();

		// The happy path is untouched: the undo reports success and the device is back.
		QVERIFY(run(QStringLiteral("plugin.unload"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), fx}}).ok);
		QCOMPARE(deviceCount(track), 0);
		REV_UNDO_OR_FAIL();
		QCOMPARE(deviceCount(track), 1);

		// The defect: a document the DEVICE refuses, naming a device this build can build.
		const QString restored = deviceIdAt(track, 0);
		Effect* live = resolveControlEffect(target, restored, &error);
		QVERIFY2(live != nullptr, qPrintable(error.errorMessage));
		QDomDocument document;
		QVERIFY(document.setContent(controlEffectStateXml(live)));
		const QDomElement body = document.documentElement().firstChildElement(live->nodeName());
		QVERIFY2(!body.isNull(), "the captured device document carries no body to remove");
		document.documentElement().removeChild(body);
		const QString refused = document.toString();

		// plugin.unload again, the REAL command (so the transaction control.undo consults
		// is the one it recorded), then the step it recorded with the refused document.
		QVERIFY(run(QStringLiteral("plugin.unload"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), restored}}).ok);
		QCOMPARE(deviceCount(track), 0);
		auto holder = std::make_shared<Effect*>(nullptr);
		control::addStructuralUndoStep(
			[chain = target.chain, refused, holder]() {
				*holder = control::recreateEffectFromState(chain, refused, 0);
			},
			[chain = target.chain, holder]() {
				if (*holder != nullptr) { chain->removeEffect(*holder); (*holder)->deleteLater(); *holder = nullptr; }
			},
			refused.size());

		// (1) the undo must NOT report success - `undone: true` here was the lie ...
		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(!undo.ok, "control.undo reported SUCCESS although the device was not restored "
			"(BUG-CTL-1): the recreated instance sat at its defaults");
		QVERIFY(undo.errorKind == ControlErrorKind::InvalidArgs
			|| undo.errorKind == ControlErrorKind::Refused);
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("NOT restored")), qPrintable(undo.errorMessage));
		QCOMPARE(deviceCount(track), 0);  // (2) no half-restored device left behind
		// (3) ... and repeated undo/redo stays honest. No depth delta is asserted:
		// undo_depth also counts the chain's own enabled-flag checkpoints (appendEffect/
		// removeEffect), a pre-existing EffectChain behaviour the happy path has too.
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(deviceCount(track), 0);
		QVERIFY2(!run(QStringLiteral("control.undo")).ok, "a second undo of the failed restore "
			"reported SUCCESS (BUG-CTL-1)");
		QCOMPARE(deviceCount(track), 0);
		QVERIFY(run(QStringLiteral("track.list")).ok);
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

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.path() + QStringLiteral("/state.xml");
		QVERIFY(run(QStringLiteral("plugin.state_save"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("path"), file}}).ok);
		QVERIFY(run(QStringLiteral("plugin.state_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("path"), file}}).ok);

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
		// must be able to run it. It refused, for want of the arguments the
		// advertised command requires.
		const ControlResult replay = run(op, inverse.value(QStringLiteral("args")).toObject());
		QVERIFY2(replay.ok, qPrintable(QStringLiteral("plugin.state_load advertised the "
			"replayable inverse %1%2, which the registry refused: %3").arg(op,
				QString::fromUtf8(QJsonDocument(inverse).toJson(QJsonDocument::Compact)),
				replay.errorMessage)));
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
};

QTEST_GUILESS_MAIN(ReversibilityUndoTest)
#include "ReversibilityUndoTest.moc"
