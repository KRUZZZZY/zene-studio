/*
 * RealtimeExportMenuTest.cpp - the Hardware I/O menu's real-time render through a hardware insert
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

/*! Q050-089.2: the real application on the Dummy device, configured as a four-output interface whose cable carries
 *  outputs 3/4 back to the input through a box that halves the level (LMMS_DUMMY_LOOPBACK_GAIN). A sample track plays
 *  a tone at 0.5 through a channel whose hardware insert sends to 3/4 and returns from 1/2. The channel's Hardware I/O
 *  menu entry opens render.realtime's form; OK renders while the device plays, and the file it names measures at the
 *  processed level (0.25 peak, about -12 dBTP), not the dry 0.5 an offline export writes. With the send moved to 7/8,
 *  which the device cannot carry, the same entry shows the refusal and writes nothing. */

#include <QtTest>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "HardwareInsert.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MixerHardwareMenu.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

const QStringList Entry{QStringLiteral("Hardware I/O"), QStringLiteral("Render Through Hardware (Real Time)...")};

} // namespace

class RealtimeExportMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("LMMS_DUMMY_OUTPUT_CHANNELS", "4");
		qputenv("LMMS_DUMMY_LOOPBACK", "1");
		qputenv("LMMS_DUMMY_LOOPBACK_OUTPUT", "3");
		qputenv("LMMS_DUMMY_OUTPUT_LATENCY", "100");
		qputenv("LMMS_DUMMY_INPUT_LATENCY", "200");
		qputenv("LMMS_DUMMY_LOOPBACK_GAIN", "0.5");
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);

		// The channel first: a track's channel choice is bounded by the channels there are.
		const ControlResult channel = run(QStringLiteral("mixer.add_channel"));
		QVERIFY2(channel.ok, qPrintable(channel.errorMessage));
		m_channel = channel.result.value(QStringLiteral("channel")).toString();
		const ControlResult track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const QString trackId = track.result.value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("sample.generate"), {{QStringLiteral("track"), trackId}, {QStringLiteral("position"), 96},
			{QStringLiteral("length"), 96}, {QStringLiteral("kind"), QStringLiteral("tone")}, {QStringLiteral("frequency"), 440},
			{QStringLiteral("amplitude"), 0.5}}).ok);
		QCOMPARE(run(QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), trackId},
			{QStringLiteral("channel"), m_channel}}).result.value(QStringLiteral("channel")).toString(), m_channel);
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_channel}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("3/4")}, {QStringLiteral("return"), QStringLiteral("1/2")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), m_channel}}).ok);
		const HardwareInsert& insert = Engine::mixer()->mixerChannel(Engine::mixer()->numChannels() - 1)->m_hardwareIo.insert;
		QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Done), 10000);
	}

	void cleanupTestCase()
	{
		closeForms();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMenuRendersThroughTheInsertAndTheFileMeasuresProcessed()
	{
		QMenu menu;
		addHardwareMenu(&menu, m_channel);
		QAction* entry = menuAction(&menu, Entry);
		QVERIFY2(entry != nullptr, "the Hardware I/O menu has no real-time render entry");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("render.realtime"));
		entry->trigger();
		auto* form = openForm();
		QVERIFY2(form != nullptr, "the entry opened no form");
		const QString out = m_home.filePath(QStringLiteral("through-hardware.wav"));
		QVERIFY(form->setField(QStringLiteral("out"), out));
		form->accept();  // runs render.realtime: the device plays the song meanwhile
		QVERIFY2(form->errorText().isEmpty(), qPrintable(form->errorText()));
		QVERIFY2(QFile::exists(out), qPrintable(form->resultText()));
		QVERIFY2(form->resultText().contains(out), qPrintable(form->resultText()));

		// The file opens and measures at the box's level: 0.5 x 0.5 = 0.25 peak, about -12 dBTP (dry would be -6).
		const ControlResult measured = run(QStringLiteral("meter.measure_file"), {{QStringLiteral("path"), out}});
		QVERIFY2(measured.ok, qPrintable(measured.errorMessage));
		const double peak = measured.result.value(QStringLiteral("true_peak_dbtp")).toDouble(0.0);
		QVERIFY2(peak > -12.8 && peak < -11.5, qPrintable(QString::number(peak)));
		QVERIFY(measured.result.value(QStringLiteral("frames")).toInt() > 0);
		closeForms();
	}

	void aSendTheDeviceCannotCarryIsRefusedInTheForm()
	{
		QVERIFY(run(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), m_channel},
			{QStringLiteral("send"), QStringLiteral("7/8")}}).ok);
		QMenu menu;
		addHardwareMenu(&menu, m_channel);
		QAction* entry = menuAction(&menu, Entry);
		QVERIFY2(entry != nullptr, "the Hardware I/O menu has no real-time render entry");
		entry->trigger();
		auto* form = openForm();
		QVERIFY(form != nullptr);
		const QString out = m_home.filePath(QStringLiteral("refused.wav"));
		QVERIFY(form->setField(QStringLiteral("out"), out));
		form->accept();
		QVERIFY2(form->errorText().contains(QStringLiteral("beyond_device")), qPrintable(form->errorText()));
		QVERIFY(!QFile::exists(out));
		closeForms();
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

private:
	//! The CommandForm the last action opened (they are modeless and deleted on close), or nullptr.
	static CommandForm* openForm()
	{
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			auto* form = qobject_cast<CommandForm*>(widget);
			if (form != nullptr && form->isVisible()) { return form; }
		}
		return nullptr;
	}

	static void closeForms()
	{
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr) { widget->close(); }
		}
	}

	QTemporaryDir m_home;
	QString m_channel;
};

QTEST_MAIN(RealtimeExportMenuTest)
#include "RealtimeExportMenuTest.moc"
