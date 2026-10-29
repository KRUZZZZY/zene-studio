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
 *  the instrument track by id, both exactly as the headless mixer.cpu_load read returns them. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTableWidget>

#include <chrono>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "CpuDiagnosticsDialog.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "TransportReadouts.h"

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
};

QTEST_MAIN(CpuDiagnosticsDialogTest)
#include "CpuDiagnosticsDialogTest.moc"
