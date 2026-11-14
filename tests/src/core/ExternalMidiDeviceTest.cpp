/*
 * ExternalMidiDeviceTest.cpp - the External MIDI device of an instrument track (Q050-252.1)
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

/*! Q050-252.1: device.external_set / _get_state / _preset_* configure one instrument track's MIDI output (port,
 *  channel, bank select, program) and keep named presets of it. The output is a recording MIDI client with
 *  ports the test controls, so what REACHES the output is read at the client boundary: bank select CC 0 / CC 32
 *  then the program change, once per change, on the chosen channel, to the track's own port. Presets, bank and
 *  program survive project save/open; one control.undo puts the previous settings back. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiPort.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! A MIDI client with output ports the test controls, recording every event it is handed.
class RecordingMidiClient : public MidiClient
{
public:
	QStringList writable{QStringLiteral("Synth:out"), QStringLiteral("Other:out")};
	QList<QPair<const MidiPort*, MidiEvent>> sent;
	QStringList subscribedOut;
	QStringList destinationsAtLastSend;

	void processOutEvent(const MidiEvent& event, const TimePos&, const MidiPort* port) override
	{
		sent.append({port, event});
		destinationsAtLastSend.clear();
		for (auto it = port->writablePorts().constBegin(); it != port->writablePorts().constEnd(); ++it)
		{
			if (it.value()) { destinationsAtLastSend.append(it.key()); }
		}
	}
	QStringList writablePorts() const override { return writable; }
	void subscribeWritablePort(MidiPort*, const QString& dest, bool subscribe) override
	{
		if (subscribe) { subscribedOut.append(dest); }
	}
};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString text(const MidiEvent& e)
{
	switch (e.type())
	{
		case MidiControlChange: return QStringLiteral("cc%1=%2@%3").arg(e.controllerNumber()).arg(e.controllerValue()).arg(e.channel() + 1);
		case MidiProgramChange: return QStringLiteral("pc%1@%2").arg(e.program() + 1).arg(e.channel() + 1);
		default: return QStringLiteral("other");
	}
}

} // namespace

class ExternalMidiDeviceTest : public QObject
{
	Q_OBJECT

private:
	RecordingMidiClient m_client;
	MidiClient* m_engineClient = nullptr;

	InstrumentTrack* newTrack()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		track->midiPort()->rebindClientForTest(&m_client);
		m_client.sent.clear();
		m_client.subscribedOut.clear();
		return track;
	}

	//! What the output was handed since the last call, as text: "cc0=2@5 cc32=3@5 pc17@5".
	QString outputSince()
	{
		QStringList out;
		for (const auto& item : m_client.sent) { out.append(text(item.second)); }
		m_client.sent.clear();
		return out.join(QLatin1Char(' '));
	}

	static QString id(const InstrumentTrack* track) { return control::trackIdOf(track); }

	ControlResult set(const InstrumentTrack* track, QJsonObject args)
	{
		args.insert(QStringLiteral("track"), id(track));
		return run(QStringLiteral("device.external_set"), args);
	}

	ControlResult preset(const char* verb, const InstrumentTrack* track, const QString& name)
	{
		return run(QStringLiteral("device.external_preset_") + QLatin1String(verb), {{QStringLiteral("track"), id(track)}, {QStringLiteral("name"), name}});
	}

	QJsonObject stateOf(const InstrumentTrack* track)
	{
		return run(QStringLiteral("device.external_get_state"), {{QStringLiteral("track"), id(track)}}).result;
	}

	static int undoDepth() { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();  // the ports leave the recording client before it goes
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_client.writable = QStringList{QStringLiteral("Synth:out"), QStringLiteral("Other:out")};
	}

	//! Criterion 1: the command configures and reports port, channel, bank and program, and the bank and program
	//! reach the selected output - once, in order, on the channel.
	void theCommandConfiguresTheDeviceAndThePatchReachesTheOutput()
	{
		InstrumentTrack* track = newTrack();
		const QJsonObject before = stateOf(track);
		QCOMPARE(before.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(before.value(QStringLiteral("bank_msb")).toInt(), -1);
		QCOMPARE(before.value(QStringLiteral("bank_lsb")).toInt(), -1);
		QCOMPARE(before.value(QStringLiteral("program")).toInt(), 1);
		QCOMPARE(before.value(QStringLiteral("available_ports")).toArray().size(), 2);

		const ControlResult done = set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("channel"), 5},
			{QStringLiteral("bank_msb"), 2}, {QStringLiteral("bank_lsb"), 3}, {QStringLiteral("program"), 17}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(outputSince(), QStringLiteral("cc0=2@5 cc32=3@5 pc17@5"));
		QCOMPARE(m_client.subscribedOut, QStringList{QStringLiteral("Synth:out")});
		QCOMPARE(done.result.value(QStringLiteral("patch_sent")).toBool(), true);
		const QJsonObject after = stateOf(track);
		QCOMPARE(after.value(QStringLiteral("enabled")).toBool(), true);
		QCOMPARE(after.value(QStringLiteral("port")).toString(), QStringLiteral("Synth:out"));
		QCOMPARE(after.value(QStringLiteral("channel")).toInt(), 5);
		QCOMPARE(after.value(QStringLiteral("bank_msb")).toInt(), 2);
		QCOMPARE(after.value(QStringLiteral("bank_lsb")).toInt(), 3);
		QCOMPARE(after.value(QStringLiteral("program")).toInt(), 17);
		QCOMPARE(track->midiPort()->outputBankMsb(), 2);
		QCOMPARE(track->midiPort()->outputProgram(), 17);

		// only the program: the bank that is set goes out again with it (a program change needs its bank)
		QVERIFY(set(track, {{QStringLiteral("program"), 18}}).ok);
		QCOMPARE(outputSince(), QStringLiteral("cc0=2@5 cc32=3@5 pc18@5"));
		// the bank alone is sent with the program it takes effect on
		QVERIFY(set(track, {{QStringLiteral("bank_lsb"), 4}}).ok);
		QCOMPARE(outputSince(), QStringLiteral("cc0=2@5 cc32=4@5 pc18@5"));
		// the same values again: nothing changed, nothing is sent
		const ControlResult same = set(track, {{QStringLiteral("bank_lsb"), 4}, {QStringLiteral("program"), 18}});
		QVERIFY(same.ok);
		QCOMPARE(outputSince(), QString());
		QCOMPARE(same.result.value(QStringLiteral("patch_sent")).toBool(), false);
		// a bank of -1 sends no select: the program change alone, as a port that never chose a bank always did
		QVERIFY(set(track, {{QStringLiteral("bank_msb"), -1}, {QStringLiteral("bank_lsb"), -1}}).ok);
		QCOMPARE(outputSince(), QStringLiteral("pc18@5"));
	}

	//! Another port, or the channel, sends no patch of its own; the output is exclusive to the chosen port; off stops it.
	void aPortOrChannelChangeSendsNoPatchAndOutputIsOffWhenAsked()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}}).ok);
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Other:out")}, {QStringLiteral("channel"), 9}}).ok);
		QCOMPARE(outputSince(), QString());
		const QJsonObject state = stateOf(track);
		QCOMPARE(state.value(QStringLiteral("port")).toString(), QStringLiteral("Other:out"));
		QCOMPARE(state.value(QStringLiteral("ports")).toArray().size(), 1);  // the first was left
		const ControlResult off = set(track, {{QStringLiteral("enabled"), false}, {QStringLiteral("program"), 40}});
		QVERIFY(off.ok);
		QCOMPARE(off.result.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(off.result.value(QStringLiteral("patch_sent")).toBool(), false);  // set, but there is no output to reach
		QCOMPARE(outputSince(), QString());
		QCOMPARE(track->midiPort()->outputProgram(), 40);
	}

	//! A refused call changes nothing and takes no undo step.
	void aRefusedCallChangesNothing()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("program"), 3}}).ok);
		outputSince();
		const QJsonObject before = stateOf(track);
		const int depth = undoDepth();
		QCOMPARE(set(track, {{QStringLiteral("port"), QStringLiteral("nope")}, {QStringLiteral("program"), 9}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(set(track, {{QStringLiteral("port"), QString()}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(set(track, {{QStringLiteral("bank_msb"), 128}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(set(track, {{QStringLiteral("bank_lsb"), -2}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(set(track, {{QStringLiteral("program"), 0}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(set(track, {{QStringLiteral("channel"), 17}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(set(track, {{QStringLiteral("program"), QStringLiteral("seven")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(set(track, {}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("device.external_set"), {{QStringLiteral("track"), QStringLiteral("trk-99999")}, {QStringLiteral("program"), 2}}).ok, false);
		QCOMPARE(preset("recall", track, QStringLiteral("none such")).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(preset("remove", track, QStringLiteral("none such")).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(preset("save", track, QStringLiteral("  ")).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(stateOf(track), before);
		QCOMPARE(outputSince(), QString());
		QCOMPARE(undoDepth(), depth);
	}

	//! Criterion 2 (presets): save, change, recall puts the saved settings back and sends the patch - also when the
	//! values were already those.
	void aPresetSavesAndRecallsItsMidiSettings()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("channel"), 3}, {QStringLiteral("bank_msb"), 1},
			{QStringLiteral("bank_lsb"), 7}, {QStringLiteral("program"), 33}}).ok);
		const ControlResult saved = preset("save", track, QStringLiteral(" Bass "));
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("saved")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Bass"));
		QCOMPARE(saved.result.value(QStringLiteral("replaced")).toBool(), false);

		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Other:out")}, {QStringLiteral("channel"), 10}, {QStringLiteral("bank_msb"), -1},
			{QStringLiteral("bank_lsb"), 0}, {QStringLiteral("program"), 5}}).ok);
		outputSince();
		QVERIFY(preset("save", track, QStringLiteral("Drums")).ok);
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 2);

		const ControlResult recalled = preset("recall", track, QStringLiteral("Bass"));
		QVERIFY2(recalled.ok, qPrintable(recalled.errorMessage));
		const QJsonObject state = stateOf(track);
		QCOMPARE(state.value(QStringLiteral("port")).toString(), QStringLiteral("Synth:out"));
		QCOMPARE(state.value(QStringLiteral("channel")).toInt(), 3);
		QCOMPARE(state.value(QStringLiteral("bank_msb")).toInt(), 1);
		QCOMPARE(state.value(QStringLiteral("bank_lsb")).toInt(), 7);
		QCOMPARE(state.value(QStringLiteral("program")).toInt(), 33);
		QCOMPARE(recalled.result.value(QStringLiteral("port_applied")).toBool(), true);
		QCOMPARE(outputSince(), QStringLiteral("cc0=1@3 cc32=7@3 pc33@3"));
		// again, with nothing to change: the patch still goes out (that is what recalling a preset is for)
		QVERIFY(preset("recall", track, QStringLiteral("Bass")).ok);
		QCOMPARE(outputSince(), QStringLiteral("cc0=1@3 cc32=7@3 pc33@3"));
		// saving over a name replaces it
		QVERIFY(set(track, {{QStringLiteral("program"), 34}}).ok);
		outputSince();
		QCOMPARE(preset("save", track, QStringLiteral("Bass")).result.value(QStringLiteral("replaced")).toBool(), true);
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 2);
		QVERIFY(preset("remove", track, QStringLiteral("Drums")).ok);
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 1);
	}

	//! A preset whose port is not there now applies the rest and says what it could not.
	void aPresetWhosePortIsGoneAppliesTheRestAndSaysSo()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("channel"), 6}, {QStringLiteral("program"), 12}}).ok);
		QVERIFY(preset("save", track, QStringLiteral("Lead")).ok);
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Other:out")}, {QStringLiteral("channel"), 2}, {QStringLiteral("program"), 99}}).ok);
		m_client.writable = QStringList{QStringLiteral("Other:out")};
		const ControlResult recalled = preset("recall", track, QStringLiteral("Lead"));
		QVERIFY(recalled.ok);
		QCOMPARE(recalled.result.value(QStringLiteral("port_applied")).toBool(), false);
		QCOMPARE(recalled.result.value(QStringLiteral("missing_port")).toString(), QStringLiteral("Synth:out"));
		const QJsonObject state = stateOf(track);
		QCOMPARE(state.value(QStringLiteral("port")).toString(), QStringLiteral("Other:out"));  // left where it was
		QCOMPARE(state.value(QStringLiteral("channel")).toInt(), 6);
		QCOMPARE(state.value(QStringLiteral("program")).toInt(), 12);
		m_client.writable = QStringList{QStringLiteral("Synth:out"), QStringLiteral("Other:out")};
	}

	//! A preset saved with output off has no port; recalling it must restore output off, not keep a later route.
	void anEmptyPortPresetRestoresOutputOff()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(preset("save", track, QStringLiteral("Silent")).ok);
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("program"), 12}}).ok);
		outputSince();
		const int depth = undoDepth();

		const ControlResult recalled = preset("recall", track, QStringLiteral("Silent"));
		QVERIFY(recalled.ok);
		QCOMPARE(recalled.result.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(recalled.result.value(QStringLiteral("port")).toString(), QString());
		QCOMPARE(recalled.result.value(QStringLiteral("patch_sent")).toBool(), false);
		QVERIFY(!track->midiPort()->isWritable());
		QCOMPARE(outputSince(), QString());
		QCOMPARE(undoDepth(), depth + 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(stateOf(track).value(QStringLiteral("port")).toString(), QStringLiteral("Synth:out"));
		QVERIFY(track->midiPort()->isWritable());
		QCOMPARE(outputSince(), QStringLiteral("pc12@1"));
	}

	//! Criterion 2 (undo): one control.undo puts every setting back, and the gear is told the patch that returns -
	//! once, not once per setting.
	void oneUndoRestoresTheDevice()
	{
		InstrumentTrack* track = newTrack();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("channel"), 4}, {QStringLiteral("bank_msb"), 5},
			{QStringLiteral("bank_lsb"), 6}, {QStringLiteral("program"), 7}}).ok);
		const QJsonObject first = stateOf(track);
		outputSince();
		const int depth = undoDepth();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Other:out")}, {QStringLiteral("channel"), 11}, {QStringLiteral("bank_msb"), 9},
			{QStringLiteral("bank_lsb"), 8}, {QStringLiteral("program"), 70}}).ok);
		QCOMPARE(undoDepth(), depth + 1);
		outputSince();
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(stateOf(track), first);
		QCOMPARE(outputSince(), QStringLiteral("cc0=5@4 cc32=6@4 pc7@4"));
		QCOMPARE(m_client.destinationsAtLastSend, QStringList{QStringLiteral("Synth:out")});
		QCOMPARE(undoDepth(), depth);

		// a preset's save, recall and removal are each one step, and one undo takes each back
		QVERIFY(preset("save", track, QStringLiteral("A")).ok);
		QVERIFY(set(track, {{QStringLiteral("program"), 20}}).ok);
		QVERIFY(preset("recall", track, QStringLiteral("A")).ok);
		QCOMPARE(track->midiPort()->outputProgram(), 7);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->midiPort()->outputProgram(), 20);
		QVERIFY(preset("remove", track, QStringLiteral("A")).ok);
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the set to 20
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the save
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 0);
		QCOMPARE(track->midiPort()->outputProgram(), 7);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(stateOf(track).value(QStringLiteral("presets")).toArray().size(), 1);
	}

	//! Criterion 2 (project): bank, program, channel and presets are in the saved project; a track that never used
	//! them saves no new attribute and no preset; a project from before them opens with no bank and no presets.
	void theDeviceSavesWithTheProject()
	{
		InstrumentTrack* plain = newTrack();
		{
			QDomDocument doc;
			QDomElement element = doc.createElement(QStringLiteral("track"));
			plain->midiPort()->saveState(doc, element);
			const QString xml = doc.toString();
			QVERIFY2(!xml.contains(QStringLiteral("outputbank")), qPrintable(xml));
			QVERIFY2(!xml.contains(QStringLiteral("devicepreset")), qPrintable(xml));
		}
		InstrumentTrack* track = newTrack();
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}, {QStringLiteral("channel"), 8}, {QStringLiteral("bank_msb"), 11},
			{QStringLiteral("bank_lsb"), 22}, {QStringLiteral("program"), 99}}).ok);
		QVERIFY(preset("save", track, QStringLiteral("Strings & <Pads>")).ok);
		QVERIFY(set(track, {{QStringLiteral("program"), 100}}).ok);
		QVERIFY(preset("save", track, QStringLiteral("Brass")).ok);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = dir.filePath(QStringLiteral("external-device.mmp"));
		const ControlResult wrote = run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}});
		QVERIFY2(wrote.ok, qPrintable(wrote.errorMessage));
		Engine::getSong()->clearProject();
		Engine::getSong()->setLoadOnLaunch(false);
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));

		InstrumentTrack* reloaded = nullptr;
		InstrumentTrack* untouched = nullptr;
		for (Track* candidate : Engine::getSong()->tracks())
		{
			auto* instrument = dynamic_cast<InstrumentTrack*>(candidate);
			if (instrument == nullptr) { continue; }
			if (instrument->midiPort()->outputProgram() == 100) { reloaded = instrument; }
			else { untouched = instrument; }
		}
		QVERIFY2(reloaded != nullptr, "the reopened project has no track with the saved program");
		QVERIFY(untouched != nullptr);
		QCOMPARE(untouched->midiPort()->outputBankMsb(), -1);
		QVERIFY(untouched->midiPort()->devicePresets().isEmpty());
		const QJsonObject state = stateOf(reloaded);
		QCOMPARE(state.value(QStringLiteral("channel")).toInt(), 8);
		QCOMPARE(state.value(QStringLiteral("bank_msb")).toInt(), 11);
		QCOMPARE(state.value(QStringLiteral("bank_lsb")).toInt(), 22);
		const QJsonArray presets = state.value(QStringLiteral("presets")).toArray();
		QCOMPARE(presets.size(), 2);
		QCOMPARE(presets[0].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Strings & <Pads>"));
		QCOMPARE(presets[0].toObject().value(QStringLiteral("program")).toInt(), 99);
		QCOMPARE(presets[0].toObject().value(QStringLiteral("port")).toString(), QStringLiteral("Synth:out"));
		QCOMPARE(presets[1].toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Brass"));
		// a recalled preset on the reloaded track works (its port is not in this engine's client: the rest applies)
		const ControlResult recalled = preset("recall", reloaded, QStringLiteral("Strings & <Pads>"));
		QVERIFY(recalled.ok);
		QCOMPARE(reloaded->midiPort()->outputProgram(), 99);

		// a project from before this feature: a <midiport> with no bank and no presets
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral("<midiport inputchannel=\"0\" outputchannel=\"2\" outputprogram=\"4\" writable=\"0\" readable=\"0\"/>")));
		reloaded->midiPort()->restoreState(doc.documentElement());
		QCOMPARE(reloaded->midiPort()->outputBankMsb(), -1);
		QCOMPARE(reloaded->midiPort()->outputBankLsb(), -1);
		QCOMPARE(reloaded->midiPort()->outputProgram(), 4);
		QCOMPARE(reloaded->midiPort()->outputChannel(), 2);
		QVERIFY(reloaded->midiPort()->devicePresets().isEmpty());
	}

	//! Damaged saved data is clamped and bounded rather than trusted.
	void aDamagedPresetListIsBounded()
	{
		InstrumentTrack* track = newTrack();
		QString xml = QStringLiteral("<midiport outputbankmsb=\"900\" outputprogram=\"4\">");
		xml += QStringLiteral("<devicepreset name=\"\" program=\"3\"/><devicepreset name=\"A\" channel=\"99\" bankmsb=\"-50\" banklsb=\"200\" program=\"0\"/>"
			"<devicepreset name=\"A\" program=\"9\"/>");
		for (int i = 0; i < MidiPort::MaxDevicePresets + 10; ++i) { xml += QStringLiteral("<devicepreset name=\"p%1\"/>").arg(i); }
		xml += QStringLiteral("</midiport>");
		QDomDocument doc;
		QVERIFY(doc.setContent(xml));
		track->midiPort()->restoreState(doc.documentElement());
		const auto& presets = track->midiPort()->devicePresets();
		QCOMPARE(presets.size(), MidiPort::MaxDevicePresets);
		QCOMPARE(presets[0].name, QStringLiteral("A"));  // the empty name and the repeat are dropped
		QCOMPARE(presets[0].channel, 16);
		QCOMPARE(presets[0].bankMsb, -1);
		QCOMPARE(presets[0].bankLsb, 127);
		QCOMPARE(presets[0].program, 1);
		QCOMPARE(track->midiPort()->outputBankMsb(), 127);  // the model clamps its own range
		// the 65th preset is refused by the command, not by silently dropping one
		QVERIFY(set(track, {{QStringLiteral("port"), QStringLiteral("Synth:out")}}).ok);
		QCOMPARE(preset("save", track, QStringLiteral("one too many")).errorKind, ControlErrorKind::Refused);
		QCOMPARE(preset("save", track, QStringLiteral("A")).ok, true);  // replacing one is allowed when full
	}

	//! An instrument track only.
	void anAudioTrackIsRefused()
	{
		Track* sample = Track::create(Track::Type::Sample, Engine::getSong());
		QCOMPARE(run(QStringLiteral("device.external_get_state"), {{QStringLiteral("track"), control::trackIdOf(sample)}}).errorKind, ControlErrorKind::Refused);
	}
};

QTEST_MAIN(ExternalMidiDeviceTest)
#include "ExternalMidiDeviceTest.moc"
