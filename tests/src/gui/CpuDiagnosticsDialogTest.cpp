/*
 * CpuDiagnosticsDialogTest.cpp - the CPU readout's diagnostics window shows what mixer.cpu_load reports
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

/*! Q050-074.1: a render period made late on purpose (a direct slot on nextAudioBuffer spins past the
 *  deadline inside the timed period) at a known song position; then the header's CPU readout's
 *  right-click action - the control the musician uses, triggered, not the handler - opens CPU
 *  diagnostics, whose late-period table shows that mark at its bar.beat and whose synthesis table lists
 *  the instrument track by id, both exactly as the headless mixer.cpu_load read returns them.
 *  Q050-074.2: the same window's audio-device table against mixer.cpu_load's audio_device. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>

#include <chrono>

#include "lmmsconfig.h"

#include "AudioDummy.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "CpuDiagnosticsDialog.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "TransportReadouts.h"

#ifdef LMMS_HAVE_ALSA
#include "AudioAlsa.h"
#endif

using namespace lmms;
using namespace lmms::gui;

class CpuDiagnosticsDialogTest : public QObject
{
	Q_OBJECT

private:
	bool m_spin = false;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		connect(Engine::audioEngine(), &AudioEngine::nextAudioBuffer, this, [this] {
			if (!m_spin) { return; }
			m_spin = false;
			const AudioEngine* engine = Engine::audioEngine();
			const auto period = std::chrono::microseconds(
				std::int64_t{1000000} * engine->framesPerPeriod() / engine->outputSampleRate());
			const auto until = std::chrono::steady_clock::now() + 3 * period;
			while (std::chrono::steady_clock::now() < until) {}
		}, Qt::DirectConnection);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theCpuReadoutOpensTheMarksAgentsRead()
	{
		Song* song = Engine::getSong();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		QVERIFY(track != nullptr);
		track->setName(QStringLiteral("Lead"));

		const tick_t at = 192 + 48;  // bar 2, beat 2 in 4/4 - the transport stands there
		song->setPlayPos(at, Song::PlayMode::Song);
		m_spin = true;
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY(!m_spin);

		const ControlResult load = ControlRegistry::instance()->invoke(QStringLiteral("mixer.cpu_load"));
		QVERIFY2(load.ok, qPrintable(load.errorMessage));
		const QJsonObject late = load.result.value(QStringLiteral("late_periods")).toObject();
		const QJsonArray marks = late.value(QStringLiteral("marks")).toArray();
		QVERIFY2(!marks.isEmpty(), "the late period was not recorded");
		const QJsonObject newest = marks.last().toObject();
		QCOMPARE(newest.value(QStringLiteral("tick")).toInt(), at);
		QCOMPARE(newest.value(QStringLiteral("position")).toString(), QStringLiteral("2.2"));
		QVERIFY(!newest.value(QStringLiteral("playing")).toBool());

		TransportReadouts readouts;
		ReadoutField* cpu = readouts.load();
		QVERIFY(cpu != nullptr);
		QCOMPARE(cpu->contextMenuPolicy(), Qt::ActionsContextMenu);
		QAction* open = cpu->findChild<QAction*>(QStringLiteral("cpuDiagnosticsAction"));
		QVERIFY2(open != nullptr && cpu->actions().contains(open), "the CPU readout has no CPU diagnostics action");
		QCOMPARE(open->property("controlCommand").toString(), QStringLiteral("mixer.cpu_load"));
		open->trigger();

		auto* dialog = readouts.findChild<CpuDiagnosticsDialog*>(QStringLiteral("cpuDiagnosticsDialog"));
		QVERIFY2(dialog != nullptr && dialog->isVisible(), "the action did not open CPU diagnostics");

		QTableWidget* lateTable = dialog->latePeriods();
		QCOMPARE(lateTable->rowCount(), static_cast<int>(marks.size()));
		QCOMPARE(lateTable->item(0, 1)->text(), QStringLiteral("2.2"));  // newest first
		QCOMPARE(lateTable->item(0, 1)->data(Qt::UserRole).toInt(), at);
		QCOMPARE(lateTable->item(0, 0)->text(), QString::number(newest.value(QStringLiteral("serial")).toDouble(), 'f', 0));
		QCOMPARE(lateTable->item(0, 5)->text(), QStringLiteral("stopped"));
		QVERIFY2(dialog->summary()->text().contains(QStringLiteral("late periods: %1")
			.arg(static_cast<qulonglong>(late.value(QStringLiteral("count")).toDouble()))), qPrintable(dialog->summary()->text()));

		const QJsonArray tracks = load.result.value(QStringLiteral("tracks")).toArray();
		QTableWidget* trackTable = dialog->tracks();
		QCOMPARE(trackTable->rowCount(), static_cast<int>(tracks.size()));
		bool listed = false;
		for (int row = 0; row < trackTable->rowCount(); ++row)
		{
			listed = listed || (trackTable->item(row, 0)->data(Qt::UserRole).toString() == control::trackIdOf(track)
				&& trackTable->item(row, 0)->text() == QStringLiteral("Lead"));
		}
		QVERIFY2(listed, "the instrument track is not in the synthesis table");
		dialog->close();
	}

	//! Q050-074.2: the window's audio-device table is mixer.cpu_load's audio_device, row for row - on the
	//! headless Dummy every device reading says "unavailable" (not 0), and on a device that reports its timing
	//! (the ALSA null PCM, where built) the rate, period, buffer, latency and xrun rows carry the command's values.
	void theDeviceTableShowsTheCommandsDeviceState()
	{
		TransportReadouts readouts;
		QAction* open = readouts.load()->findChild<QAction*>(QStringLiteral("cpuDiagnosticsAction"));
		QVERIFY(open != nullptr);
		open->trigger();
		auto* dialog = readouts.findChild<CpuDiagnosticsDialog*>(QStringLiteral("cpuDiagnosticsDialog"));
		QVERIFY(dialog != nullptr && dialog->isVisible());
		verifyDeviceTable(dialog, AudioDummy::name(), false);
#ifdef LMMS_HAVE_ALSA
		ConfigManager::inst()->setValue(QStringLiteral("audioalsa"), QStringLiteral("device"), QStringLiteral("null"));
		bool ok = false;
		auto* alsa = new AudioAlsa(ok, Engine::audioEngine());
		if (!ok) { delete alsa; dialog->close(); QSKIP("the ALSA null PCM does not open here"); }
		Engine::audioEngine()->storeAudioDevice();
		Engine::audioEngine()->setAudioDevice(alsa, false);
		dialog->refresh();
		verifyDeviceTable(dialog, AudioAlsa::name(), true);
		Engine::audioEngine()->restoreAudioDevice();
#endif
		dialog->close();
	}

	//! Q050-074.2 rework: input and output at different rates - the window's input-rate row and input-latency
	//! milliseconds are the command's, at the INPUT rate, and the output side keeps the output rate.
	void theDeviceTableShowsInputAndOutputAtTheirOwnRates()
	{
		AudioEngine* engine = Engine::audioEngine();
		engine->storeAudioDevice();
		engine->setAudioDevice(new SplitRateDevice(engine), false);
		TransportReadouts readouts;
		readouts.load()->findChild<QAction*>(QStringLiteral("cpuDiagnosticsAction"))->trigger();
		auto* dialog = readouts.findChild<CpuDiagnosticsDialog*>(QStringLiteral("cpuDiagnosticsDialog"));
		QVERIFY(dialog != nullptr && dialog->isVisible());
		dialog->refresh();

		const ControlResult load = ControlRegistry::instance()->invoke(QStringLiteral("mixer.cpu_load"));
		QVERIFY(load.ok);
		const QJsonObject device = load.result.value(QStringLiteral("audio_device")).toObject();
		QCOMPARE(device.value(QStringLiteral("sample_rate")).toObject().value(QStringLiteral("value")).toDouble(), 44100.0);
		QCOMPARE(device.value(QStringLiteral("input_sample_rate")).toObject().value(QStringLiteral("value")).toDouble(), 48000.0);
		QCOMPARE(device.value(QStringLiteral("input_latency")).toObject().value(QStringLiteral("ms")).toDouble(), 10.0);
		QCOMPARE(device.value(QStringLiteral("output_latency")).toObject().value(QStringLiteral("ms")).toDouble(), 20.0);

		QTableWidget* table = dialog->audioDevice();
		QCOMPARE(cell(table, QStringLiteral("sample_rate")), QStringLiteral("44100 Hz"));
		QCOMPARE(cell(table, QStringLiteral("input_sample_rate")), QStringLiteral("48000 Hz"));
		QCOMPARE(cell(table, QStringLiteral("input_latency")), QStringLiteral("480 frames (10.00 ms, test_input)"));
		QCOMPARE(cell(table, QStringLiteral("output_latency")), QStringLiteral("882 frames (20.00 ms, test_output)"));
		dialog->close();
		engine->restoreAudioDevice();
	}

private:
	//! A device whose input runs at 48 kHz and output at 44.1 kHz - a separate capture interface's case.
	class SplitRateDevice : public AudioDevice
	{
	public:
		explicit SplitRateDevice(AudioEngine* engine) : AudioDevice(DEFAULT_CHANNELS, engine) {}
		DeviceTiming deviceTiming() const override
		{
			DeviceTiming timing;
			timing.backend = QStringLiteral("split-rate test device");
			timing.sampleRate = 44100;
			timing.inputSampleRate = 48000;
			timing.inputLatencyFrames = 480;
			timing.inputLatencyBasis = QStringLiteral("test_input");
			timing.outputLatencyFrames = 882;
			timing.outputLatencyBasis = QStringLiteral("test_output");
			return timing;
		}

	private:
		void startProcessingImpl() override {}
		void stopProcessingImpl() override {}
	};

	static QString cell(QTableWidget* table, const QString& key)
	{
		for (int row = 0; row < table->rowCount(); ++row)
		{
			if (table->item(row, 0)->data(Qt::UserRole).toString() == key) { return table->item(row, 1)->text(); }
		}
		return QStringLiteral("<no row %1>").arg(key);
	}

	static void verifyDeviceTable(CpuDiagnosticsDialog* dialog, const QString& backend, bool reports)
	{
		dialog->refresh();
		const ControlResult load = ControlRegistry::instance()->invoke(QStringLiteral("mixer.cpu_load"));
		QVERIFY2(load.ok, qPrintable(load.errorMessage));
		const QJsonObject device = load.result.value(QStringLiteral("audio_device")).toObject();
		QCOMPARE(device.value(QStringLiteral("backend")).toString(), backend);
		QTableWidget* table = dialog->audioDevice();
		QVERIFY(table != nullptr);
		QCOMPARE(cell(table, QStringLiteral("backend")), backend);
		const QJsonObject engine = device.value(QStringLiteral("engine")).toObject();
		QCOMPARE(cell(table, QStringLiteral("engine_sample_rate")),
			QStringLiteral("%1 Hz").arg(engine.value(QStringLiteral("sample_rate_hz")).toDouble(), 0, 'f', 0));
		QCOMPARE(cell(table, QStringLiteral("engine_period")),
			QStringLiteral("%1 frames").arg(engine.value(QStringLiteral("period_frames")).toDouble(), 0, 'f', 0));
		QCOMPARE(cell(table, QStringLiteral("late_periods")), QString::number(
			load.result.value(QStringLiteral("late_periods")).toObject().value(QStringLiteral("count")).toDouble(), 'f', 0));
		for (const char* key : {"sample_rate", "period_frames", "buffer_frames", "output_latency", "xruns"})
		{
			const QJsonObject reading = device.value(QLatin1String(key)).toObject();
			QCOMPARE(reading.value(QStringLiteral("available")).toBool(), reports);
			const QString text = cell(table, QLatin1String(key));
			if (!reports) { QCOMPARE(text, QStringLiteral("unavailable")); continue; }
			const QString number = QString::number(reading.value(QStringLiteral("value")).toDouble(), 'f', 0);
			QVERIFY2(text.startsWith(number), qPrintable(QStringLiteral("%1: '%2' vs %3").arg(QLatin1String(key), text, number)));
		}
		QCOMPARE(cell(table, QStringLiteral("input_latency")), QStringLiteral("unavailable"));  // no capture open on either
		QCOMPARE(cell(table, QStringLiteral("input_sample_rate")), QStringLiteral("unavailable"));
	}
};

QTEST_MAIN(CpuDiagnosticsDialogTest)
#include "CpuDiagnosticsDialogTest.moc"
