/*
 * ControllerScriptTest.cpp - Lua control-surface scripts on a bound MIDI port (Q050-081)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Q050-081, against a recording MIDI client (the transport the port talks to is the only fake; the port,
 *  the script, the registry and the mixer are the product's):
 *   1. the shipped example (data/scripts/controller-surface.lua) bound to an input and an output receives a CC
 *      from the input, runs mixer.set_volume through the registry, and sends its feedback CC to the output -
 *      measured at the client, on that script's port; notes run transport.play / transport.stop with LED feedback;
 *   2. errors are bounded and reported: a runaway loop stops at the instruction budget, eight errors in a row
 *      fault the script (later events are dropped and counted), a script that does not compile never starts;
 *   3. delivery never blocks the audio thread: 10,000 events pushed from a realtime thread allocate nothing,
 *      the ring keeps 256 and counts the rest; a lost input port is reported and re-bound when it returns. */

#include <QtTest>

#include <QDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <thread>

#include "AllocationProbe.h"
#include "ControlRegistry.h"
#include "ControllerScript.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "Mixer.h"
#include "RealtimeScope.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! A MIDI client with a port list the test controls, recording what it is asked to send and subscribe.
class RecordingMidiClient : public MidiClient
{
public:
	QStringList readable{QStringLiteral("Pad:in")};
	QStringList writable{QStringLiteral("Pad:out")};
	QList<QPair<const MidiPort*, MidiEvent>> sent;
	QStringList subscribedIn;
	QStringList subscribedOut;

	void processOutEvent(const MidiEvent& event, const TimePos&, const MidiPort* port) override { sent.append({port, event}); }
	QStringList readablePorts() const override { return readable; }
	QStringList writablePorts() const override { return writable; }
	void subscribeReadablePort(MidiPort*, const QString& dest, bool subscribe) override
	{
		if (subscribe) { subscribedIn.append(dest); }
	}
	void subscribeWritablePort(MidiPort*, const QString& dest, bool subscribe) override
	{
		if (subscribe) { subscribedOut.append(dest); }
	}
};

MidiEvent cc(int channel, int controller, int value)
{
	return MidiEvent(MidiControlChange, static_cast<int8_t>(channel - 1), static_cast<int16_t>(controller),
		static_cast<int16_t>(value));
}

MidiEvent noteOn(int key)
{
	return MidiEvent(MidiNoteOn, 0, static_cast<int16_t>(key), 100);
}

} // namespace

class ControllerScriptTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	RecordingMidiClient m_client;

	QString writeScript(const QString& name, const QByteArray& source)
	{
		const QString path = m_dir.filePath(name);
		QFile file(path);
		if (file.open(QIODevice::WriteOnly)) { file.write(source); }
		return path;
	}

	ControllerScript* start(const QString& name, const QString& path, QString* error)
	{
		return ControllerScript::start({name, path, QStringLiteral("Pad:in"), QStringLiteral("Pad:out")}, &m_client, error);
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControllerScript::stopAll();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		ControllerScript::stopAll();
		m_client.sent.clear();
		m_client.readable = {QStringLiteral("Pad:in")};
	}

	//! Criterion 1: the shipped example, MIDI in -> registered command -> feedback out, on the bound port.
	void theExampleRunsACommandAndSendsFeedbackToItsPort()
	{
		QString error;
		ControllerScript* script = start(QStringLiteral("example"),
			QStringLiteral(ZENE_SOURCE_DIR) + QStringLiteral("/data/scripts/controller-surface.lua"), &error);
		QVERIFY2(script != nullptr, qPrintable(error));
		QVERIFY(m_client.subscribedIn.contains(QStringLiteral("Pad:in")));
		QVERIFY(m_client.subscribedOut.contains(QStringLiteral("Pad:out")));

		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		master->m_volumeModel.setValue(1.0f);
		// The event arrives through the port, exactly as the MIDI client delivers it.
		script->port().processInEvent(cc(1, 7, 64));
		QCOMPARE(script->drain(), 1);
		QVERIFY2(std::fabs(master->m_volumeModel.value() - 64.0f / 127.0f) < 0.01f, qPrintable(QStringLiteral("fader %1; log %2")
			.arg(master->m_volumeModel.value()).arg(QString::fromUtf8(QJsonDocument(script->state()).toJson(QJsonDocument::Compact)))));
		QCOMPARE(m_client.sent.size(), 1);
		QCOMPARE(m_client.sent.at(0).first, &script->port());
		QCOMPARE(m_client.sent.at(0).second.type(), MidiControlChange);
		QCOMPARE(static_cast<int>(m_client.sent.at(0).second.controllerNumber()), 7);
		QCOMPARE(static_cast<int>(m_client.sent.at(0).second.controllerValue()), 64);
		// The write is the registry's: recorded, and one undo takes it back.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(master->m_volumeModel.value(), 1.0f);

		Song* song = Engine::getSong();
		script->port().processInEvent(noteOn(94));
		script->drain();
		QVERIFY(song->isPlaying());
		QCOMPARE(m_client.sent.last().second.type(), MidiNoteOn);
		QCOMPARE(static_cast<int>(m_client.sent.last().second.key()), 94);
		script->port().processInEvent(noteOn(93));
		script->drain();
		QVERIFY(!song->isPlaying());
		QCOMPARE(m_client.sent.last().second.type(), MidiNoteOff);

		const QJsonObject state = script->state();
		QCOMPARE(state.value(QStringLiteral("handled")).toInt(), 3);
		QCOMPARE(state.value(QStringLiteral("commands")).toInt(), 4);  // on_start reads the mixer once
		QCOMPARE(state.value(QStringLiteral("feedback_sent")).toInt(), 3);
		QCOMPARE(state.value(QStringLiteral("errors")).toInt(), 0);
		QVERIFY(state.value(QStringLiteral("log")).toArray().contains(QStringLiteral("controller-surface ready")));
	}

	//! Criterion 1, agent side: the same lifecycle through the registered commands.
	void theCommandsStartReadAndStopAScript()
	{
		const QString path = writeScript(QStringLiteral("idle.lua"), "function on_midi(e) end\n");
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult relative = registry->invoke(QStringLiteral("controller.script_start"),
			{{QStringLiteral("name"), QStringLiteral("a")}, {QStringLiteral("path"), QStringLiteral("idle.lua")}});
		QCOMPARE(relative.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult started = registry->invoke(QStringLiteral("controller.script_start"),
			{{QStringLiteral("name"), QStringLiteral("a")}, {QStringLiteral("path"), path}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		const ControlResult state = registry->invoke(QStringLiteral("controller.script_state"));
		QCOMPARE(state.result.value(QStringLiteral("count")).toInt(), 1);
		QVERIFY(registry->invoke(QStringLiteral("controller.script_stop"), {{QStringLiteral("name"), QStringLiteral("a")}}).ok);
		QCOMPARE(registry->invoke(QStringLiteral("controller.script_stop"), {{QStringLiteral("name"), QStringLiteral("a")}}).errorKind,
			ControlErrorKind::NotFound);
	}

	//! Criterion 3: errors are bounded and reported, and the script is stopped after eight in a row.
	void scriptErrorsAreBoundedAndReported()
	{
		QString error;
		const QString broken = writeScript(QStringLiteral("broken.lua"), "function on_midi(e) this is not lua\n");
		QVERIFY(start(QStringLiteral("broken"), broken, &error) == nullptr);
		QVERIFY2(error.contains(QStringLiteral("broken.lua")), qPrintable(error));
		QVERIFY(ControllerScript::find(QStringLiteral("broken")) == nullptr);

		const QString runaway = writeScript(QStringLiteral("runaway.lua"),
			"function on_midi(e)\n  if e.controller == 1 then while true do end end\n"
			"  if e.controller == 2 then error('boom') end\n  surface.send_cc(1, 3, 3)\nend\n");
		ControllerScript* script = start(QStringLiteral("runaway"), runaway, &error);
		QVERIFY2(script != nullptr, qPrintable(error));
		script->port().processInEvent(cc(1, 1, 0));
		QElapsedTimer timer;
		timer.start();
		script->drain();
		QVERIFY2(timer.elapsed() < 2000, "the instruction budget did not stop a runaway loop");
		QJsonObject state = script->state();
		QCOMPARE(state.value(QStringLiteral("errors")).toInt(), 1);
		QVERIFY2(state.value(QStringLiteral("last_error")).toString().contains(QStringLiteral("instruction budget")),
			qPrintable(state.value(QStringLiteral("last_error")).toString()));
		QCOMPARE(state.value(QStringLiteral("faulted")).toBool(), false);
		// A good call resets the run of errors.
		script->port().processInEvent(cc(1, 3, 0));
		script->drain();
		QCOMPARE(m_client.sent.size(), 1);

		for (int i = 0; i < ControllerScript::MaxConsecutiveErrors; ++i) { script->port().processInEvent(cc(1, 2, 0)); }
		script->drain();
		state = script->state();
		QCOMPARE(state.value(QStringLiteral("errors")).toInt(), 1 + ControllerScript::MaxConsecutiveErrors);
		QCOMPARE(state.value(QStringLiteral("faulted")).toBool(), true);
		QVERIFY(state.value(QStringLiteral("last_error")).toString().contains(QStringLiteral("boom")));
		QVERIFY(state.value(QStringLiteral("last_error")).toString().size() <= 512);
		// Faulted: later events are dropped and counted, the script is not run.
		script->port().processInEvent(cc(1, 3, 0));
		script->drain();
		QCOMPARE(m_client.sent.size(), 1);
		QCOMPARE(script->state().value(QStringLiteral("dropped")).toInt(), 1);
	}

	//! Criterion 3: delivery from the audio thread is allocation-free and bounded; port loss is reported.
	void deliveryNeverBlocksTheAudioThreadAndPortLossIsReported()
	{
		QString error;
		const QString path = writeScript(QStringLiteral("count.lua"), "n = 0\nfunction on_midi(e) n = n + 1 end\n");
		ControllerScript* script = start(QStringLiteral("count"), path, &error);
		QVERIFY2(script != nullptr, qPrintable(error));

		constexpr int Flood = 10000;
		std::uint64_t allocations = 1;
		std::uint64_t frees = 1;
		qint64 elapsedUs = 0;
		std::thread audio([&] {
			const RealtimeScope realtime;
			const MidiEvent event = cc(1, 9, 1);
			QElapsedTimer timer;
			test::tlAllocationCount = 0;
			test::tlFreeCount = 0;
			test::tlCountAllocations = true;
			timer.start();
			for (int i = 0; i < Flood; ++i) { script->processInEvent(event); }
			elapsedUs = timer.nsecsElapsed() / 1000;
			test::tlCountAllocations = false;
			allocations = test::tlAllocationCount;
			frees = test::tlFreeCount;
		});
		audio.join();
		QCOMPARE(allocations, std::uint64_t{0});
		QCOMPARE(frees, std::uint64_t{0});
		qInfo("Q050_081_FLOOD events=%d elapsed_us=%lld", Flood, static_cast<long long>(elapsedUs));
		QJsonObject state = script->state();
		QCOMPARE(state.value(QStringLiteral("received")).toInt(), Flood);
		QCOMPARE(state.value(QStringLiteral("dropped")).toInt(), Flood - ControllerScript::InboxCapacity);
		// The UI thread works the backlog off in bounded turns.
		QCOMPARE(script->drain(), ControllerScript::MaxEventsPerTurn);
		int handled = ControllerScript::MaxEventsPerTurn;
		while (const int ran = script->drain()) { handled += ran; }
		QCOMPARE(handled, ControllerScript::InboxCapacity);

		// The input goes away: reported once, not an error of the script.
		m_client.readable.clear();
		script->checkPorts();
		script->checkPorts();
		state = script->state();
		QCOMPARE(state.value(QStringLiteral("port_lost")).toBool(), true);
		QCOMPARE(state.value(QStringLiteral("port_losses")).toInt(), 1);
		QVERIFY(state.value(QStringLiteral("last_error")).toString().contains(QStringLiteral("Pad:in")));
		QCOMPARE(state.value(QStringLiteral("errors")).toInt(), 0);
		// It comes back: bound again, and its events run.
		m_client.subscribedIn.clear();
		m_client.readable = {QStringLiteral("Pad:in")};
		script->checkPorts();
		state = script->state();
		QCOMPARE(state.value(QStringLiteral("port_lost")).toBool(), false);
		QCOMPARE(state.value(QStringLiteral("reconnects")).toInt(), 1);
		QVERIFY(m_client.subscribedIn.contains(QStringLiteral("Pad:in")));
		script->port().processInEvent(cc(1, 9, 1));
		QCOMPARE(script->drain(), 1);
	}

	//! A script cannot reach files or the OS, and cannot start another script.
	void theSandboxHoldsAndScriptsCannotStartScripts()
	{
		QString error;
		const QString path = writeScript(QStringLiteral("sandbox.lua"),
			"function on_midi(e)\n  assert(io == nil and os == nil and require == nil and dofile == nil)\n"
			"  local ok, msg, kind = zene.command('controller.script_start', {name='x', path='/tmp/x.lua'})\n"
			"  assert(ok == nil and kind == 'refused', tostring(msg))\n  surface.send_cc(1, 1, 1)\nend\n");
		ControllerScript* script = start(QStringLiteral("sandbox"), path, &error);
		QVERIFY2(script != nullptr, qPrintable(error));
		script->port().processInEvent(cc(1, 1, 1));
		script->drain();
		QCOMPARE(script->state().value(QStringLiteral("errors")).toInt(), 0);
		QCOMPARE(m_client.sent.size(), 1);
		QVERIFY(ControllerScript::find(QStringLiteral("x")) == nullptr);
	}
};

QTEST_GUILESS_MAIN(ControllerScriptTest)
#include "ControllerScriptTest.moc"
