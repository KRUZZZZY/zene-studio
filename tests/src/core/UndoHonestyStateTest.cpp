/*
 * UndoHonestyStateTest.cpp - W1 (0.5.0 catch-up): the state and preset redo replays the bytes it loaded, key names, and the inverse sweep
 *                                 (split out of UndoHonestyTest to keep both under the file-length ratchet)
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
/*! W1, moved unchanged from UndoHonestyTest.cpp: a state load's and a preset load's redo replay the bytes they loaded
 *  (Luna 5724, 5722); a key-name change records the names it restores (Luna 1096); a key-name set without a name changes
 *  nothing (Luna 4836); no record names an UNIMPLEMENTED inverse (E19). */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QStringList>
#include <QTemporaryDir>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlDeviceMoves.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "ControllerConnection.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LfoController.h"
#include "MidiClip.h"
#include "MidiController.h"
#include "Mixer.h"
#include "Note.h"
#include "ProjectJournal.h"
#include "Rack.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "TrackKeyNames.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}
//! Points ConfigManager's working directory at a private folder for one scope. Preset files are
//! written under the working directory, so a case that saves presets must not touch the user's
//! real preset directory; the previous working directory is restored when the scope ends.
class PrivateWorkingDir
{
public:
	PrivateWorkingDir() : m_previous(ConfigManager::inst()->workingDir())
	{
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("work")));
	}
	~PrivateWorkingDir() { ConfigManager::inst()->setWorkingDir(m_previous); }
	PrivateWorkingDir(const PrivateWorkingDir&) = delete;
	PrivateWorkingDir& operator=(const PrivateWorkingDir&) = delete;

private:
	QTemporaryDir m_dir;
	QString m_previous;
};

bool undone() { return run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool(); }
} // namespace

class UndoHonestyStateTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		while (Engine::mixer()->numChannels() <= 4) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Luna cluster 5724: a state load's REDO must replay the BYTES the load
	//! applied, not whatever the path happens to hold when the redo runs.
	//!
	//! The redo closure captured `path` and re-read the file
	//! (src/core/ControlCommandsPluginState.cpp:190). Replacing that file with a
	//! DIFFERENT valid document of the same device therefore made the redo apply
	//! the new state: a redo that reads the file lands on 'decoy', a redo that
	//! replays what it loaded lands on 'alpha'.
	//!
	//! The track NAME is the observable: Track::saveTrack writes it into the
	//! preset document (src/core/Track.cpp:454) and loadPreset reads it back
	//! (src/core/Track.cpp:609), and track.rename / track.get_state are commands
	//! of the surface.
	void theStateLoadRedoReplaysTheBytesItLoaded()
	{
		const QString track = revtest::addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		const QString pluginId = QStringLiteral("inst");
		const auto rename = [track](const QString& name) {
			return revtest::run(QStringLiteral("track.rename"),
				{{QStringLiteral("track"), track}, {QStringLiteral("name"), name}}).ok;
		};
		const auto save = [track, pluginId](const QString& path) {
			return revtest::run(QStringLiteral("plugin.state_save"),
				{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
					{QStringLiteral("path"), path}}).ok;
		};

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString loaded = dir.path() + QStringLiteral("/loaded.xml");
		const QString decoy = dir.path() + QStringLiteral("/decoy.xml");

		QVERIFY(rename(QStringLiteral("alpha")));
		QVERIFY(save(loaded));
		QVERIFY(rename(QStringLiteral("decoy")));
		QVERIFY(save(decoy));
		QVERIFY(rename(QStringLiteral("gamma")));

		QVERIFY(revtest::run(QStringLiteral("plugin.state_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("path"), loaded}}).ok);
		QCOMPARE(revtest::trackName(track), QStringLiteral("alpha"));

		REV_UNDO_OR_FAIL();
		QCOMPARE(revtest::trackName(track), QStringLiteral("gamma"));

		// The file the redo would RE-READ now holds another valid state of this
		// very device. Written straight to disk on purpose: a command here would
		// take an undo step of its own and discard the redo being tested.
		{
			QFile out(loaded);
			QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
			out.write(revtest::readBytes(decoy));
		}

		QVERIFY(revtest::run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool());
		QCOMPARE(revtest::trackName(track), QStringLiteral("alpha"));
	}

	//! Luna cluster 5722: a preset load's REDO must replay the BYTES it loaded,
	//! not whatever the preset file holds when the redo runs.
	//!
	//! The redo closure captured `path` and re-read it
	//! (src/core/ControlCommandsPluginPreset.cpp:316-323). Overwriting that
	//! preset between the undo and the redo therefore made the redo apply the
	//! replacement: a redo that reads the file lands on 'decoy', a redo that
	//! replays what it loaded lands on 'alpha'.
	void thePresetLoadRedoReplaysTheBytesItLoaded()
	{
		PrivateWorkingDir isolated;  // the presets below go to a private directory, never the user's
		const QString track = revtest::addInstrumentTrack();
		if (track.isEmpty()) { QSKIP("this build exposes no loadable built-in instrument"); }
		const QString pluginId = QStringLiteral("inst");
		const auto rename = [track](const QString& name) {
			return revtest::run(QStringLiteral("track.rename"),
				{{QStringLiteral("track"), track}, {QStringLiteral("name"), name}}).ok;
		};
		const auto save = [track, pluginId](const QString& name) {
			// The presets live in the PRIVATE working directory set up above (plugin.preset_save
			// writes under the working directory). Replacing keeps a re-run simple.
			return revtest::run(QStringLiteral("plugin.preset_save"),
				{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
					{QStringLiteral("name"), name}, {QStringLiteral("overwrite"), true}});
		};

		QVERIFY(rename(QStringLiteral("alpha")));
		const ControlResult alpha = save(QStringLiteral("alpha"));
		QVERIFY2(alpha.ok, qPrintable(alpha.errorMessage));
		QVERIFY(rename(QStringLiteral("decoy")));
		const ControlResult decoy = save(QStringLiteral("decoy"));
		QVERIFY2(decoy.ok, qPrintable(decoy.errorMessage));
		QVERIFY(rename(QStringLiteral("gamma")));

		QVERIFY(revtest::run(QStringLiteral("plugin.preset_load"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), pluginId},
				{QStringLiteral("name"), QStringLiteral("alpha")}}).ok);
		QCOMPARE(revtest::trackName(track), QStringLiteral("alpha"));

		REV_UNDO_OR_FAIL();
		QCOMPARE(revtest::trackName(track), QStringLiteral("gamma"));

		// The preset the redo would RE-READ now holds another valid state of this
		// very device. Written straight to disk on purpose: a command here would
		// take an undo step of its own and discard the redo being tested.
		{
			QFile out(alpha.result.value(QStringLiteral("path")).toString());
			QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
			out.write(revtest::readBytes(decoy.result.value(QStringLiteral("path")).toString()));
		}

		QVERIFY(revtest::run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool());
		QCOMPARE(revtest::trackName(track), QStringLiteral("alpha"));

		QFile::remove(alpha.result.value(QStringLiteral("path")).toString());
		QFile::remove(decoy.result.value(QStringLiteral("path")).toString());
	}

	//! Luna cluster 1096: the undo closure carried the old names, but the transaction's
	//! 'before' payload was empty - so the record that claims to restore them described no
	//! state at all, and a client reading control.transactions saw a reversible step with
	//! nothing to reverse to.
	void theKeyNameTransactionCarriesTheNamesItRestores()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);
		// The first label on a track has no before-state - the track had no names to replace.
		QVERIFY(run(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QStringLiteral("Kick")}}).ok);
		const ControlRegistry::Transaction* first = ControlRegistry::instance()->lastTransaction();
		QVERIFY2(first != nullptr, "setting a key name recorded no transaction");
		QVERIFY2(first->reversible, "the key-name change recorded no inverse");

		// Replacing it is the case that matters: the step restores "Kick", so the record
		// has to say so - an empty before-state describes nothing to return to.
		QVERIFY(run(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QStringLiteral("BD")}}).ok);
		const ControlRegistry::Transaction* second = ControlRegistry::instance()->lastTransaction();
		QVERIFY(second != nullptr);
		const QJsonArray carried = second->before.value(QStringLiteral("names")).toArray();
		QCOMPARE(carried.size(), 1);
		QCOMPARE(carried.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Kick"));
		// And the undo step really does put that name back, as the record claims.
		QCOMPARE(TrackKeyNames::instance().name(track->id(), 24), QStringLiteral("BD"));
		QVERIFY(undone());
		QCOMPARE(TrackKeyNames::instance().name(track->id(), 24), QStringLiteral("Kick"));
	}

	//! Luna cluster 4836: 'name' is not a required parameter, so a call that left it out
	//! turned into an empty string - which the handler reads as "remove this key". A caller
	//! that misspelled or forgot the field silently deleted the label.
	void aKeyNameSetWithoutANameChangesNothing()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);
		QVERIFY(run(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QStringLiteral("Kick")}}).ok);
		// No 'name' at all: refused, and the label it would have deleted is still there.
		const ControlResult missing = run(QStringLiteral("key_names.set"),
			{{QStringLiteral("track"), id}, {QStringLiteral("key"), 24}});
		QCOMPARE(missing.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(TrackKeyNames::instance().name(track->id(), 24), QStringLiteral("Kick"));
		// An explicit empty name is still how a label is removed - that stays a removal.
		QVERIFY(run(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QString()}}).ok);
		QVERIFY(TrackKeyNames::instance().name(track->id(), 24).isEmpty());
	}

	//! new-p14-19 BUG 13: a key-name call whose map is already in the requested state is
	//! not an edit. storeNames recorded a journal step (and marked the project modified)
	//! unconditionally, so reapplying the current name, removing a key that had none, or
	//! applying the empty 'none' preset to an empty track put a no-op on the undo stack:
	//! the next undo consumed it and left the real edit in place.
	void aKeyNameEditThatChangesNothingRecordsNoStep()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(registry != nullptr);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int coalesceWindow = registry->coalesceWindowMs();
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);

		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(track != nullptr);
		const QString id = control::trackIdOf(track);

		// A no-op leaves the journal and the transaction log exactly as it found them.
		const auto expectNoStep = [&](const QString& command, const QJsonObject& args) {
			const quint64 stepBefore = journal->topStepSerial();
			const int countBefore = registry->transactions().size();
			const ControlResult result = run(command, args);
			QVERIFY2(result.ok, qPrintable(result.errorMessage));
			QCOMPARE(journal->topStepSerial(), stepBefore);
			QCOMPARE(registry->transactions().size(), countBefore);
		};

		// The empty preset on a track that has no names removes nothing.
		expectNoStep(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), id},
			{QStringLiteral("preset"), QStringLiteral("none")}});
		// Removing a key that has no name removes nothing.
		expectNoStep(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QString()}});

		// Reapplying the current name: the map already holds exactly that.
		QVERIFY(run(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QStringLiteral("Kick")}}).ok);
		QCOMPARE(TrackKeyNames::instance().name(track->id(), 24), QStringLiteral("Kick"));
		expectNoStep(QStringLiteral("key_names.set"), {{QStringLiteral("track"), id}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QStringLiteral("Kick")}});

		// The one real edit is still the step the next undo takes: the no-op did not sit
		// on top of it and get consumed in its place.
		QVERIFY(undone());
		QVERIFY(TrackKeyNames::instance().name(track->id(), 24).isEmpty());

		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), coalesceWindow}}).ok);
	}

	//! Luna cluster new-p14-81: a removal whose inverse was NOT recorded must not
	//! be reported as reversible.
	//!
	//! journalTrackRemoval() answered "true" as soon as it had CAPTURED the
	//! track's XML - whether or not the structural step was actually pushed. A
	//! journal that is not journalling (or no journal at all) records nothing:
	//! ProjectJournal::addJournalStructure() refuses the step. The helper then
	//! claimed success, and track.remove - which takes that claim for its
	//! transaction's `reversible` - answered "reversible: true" over a deletion
	//! no undo could reverse. The next undo would then take back an OLDER edit.
	//!
	//! The seam is the engine's own journalling flag: the same switch the
	//! product's JournalPause uses to keep an undo from recording a checkpoint
	//! of its own. No clock, no sleep, no race.
	void aRemovalThatRecordedNoStepIsNotReportedAsReversible()
	{
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QVERIFY(journal->isJournalling());

		// (0) The helper's OWN answer, before any command is involved: a refused
		// step is not an inverse, and the helper must say so.
		const QString direct = revtest::addTrack();
		QVERIFY(!direct.isEmpty());
		Track* directTrack = Engine::getSong()->tracks().back();
		QVERIFY(directTrack != nullptr);
		journal->setJournalling(false);
		const bool directRecorded = control::journalTrackRemoval(directTrack);
		journal->setJournalling(true);
		QVERIFY2(!directRecorded,
			"journalTrackRemoval claimed an inverse although no structural step was recorded (new-p14-81)");

		// (1) The journal records nothing: the removal is real, but it has no inverse.
		journal->setJournalling(false);
		const ControlResult removed = run(QStringLiteral("track.remove"), {{QStringLiteral("track"), direct}});
		journal->setJournalling(true);
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QVERIFY(!revtest::hasTrack(direct));
		const QJsonObject record = revtest::stateOf(QStringLiteral("track.remove"));
		QVERIFY2(!record.isEmpty(), "the removal recorded no transaction at all");
		QVERIFY2(!record.value(QStringLiteral("reversible")).toBool(),
			"the removal was reported as reversible although no undo step was recorded "
			"(new-p14-81): the next undo would take back an OLDER edit");

		// (2) The very same removal, with the journal recording again, is reversible.
		const QString other = revtest::addTrack();
		QVERIFY(!other.isEmpty());
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), other}}).ok);
		const QJsonObject recorded = revtest::stateOf(QStringLiteral("track.remove"));
		QCOMPARE(recorded.value(QStringLiteral("reversible")).toBool(), true);
	}

	/*! A SIBLING structural helper must pass a refused step on instead of claiming it
	 *  was recorded: journalChainRemoval() (rack.remove_chain) is the same writer as
	 *  journalTrackRemoval(), and a chain removal the journal refused to record has
	 *  no inverse either (new-p14-81).
	 *
	 *  Two things are proved here, and both must fail without the helper's fix:
	 *  the helper's own answer (it returns false when the step was refused), and
	 *  the RECORD the command leaves behind (it must not claim a recorded undo
	 *  step - a record that says otherwise is the lie the finding is about).
	 */
	void aRackRemovalThatRecordedNoStepIsNotReportedAsReversible()
	{
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		QVERIFY(journal->isJournalling());
		journal->clearJournal();

		MixerChannel* channel = Engine::mixer()->mixerChannel(4);
		const QString channelId = control::channelIdOf(channel);
		const ControlResult added = run(QStringLiteral("rack.add_chain"),
			{{QStringLiteral("channel"), channelId}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const int chain = added.result.value(QStringLiteral("chain")).toInt();
		QVERIFY(chain >= 1);
		const int selectedBefore = channel->m_rack.selectedChain();

		// (1) The helper's own answer: the paused journal refuses its structural
		// step, so there is no inverse to report.
		journal->setJournalling(false);
		const bool recorded = control::journalChainRemoval(channelId, chain, {}, selectedBefore);
		journal->setJournalling(true);
		QVERIFY2(!recorded,
			"journalChainRemoval claimed an inverse although no structural step was "
			"recorded (new-p14-81)");

		// (2) The record the command leaves: the removal is real, but nothing was
		// recorded for it, so the record must not claim a recorded undo step.
		journal->setJournalling(false);
		const ControlResult removed = run(QStringLiteral("rack.remove_chain"),
			{{QStringLiteral("channel"), channelId}, {QStringLiteral("chain"), chain}});
		journal->setJournalling(true);
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(channel->m_rack.chainCount(), chain);
		const QJsonObject record = revtest::stateOf(QStringLiteral("rack.remove_chain"));
		QVERIFY2(!record.isEmpty(), "rack.remove_chain recorded no transaction at all");
		QVERIFY2(!record.value(QStringLiteral("reversible")).toBool(),
			"rack.remove_chain claimed an inverse although no structural step was recorded "
			"(new-p14-81)");
		const QString mechanism = record.value(QStringLiteral("mechanism")).toString();
		QVERIFY2(!mechanism.contains(QStringLiteral("the recorded undo step")),
			qPrintable(QStringLiteral("rack.remove_chain's record claimed a recorded undo step "
				"although the journal recorded none (new-p14-81): ") + mechanism));

		// Drop the add-chain step: its chain was deliberately removed without an inverse.
		journal->clearJournal();
	}

	/*! An explicit JOURNAL inverse is not reversible when its command left no step.
	 *
	 *  The registry used to take the handler's claim at its word: a command that
	 *  declared a journal inverse and recorded nothing was still reported as
	 *  reversible, so the record said "this can be undone" over an edit no undo
	 *  could reach (new-p14-81). A PAIRED COMMAND inverse and a DEFERRED journal
	 *  step need no step of their own and keep their claim.
	 */
	void aJournalInverseWithoutARecordedStepIsNotReportedAsReversible()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(registry != nullptr);
		ControlCommand cmd;
		cmd.id = QStringLiteral("test.claims_unrecorded_journal_inverse");
		cmd.group = QStringLiteral("test");
		cmd.verb = QStringLiteral("claims_unrecorded_journal_inverse");
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) {
			return ControlResult::success({{QStringLiteral("__transaction"), control::transactionPayload({},
				QStringLiteral("control.undo"), {}, true, QStringLiteral("test journal inverse"))}});
		};
		registry->registerCommand(cmd);

		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->setJournalling(false);
		const ControlResult invoked = registry->invoke(cmd.id, {});
		journal->setJournalling(true);
		QVERIFY2(invoked.ok, qPrintable(invoked.errorMessage));
		const ControlRegistry::Transaction* record = registry->lastTransaction();
		QVERIFY(record != nullptr);
		QCOMPARE(record->command, cmd.id);
		QCOMPARE(record->step, quint64(0));
		QVERIFY2(!record->reversible,
			"the registry reported a journal inverse as reversible although this call "
			"recorded no journal step (new-p14-81)");
	}

	//! An unrecorded journal step above a command is undone without borrowing that command's name.
	void anUnrecordedTopStepIsReportedWithoutRenamingTheCommandRecord()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(registry != nullptr);
		const int coalesceWindow = registry->coalesceWindowMs();
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
		FloatModel& volume = Engine::mixer()->mixerChannel(1)->m_volumeModel;
		const float before = volume.value();
		QVERIFY(run(QStringLiteral("mixer.set_volume"), {{QStringLiteral("channel"), control::channelIdOf(Engine::mixer()->mixerChannel(1))},
			{QStringLiteral("volume"), 0.25}}).ok);
		const ControlRegistry::Transaction* record = registry->lastTransaction();
		QVERIFY(record != nullptr);
		const QString command = record->command;
		const quint64 commandStep = record->step;
		QCOMPARE(command, QStringLiteral("mixer.set_volume"));
		QCOMPARE(Engine::projectJournal()->topStepSerial(), commandStep);
		const int transactionCount = registry->transactions().size();

		volume.setValue(0.75f);
		control::addUndoStep([&volume] { volume.setValue(0.25f); }, [&volume] { volume.setValue(0.75f); });
		const quint64 unrecordedStep = Engine::projectJournal()->topStepSerial();
		QVERIFY(unrecordedStep != commandStep);

		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QVERIFY(undo.result.value(QStringLiteral("undone")).toBool());
		QCOMPARE(undo.result.value(QStringLiteral("undone_command")).toString(), QStringLiteral("unrecorded edit"));
		QVERIFY(undo.result.value(QStringLiteral("unrecorded_edit")).toBool());
		QCOMPARE(static_cast<quint64>(undo.result.value(QStringLiteral("step_serial")).toDouble()), unrecordedStep);
		QCOMPARE(registry->transactions().size(), transactionCount);
		QCOMPARE(registry->lastTransaction()->command, command);
		QCOMPARE(registry->lastTransaction()->step, commandStep);
		QVERIFY(std::abs(volume.value() - 0.25f) < 1e-4f);
		volume.setValue(before);
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), coalesceWindow}}).ok);
	}

	//! E19: no record names an "UNIMPLEMENTED" inverse any more - a real one, or an honest one-way record.
	void noRecordNamesAnUnimplementedInverse()
	{
		for (const QJsonValue& record : run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray())
		{
			const QString op = record.toObject().value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString();
			QVERIFY2(!op.contains(QStringLiteral("UNIMPLEMENTED")), qPrintable(op));
		}
	}
};

QTEST_MAIN(UndoHonestyStateTest)
#include "UndoHonestyStateTest.moc"
