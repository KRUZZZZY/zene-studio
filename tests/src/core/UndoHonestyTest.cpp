/*
 * UndoHonestyTest.cpp - W1 (0.5.0 catch-up): undo and redo say what really happened, and a gesture is one step
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

/*! W1: control.redo reports a device it could not re-create as control.undo does; a record whose inverse is the journal
 *  but whose call left no step there is refused rather than unwinding an older edit (E19); Copy to Channels (plugin.copy /
 *  chain.copy with a list of destinations) is one undo step; rack.remove_chain of the last chain is undone with its
 *  devices; rack.macro_set_pair sets two macros as one step; track.set_arm_many arms as one step; undoing controller.map
 *  over an LFO connection puts the LFO back; note.pitch_curve_set's before-state names every note's curve. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QStringList>

#include "AudioDevice.h"
#include "AudioEngine.h"
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
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

EffectChain& chainOf(int index) { return Engine::mixer()->mixerChannel(index)->m_fxChain; }

//! The first effect this build can load onto @a target (with @a chain for a rack chain): its fx-<n>, or empty.
QString loadAnEffect(const QString& target, int chain = -1)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
		{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
	{
		QJsonObject args{{QStringLiteral("target"), target}, {QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}};
		if (chain >= 0) { args.insert(QStringLiteral("chain"), chain); }
		const ControlResult loaded = run(QStringLiteral("plugin.load"), args);
		if (loaded.ok) { return loaded.result.value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! The dev-<n> of the effect plugin.list names @a name, or empty.
QString effectDevice(const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == name) { return device.toObject().value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! The chain of instrument track @a track, or nullptr when there is no such track.
EffectChain* trackChain(const QString& track)
{
	ControlResult error;
	auto* instrument = dynamic_cast<InstrumentTrack*>(control::resolveTrack(track, &error));
	return instrument != nullptr ? instrument->audioBusHandle()->effects() : nullptr;
}

//! The plugin names of the devices on @a track's chain, in order.
QStringList devicesOn(const QString& track)
{
	QStringList names;
	if (EffectChain* chain = trackChain(track))
	{
		for (const Effect* effect : chain->effects()) { names << QString::fromUtf8(effect->descriptor()->name); }
	}
	return names;
}

//! The fx-<n> of the first device on @a track, or empty.
QString firstDeviceOn(const QString& track)
{
	EffectChain* chain = trackChain(track);
	return chain != nullptr && !chain->effects().empty() ? control::effectIdOf(chain->effects().front()) : QString();
}

bool undone() { return run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool(); }
bool redone() { return run(QStringLiteral("control.redo")).result.value(QStringLiteral("redone")).toBool(); }

//! track.remove of @a track and the undo that brings it back: an equal track under the same id, but a NEW object, and
//! so are its chain and the devices on it.
bool removedAndRebuilt(const QString& track)
{
	return run(QStringLiteral("track.remove"), {{QStringLiteral("track"), track}}).ok && trackChain(track) == nullptr && undone()
		&& trackChain(track) != nullptr;
}

//! The newest A16 record.
QJsonObject lastRecord()
{
	const QJsonArray all = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	return all.isEmpty() ? QJsonObject() : all.last().toObject();
}

double macroValue(const QString& channel, const QString& macro)
{
	for (const QJsonValue& entry : run(QStringLiteral("rack.get_state"), {{QStringLiteral("channel"), channel}})
		.result.value(QStringLiteral("macros")).toArray())
	{
		if (entry.toObject().value(QStringLiteral("macro")).toString() == macro) { return entry.toObject().value(QStringLiteral("value")).toDouble(); }
	}
	return -1.0;
}

//! A mutating command that claims a journal inverse but pushes no step: the shape E19 is about.
ControlResult claimsAJournalInverse(const QJsonObject&)
{
	QJsonObject result{{QStringLiteral("done"), true}};
	result.insert(QStringLiteral("__transaction"), control::transactionPayload({}, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("test: a journal inverse the call never pushed")));
	return ControlResult::success(result);
}

} // namespace

class UndoHonestyTest : public QObject
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
		ControlCommand claim;
		claim.id = QStringLiteral("test.claims_journal_inverse");
		claim.group = QStringLiteral("test");
		claim.verb = QStringLiteral("claims_journal_inverse");
		claim.mutating = true;
		claim.handler = claimsAJournalInverse;
		ControlRegistry::instance()->registerCommand(claim);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A redo that re-creates a device which cannot be built FAILS typed - it used to answer 'redone: true'.
	void redoReportsADeviceItCouldNotRecreate()
	{
		EffectChain* chain = &chainOf(1);
		const std::size_t before = chain->effects().size();
		control::addStructuralUndoStep([] {}, [chain] { control::recreateEffectFromState(chain, QStringLiteral("<notadevice/>"), 0); }, 16);
		Engine::projectJournal()->undo();
		const ControlResult redo = run(QStringLiteral("control.redo"));
		QVERIFY2(!redo.ok, "control.redo answered success over a device it did not re-create");
		QCOMPARE(redo.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(redo.errorMessage.contains(QStringLiteral("NOT restored")), qPrintable(redo.errorMessage));
		QVERIFY2(redo.errorMessage.contains(QStringLiteral("redo")), qPrintable(redo.errorMessage));
		QCOMPARE(chain->effects().size(), before);
		Engine::projectJournal()->undo();  // the test's own step off the stack again
	}

	//! E19: a record whose inverse is the journal, from a call that pushed no step, is refused - the older edit stays.
	void anUnjournalledRecordIsRefusedNotMisattributed()
	{
		FloatModel& volume = Engine::mixer()->mixerChannel(1)->m_volumeModel;
		QVERIFY(run(QStringLiteral("mixer.set_volume"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("volume"), 0.25}}).ok);
		QVERIFY(run(QStringLiteral("test.claims_journal_inverse")).ok);
		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(!undo.ok, "control.undo unwound the journal for a call that left no step on it");
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QVERIFY2(undo.errorMessage.contains(QStringLiteral("OLDER edit")), qPrintable(undo.errorMessage));
		QVERIFY(std::abs(volume.value() - 0.25f) < 1e-4f);  // the volume edit underneath was not taken back
	}

	//! Copy to Channels: one plugin.copy naming several channels is ONE undo step, redone whole.
	void copyingADeviceToSeveralChannelsIsOneStep()
	{
		for (int i = 1; i <= 3; ++i) { chainOf(i).clear(); }
		const QString plugin = loadAnEffect(ch(1));
		if (plugin.isEmpty()) { QSKIP("this build loads no effect"); }
		const ControlResult copied = run(QStringLiteral("plugin.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("to"), QJsonArray{ch(2), ch(3)}}});
		QVERIFY2(copied.ok, qPrintable(copied.errorMessage));
		QCOMPARE(copied.result.value(QStringLiteral("to")).toArray().size(), 2);
		QCOMPARE(copied.result.value(QStringLiteral("copies")).toArray().size(), 2);
		QCOMPARE(copied.result.value(QStringLiteral("count")).toInt(), 2);
		QCOMPARE(chainOf(2).effects().size(), std::size_t{1});
		QCOMPARE(chainOf(3).effects().size(), std::size_t{1});
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // ONE undo takes both copies back
		QVERIFY(chainOf(2).effects().empty() && chainOf(3).effects().empty());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(chainOf(2).effects().size() == 1 && chainOf(3).effects().size() == 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// A list that names a channel twice, or a channel that is not there, copies nothing.
		QCOMPARE(run(QStringLiteral("plugin.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("to"), QJsonArray{ch(2), ch(2)}}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!run(QStringLiteral("plugin.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("to"), QJsonArray{ch(2), QStringLiteral("ch-9999")}}}).ok);
		QVERIFY(chainOf(2).effects().empty());
		// One destination as a string still answers 'to' as a string.
		const ControlResult single = run(QStringLiteral("plugin.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), plugin},
			{QStringLiteral("to"), ch(2)}});
		QVERIFY(single.ok && single.result.value(QStringLiteral("to")).isString());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	//! Copy Chain to Channels: the same for chain.copy; a list that names the source is refused whole.
	void copyingAChainToSeveralChannelsIsOneStep()
	{
		if (chainOf(1).effects().empty()) { QSKIP("this build loads no effect"); }
		const ControlResult copied = run(QStringLiteral("chain.copy"), {{QStringLiteral("target"), ch(1)},
			{QStringLiteral("to"), QJsonArray{ch(2), ch(3)}}});
		QVERIFY2(copied.ok, qPrintable(copied.errorMessage));
		QCOMPARE(chainOf(2).effects().size(), chainOf(1).effects().size());
		QCOMPARE(chainOf(3).effects().size(), chainOf(1).effects().size());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(chainOf(2).effects().empty() && chainOf(3).effects().empty());
		QCOMPARE(run(QStringLiteral("chain.copy"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("to"), QJsonArray{ch(2), ch(1)}}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(chainOf(2).effects().empty());
	}

	//! E19: removing the rack's LAST chain is undone with its devices and redone; an inner chain records no inverse.
	void removingTheLastRackChainIsUndone()
	{
		Rack& rack = Engine::mixer()->mixerChannel(4)->m_rack;
		const int chain = run(QStringLiteral("rack.add_chain"), {{QStringLiteral("channel"), ch(4)}}).result.value(QStringLiteral("chain")).toInt();
		QVERIFY(chain >= 1);
		if (loadAnEffect(ch(4), chain).isEmpty()) { QSKIP("this build loads no effect"); }
		const ControlResult removed = run(QStringLiteral("rack.remove_chain"), {{QStringLiteral("channel"), ch(4)}, {QStringLiteral("chain"), chain}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QVERIFY(lastRecord().value(QStringLiteral("reversible")).toBool());
		QCOMPARE(rack.chainCount(), chain);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(rack.chainCount(), chain + 1);
		QCOMPARE(rack.chain(chain)->effects().size(), std::size_t{1});  // back with its device
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(rack.chainCount(), chain);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// An inner chain: a rack appends chains, so it cannot come back at its index - the record says so.
		QVERIFY(run(QStringLiteral("rack.add_chain"), {{QStringLiteral("channel"), ch(4)}}).ok);
		QVERIFY(run(QStringLiteral("rack.remove_chain"), {{QStringLiteral("channel"), ch(4)}, {QStringLiteral("chain"), chain}}).ok);
		QVERIFY(!lastRecord().value(QStringLiteral("reversible")).toBool());
		QCOMPARE(run(QStringLiteral("control.undo")).errorKind, ControlErrorKind::Irreversible);
	}

	//! The XY pad's command: two macros as one step, a drag (a run of calls) coalesced into it; a pair is set whole or not.
	void twoMacrosAreSetAsOneStep()
	{
		const QString channel = ch(3);
		const auto add = [&channel](const char* name) {
			return run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QLatin1String(name)}})
				.result.value(QStringLiteral("macro")).toString();
		};
		const QString x = add("Cutoff");
		const QString y = add("Drive");
		QVERIFY(!x.isEmpty() && !y.isEmpty() && x != y);
		const double x0 = macroValue(channel, x);
		const double y0 = macroValue(channel, y);
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 60000}}).ok);
		const auto pair = [&](double xv, double yv) {
			return run(QStringLiteral("rack.macro_set_pair"), {{QStringLiteral("channel"), channel}, {QStringLiteral("x_macro"), x},
				{QStringLiteral("x_value"), xv}, {QStringLiteral("y_macro"), y}, {QStringLiteral("y_value"), yv}});
		};
		const ControlResult first = pair(0.2, 0.8);
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		QVERIFY(first.result.contains(QStringLiteral("x")) && first.result.contains(QStringLiteral("y")));
		QVERIFY(pair(0.3, 0.9).ok);
		QVERIFY(std::abs(macroValue(channel, x) - 0.3) < 1e-4 && std::abs(macroValue(channel, y) - 0.9) < 1e-4);
		const ControlResult undo = run(QStringLiteral("control.undo"));  // ONE undo: the whole drag, both macros
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QCOMPARE(undo.result.value(QStringLiteral("undone_commands")).toInt(), 2);
		QVERIFY(std::abs(macroValue(channel, x) - x0) < 1e-4 && std::abs(macroValue(channel, y) - y0) < 1e-4);
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), control::UndoCoalesceWindowMs}}).ok);
		// The same macro twice, or a macro that is not there, writes neither.
		QCOMPARE(run(QStringLiteral("rack.macro_set_pair"), {{QStringLiteral("channel"), channel}, {QStringLiteral("x_macro"), x},
			{QStringLiteral("x_value"), 0.5}, {QStringLiteral("y_macro"), x}, {QStringLiteral("y_value"), 0.5}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!run(QStringLiteral("rack.macro_set_pair"), {{QStringLiteral("channel"), channel}, {QStringLiteral("x_macro"), x},
			{QStringLiteral("x_value"), 0.7}, {QStringLiteral("y_macro"), QStringLiteral("macro-999")}, {QStringLiteral("y_value"), 0.5}}).ok);
		QVERIFY(std::abs(macroValue(channel, x) - x0) < 1e-4);
	}

	//! Arm All: one track.set_arm_many is ONE undo step that disarms exactly what it armed.
	void armingSeveralTracksIsOneStep()
	{
		QJsonArray tracks;
		for (int i = 0; i < 3; ++i)
		{
			const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
			tracks.append(added.result.value(QStringLiteral("track")).toString());
		}
		const ControlResult armed = run(QStringLiteral("track.set_arm_many"), {{QStringLiteral("tracks"), tracks}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("tracks")).toArray().size(), 3);
		ControlResult error;
		for (const QJsonValue& id : tracks) { QVERIFY(control::trackRecordArmed(control::resolveTrack(id.toString(), &error))); }
		// Every one already armed: a typed busy, not a second take.
		QCOMPARE(run(QStringLiteral("track.set_arm_many"), {{QStringLiteral("tracks"), tracks}, {QStringLiteral("armed"), true}}).errorKind,
			ControlErrorKind::Busy);
		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QCOMPARE(undo.result.value(QStringLiteral("restored_by")).toString(), QStringLiteral("track.set_arm_many"));
		for (const QJsonValue& id : tracks) { QVERIFY(!control::trackRecordArmed(control::resolveTrack(id.toString(), &error))); }
		for (const QJsonValue& route : armed.result.value(QStringLiteral("routes")).toArray())  // the takes stay on disk: tidy up
		{
			QFile::remove(route.toObject().value(QStringLiteral("file")).toString());
		}
		QCOMPARE(run(QStringLiteral("track.set_arm_many"), {{QStringLiteral("tracks"), QJsonArray{}}, {QStringLiteral("armed"), true}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	//! A map over an LFO connection: undo puts the LFO back (it used to leave the parameter unconnected).
	void undoingAMapOverAnLfoRestoresIt()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* lfo = new LfoController(Engine::getSong());
		Engine::getSong()->addController(lfo);
		track->volumeModel()->setControllerConnection(new ControllerConnection(lfo));
		const ControlResult mapped = run(QStringLiteral("controller.map"), {{QStringLiteral("target"), track->volumeModel()->fullDisplayName()},
			{QStringLiteral("controller"), 7}});
		QVERIFY2(mapped.ok, qPrintable(mapped.errorMessage));
		QVERIFY(dynamic_cast<MidiController*>(track->volumeModel()->controllerConnection()->getController()) != nullptr);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(track->volumeModel()->controllerConnection() != nullptr);
		QCOMPARE(track->volumeModel()->controllerConnection()->getController(), static_cast<Controller*>(lfo));
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(dynamic_cast<MidiController*>(track->volumeModel()->controllerConnection()->getController()) != nullptr);
	}

	//! The soft-takeover point is part of the binding: undoing a remap puts the OLD binding back, custom target included.
	void undoingARemapKeepsTheSoftTakeoverTarget()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		// Named, because the parameter is addressed by its display name and an unnamed
		// track shares that name with every other one the earlier cases created.
		track->setName(QStringLiteral("TakeoverTarget"));
		AutomatableModel* volume = track->volumeModel();
		const ControlResult mapped = run(QStringLiteral("controller.map"), {{QStringLiteral("target"), volume->fullDisplayName()},
			{QStringLiteral("controller"), 21}, {QStringLiteral("soft_takeover"), true}});
		QVERIFY2(mapped.ok, qPrintable(mapped.errorMessage));
		auto* bound = dynamic_cast<MidiController*>(volume->controllerConnection()->getController());
		QVERIFY(bound != nullptr);
		bound->setSoftTakeoverTarget(0.75f);
		QCOMPARE(bound->softTakeoverTarget(), 0.75f);

		// Remap the same parameter elsewhere: the fresh binding starts from the default point.
		const ControlResult remapped = run(QStringLiteral("controller.map"), {{QStringLiteral("target"), volume->fullDisplayName()},
			{QStringLiteral("controller"), 22}});
		QVERIFY2(remapped.ok, qPrintable(remapped.errorMessage));
		auto* moved = dynamic_cast<MidiController*>(volume->controllerConnection()->getController());
		QVERIFY(moved != nullptr);
		QCOMPARE(moved->midiPort().inputController(), 22);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		auto* restored = dynamic_cast<MidiController*>(volume->controllerConnection()->getController());
		QVERIFY(restored != nullptr);
		QCOMPARE(restored->midiPort().inputController(), 21);
		QVERIFY2(restored->softTakeoverEnabled(), "undoing the remap must restore the flag, not only the address");
		QCOMPARE(restored->softTakeoverTarget(), 0.75f);
	}

	//! note.pitch_curve_set over several notes: the before-state names each note's curve, not only the first one's.
	void aCurveWriteRecordsEveryNotesBefore()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(24), TimePos(0), 60), false);
		clip->addNote(Note(TimePos(24), TimePos(48), 64), false);
		clip->rearrangeAllNotes();
		const QString clipId = control::clipIdOf(clip);
		const QString first = control::noteIdOf(clip->notes().at(0));
		const QString second = control::noteIdOf(clip->notes().at(1));
		const QJsonArray bend{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("semitones"), 2.0}}};
		QVERIFY(run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("note"), second},
			{QStringLiteral("points"), bend}}).ok);
		QVERIFY(run(QStringLiteral("note.pitch_curve_set"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("notes"), QJsonArray{first, second}},
			{QStringLiteral("points"), QJsonArray{}}}).ok);
		const QJsonArray notes = lastRecord().value(QStringLiteral("before")).toObject().value(QStringLiteral("notes")).toArray();
		QCOMPARE(notes.size(), 2);
		QCOMPARE(notes.at(0).toObject().value(QStringLiteral("points")).toArray().size(), 0);
		QVERIFY(!notes.at(1).toObject().value(QStringLiteral("points")).toArray().isEmpty());  // the second note's bend
	}

	//! plugin.load, plugin.replace and plugin.unload find their chain again by the track's id, and the device by its slot:
	//! track.remove and its undo rebuild the track - its chain and its devices too - as NEW objects, which a kept
	//! EffectChain* or Effect* named as freed memory and a guarded one no longer acted on.
	void deviceStepsFindTheirChainAfterTheTrackIsRebuilt()
	{
		const QString amplifier = effectDevice(QStringLiteral("amplifier"));
		const QString booster = effectDevice(QStringLiteral("bassbooster"));
		if (amplifier.isEmpty() || booster.isEmpty()) { QSKIP("this build has no amplifier or bass booster module"); }
		const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}}).result
			.value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), amplifier}}).ok);
		const QStringList amplified = devicesOn(track);
		QCOMPARE(amplified.size(), 1);
		QVERIFY(removedAndRebuilt(track));
		QVERIFY(undone());  // plugin.load's step, on the rebuilt chain
		QVERIFY(devicesOn(track).isEmpty());
		QVERIFY(redone());
		QCOMPARE(devicesOn(track), amplified);

		QVERIFY(run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), firstDeviceOn(track)},
			{QStringLiteral("device"), booster}}).ok);
		const QStringList boosted = devicesOn(track);
		QCOMPARE(boosted.size(), 1);
		QVERIFY(boosted != amplified);
		QVERIFY(removedAndRebuilt(track));
		QVERIFY(undone());  // the old device back at its slot, the new one out
		QCOMPARE(devicesOn(track), amplified);
		QVERIFY(redone());
		QCOMPARE(devicesOn(track), boosted);

		QVERIFY(run(QStringLiteral("plugin.unload"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), firstDeviceOn(track)}}).ok);
		QVERIFY(removedAndRebuilt(track));
		QVERIFY(undone());  // the removal's step re-creates the device on the rebuilt chain...
		QCOMPARE(devicesOn(track), boosted);
		QVERIFY(redone());  // ...and its redo takes that one out again
		QVERIFY(devicesOn(track).isEmpty());
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

QTEST_MAIN(UndoHonestyTest)
#include "UndoHonestyTest.moc"
