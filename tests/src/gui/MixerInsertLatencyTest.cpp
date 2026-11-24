/*
 * MixerInsertLatencyTest.cpp - a hardware insert's effective latency in pdc.report and the strip tooltip
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

/*! Q050-089.1: the real application, a channel with a hardware insert sending to outputs 3/4 and
 *  returning from inputs 1/2, and a cable between them (the period the engine plays on 3/4 is pushed
 *  back as its capture). A ping measures the loop; pdc.report then lists that figure on the channel
 *  and the channel's strip tooltip, written by the mixer view's own sync, names the same frames. A
 *  typed override replaces it in both; a disabled insert leaves both at nothing. */

#include <QtTest>

#include <QJsonArray>
#include <QTemporaryDir>

#include <cmath>
#include <span>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "HardwareInsert.h"
#include "HardwareIo.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! pdc.report's entry for the channel with id @a id.
QJsonObject pdcChannel(const QString& id)
{
	const ControlResult report = run(QStringLiteral("pdc.report"));
	for (const QJsonValue& value : report.result.value(QStringLiteral("channels")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject(); }
	}
	return {};
}

//! The strip's tooltip after the mixer view's sync (the half-second timer, run now).
QString tooltipOf(int index)
{
	getGUI()->mixerView()->syncWithMixer();
	return getGUI()->mixerView()->channelView(index)->toolTip();
}

//! The tooltip's line for @a frames at the engine's rate, as mixerLatencyText writes it.
QString insertLine(int frames, const QString& source)
{
	const double ms = frames * 1000.0 / Engine::audioEngine()->outputSampleRate();
	return QStringLiteral("Hardware insert latency: %1 frames (%2 ms, %3)").arg(frames).arg(ms, 0, 'f', 1).arg(source);
}

//! Play one period of @a level into the channel (the mixer's own per-period insert call) and return the
//! first frame the insert left in the channel's buffer.
float playIntoInsert(MixerChannel& channel, f_cnt_t frames, float level)
{
	for (f_cnt_t f = 0; f < frames; ++f) { channel.m_buffer[f] = SampleFrame(level, level); }
	hardwareio::processInsert(channel, frames);
	return channel.m_buffer[0][0];
}

} // namespace

class MixerInsertLatencyTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test plays the periods and the cable itself
		ControlRegistry::setReady(true);
		m_index = Engine::mixer()->createChannel();
		m_id = control::channelIdOf(Engine::mixer()->mixerChannel(m_index));
		getGUI()->mixerView()->syncWithMixer();
		getGUI()->mixerView()->syncWithMixer();  // a count change rebuilds the strips
		QVERIFY(getGUI()->mixerView()->channelView(m_index) != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void anInsertThatIsOffAddsNothing()
	{
		const QJsonObject channel = pdcChannel(m_id);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_frames")).toInt(-1), 0);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_source")).toString(), QStringLiteral("off"));
		QVERIFY2(!tooltipOf(m_index).contains(QStringLiteral("Hardware insert")), qPrintable(tooltipOf(m_index)));
	}

	void theMeasuredLatencyIsInTheReportAndTheTooltip()
	{
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_id}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("3/4")}, {QStringLiteral("return"), QStringLiteral("1/2")}}).ok);
		QCOMPARE(pdcChannel(m_id).value(QStringLiteral("hardware_insert_latency_source")).toString(), QStringLiteral("unmeasured"));
		QVERIFY2(tooltipOf(m_index).contains(QStringLiteral("Hardware insert latency: not measured")), qPrintable(tooltipOf(m_index)));

		QVERIFY(run(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), m_id}}).ok);
		AudioEngine* engine = Engine::audioEngine();
		HardwareOutputBus& bus = engine->hardwareOutputs();
		const HardwareInsert& insert = Engine::mixer()->mixerChannel(m_index)->m_hardwareIo.insert;
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> cable(fpp);
		for (int i = 0; i < 64 && insert.pingState() != HardwareInsert::PingState::Done; ++i)
		{
			const std::span<const SampleFrame> played = engine->renderNextPeriod();
			for (f_cnt_t f = 0; f < fpp; ++f) { cable[f] = SampleFrame(bus.sample(2, f, played[f]), bus.sample(3, f, played[f])); }
			engine->pushInputFrames(cable.data(), fpp);
		}
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Done));
		const int measured = insert.settings().measured;
		QVERIFY(measured > 0);

		const QJsonObject channel = pdcChannel(m_id);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_frames")).toInt(-1), measured);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_source")).toString(), QStringLiteral("measured"));
		const QString tip = tooltipOf(m_index);
		QVERIFY2(tip.contains(insertLine(measured, QStringLiteral("measured"))), qPrintable(tip));
	}

	void anOverrideReplacesTheMeasuredFigureInBoth()
	{
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_id}, {QStringLiteral("latency_frames"), 300}}).ok);
		const QJsonObject channel = pdcChannel(m_id);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_frames")).toInt(-1), 300);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_source")).toString(), QStringLiteral("override"));
		QCOMPARE(hardwareio::insertLatency(*Engine::mixer()->mixerChannel(m_index)), 300);  // what PDC itself counts
		const QString tip = tooltipOf(m_index);
		QVERIFY2(tip.contains(insertLine(300, QStringLiteral("typed"))), qPrintable(tip));
		QVERIFY2(!tip.contains(QStringLiteral("measured)")), qPrintable(tip));
	}

	void disablingTheInsertClearsBoth()
	{
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_id}, {QStringLiteral("enabled"), false}}).ok);
		const QJsonObject channel = pdcChannel(m_id);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_frames")).toInt(-1), 0);
		QCOMPARE(channel.value(QStringLiteral("hardware_insert_latency_source")).toString(), QStringLiteral("off"));
		QVERIFY2(!tooltipOf(m_index).contains(QStringLiteral("Hardware insert")), qPrintable(tooltipOf(m_index)));
	}

	/*! Switching the insert off stops the periods that keep its dry delay line moving, and prepare() only
	 *  re-initialises that line when the buffers grow. Re-enabling the insert at the same block size must
	 *  therefore start the line from silence, not emit the audio it was still holding when it went off. */
	void reEnablingDoesNotReplayTheDryAudioBufferedBeforeTheDisable()
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_index);
		HardwareInsert& insert = channel->m_hardwareIo.insert;
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		const int latency = static_cast<int>(fpp);
		// 'mix' 0 is the dry signal alone, delayed by one whole period: nothing of a period has been emitted
		// until a period has been played into the line.
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_id}, {QStringLiteral("enabled"), true},
			{QStringLiteral("mix"), 0.0}, {QStringLiteral("latency_frames"), latency}}).ok);
		QCOMPARE(insert.latencyFrames(), latency);
		playIntoInsert(*channel, fpp, 0.25f);
		QCOMPARE(playIntoInsert(*channel, fpp, 0.25f), 0.25f);  // the dry signal, one period late

		// Off: the periods still run and the bypassed insert passes the dry signal through, so the line keeps
		// the 0.25 it held when the insert went off.
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_id}, {QStringLiteral("enabled"), false}}).ok);
		for (int period = 0; period < 3; ++period) { QCOMPARE(playIntoInsert(*channel, fpp, 0.25f), 0.25f); }

		// On again at the same block size: the line starts from silence. Anything else is the 0.25 buffered
		// before the disable (a stale replay) or the 0.125 played now (no delay at all).
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_id}, {QStringLiteral("enabled"), true}}).ok);
		QCOMPARE(insert.latencyFrames(), latency);
		const float reEnabled = playIntoInsert(*channel, fpp, 0.125f);
		QVERIFY2(reEnabled == 0.f, qPrintable(QStringLiteral("the first period after re-enabling emitted %1 instead of silence: "
			"the audio buffered before the disable is being replayed").arg(reEnabled)));
	}

	/*! The Delete key on a channel that carries an enabled hardware insert: the handler (MixerView.cpp:951) clears
	 *  the channel (Mixer::clearChannel, :781, before the change lock Mixer::deleteChannel takes at Mixer.cpp:1382)
	 *  and then deletes it (:785). The insert's dry line is restarted by the render thread there, and the mixer
	 *  still renders the period after the deletion. */
	void theDeleteKeyRemovesAChannelWithAnEnabledInsert()
	{
		Mixer* mixer = Engine::mixer();
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		const int index = mixer->createChannel();
		const QString id = control::channelIdOf(mixer->mixerChannel(index));
		getGUI()->mixerView()->syncWithMixer();
		getGUI()->mixerView()->syncWithMixer();  // a count change rebuilds the strips
		QVERIFY(getGUI()->mixerView()->channelView(index) != nullptr);
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), id}, {QStringLiteral("enabled"), true},
			{QStringLiteral("mix"), 0.0}, {QStringLiteral("latency_frames"), static_cast<int>(fpp)}}).ok);
		// The line holds a period of dry audio when the key is pressed.
		playIntoInsert(*mixer->mixerChannel(index), fpp, 0.25f);
		playIntoInsert(*mixer->mixerChannel(index), fpp, 0.25f);

		const mix_ch_t before = mixer->numChannels();
		getGUI()->mixerView()->setCurrentMixerChannel(index);
		QTest::keyClick(getGUI()->mixerView(), Qt::Key_Delete);
		QCOMPARE(mixer->numChannels(), static_cast<mix_ch_t>(before - 1));

		// The mixer keeps rendering, and nothing of the deleted channel's insert surfaces in the next period.
		const std::span<const SampleFrame> played = Engine::audioEngine()->renderNextPeriod();
		QCOMPARE(static_cast<f_cnt_t>(played.size()), fpp);
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			QVERIFY(std::isfinite(played[f][0]) && std::isfinite(played[f][1]));
		}
	}

private:
	QTemporaryDir m_home;
	int m_index = -1;
	QString m_id;
};

QTEST_MAIN(MixerInsertLatencyTest)
#include "MixerInsertLatencyTest.moc"
