/*
 * WidePinRoutingGuiTest.cpp - the Pin Connector on a discrete-64 bus: Channels menu and pin clicks (Q050-155.2)
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

/*! The real application over a throwaway home. An amplifier sits on a discrete-64 mixer channel; its device card's
 *  pins button opens the Pin Connector, whose matrix grows to the bus's 64 channels. The Channels menu's
 *  "Channels 41-42" entry runs port.set_pins (the agent's command) and moves the device onto that pair in one undo
 *  step; a click on a pin runs port.set_pins with that pin, also one step - and Edit > Undo (the main window's own
 *  undo, the engine's journal) takes it back. */

#include <functional>

#include <QTemporaryDir>

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QMenu>
#include <QToolButton>

#include "AudioEngine.h"
#include "AudioDevice.h"
#include "AudioPortsModel.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePanel.h"
#include "Effect.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "PinConnector.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString deviceNamed(const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == name) { return device.toObject().value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

QStringList pinsOf(const QString& target, const QString& device, const QString& direction)
{
	QStringList out;
	const QJsonObject state = run(QStringLiteral("port.get_state"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device}}).result;
	for (const QJsonValue& value : state.value(QStringLiteral("ports")).toObject().value(direction).toObject()
		.value(QStringLiteral("pins")).toArray())
	{
		out << QStringLiteral("%1:%2").arg(value.toObject().value(QStringLiteral("track_channel")).toInt())
			.arg(value.toObject().value(QStringLiteral("processor_channel")).toInt());
	}
	return out;
}

template<class T>
T* findWidget(const std::function<bool(T*)>& match)
{
	for (QWidget* widget : QApplication::allWidgets())
	{
		if (auto* typed = qobject_cast<T*>(widget); typed != nullptr && match(typed)) { return typed; }
	}
	return nullptr;
}

} // namespace

class WidePinRoutingGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 3) { Engine::mixer()->createChannel(); }
		m_target = control::channelIdOf(Engine::mixer()->mixerChannel(1));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theChannelsMenuAndAPinClickRunThePinCommands()
	{
		const QString catalogued = deviceNamed(QStringLiteral("amplifier"));
		if (catalogued.isEmpty()) { QSKIP("this build has no amplifier module"); }
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_target},
			{QStringLiteral("format"), QStringLiteral("discrete-64")}}).ok);
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), m_target},
			{QStringLiteral("device"), catalogued}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		const QString amp = loaded.result.value(QStringLiteral("id")).toString();
		ControlTarget target;
		ControlResult error;
		QVERIFY(resolveControlTarget(m_target, &target, &error));
		Effect* effect = resolveControlEffect(target, amp, &error);
		QVERIFY(effect != nullptr && effect->audioPortsModel() != nullptr);
		const AudioPortsModel* model = effect->audioPortsModel();
		QCOMPARE(static_cast<int>(model->trackChannelCount()), 2);

		// The device card's pins button opens the connector, addressed to this device.
		DeviceCard::Spec spec;
		spec.target = m_target;
		spec.plugin = amp;
		spec.title = QStringLiteral("Amplifier");
		spec.effect = effect;
		auto* card = new DeviceCard(spec, {});
		auto* pinsButton = findWidget<QToolButton>([card](QToolButton* button) {
			return card->isAncestorOf(button) && button->accessibleName() == QStringLiteral("Route Amplifier's audio pins");
		});
		QVERIFY(pinsButton != nullptr);
		QTest::mouseClick(pinsButton, Qt::LeftButton);
		auto* connector = findWidget<PinConnector>([](PinConnector*) { return true; });
		QVERIFY(connector != nullptr);
		QCOMPARE(static_cast<int>(model->trackChannelCount()), 64);  // its rows are the bus's 64 channels

		// Channels > Channels 41-42: one port.set_pins, one undo step.
		QMenu* menu = connector->channelsMenu();
		QCOMPARE(menu->actions().size(), 32);
		QAction* entry = menu->actions().at(20);
		QCOMPARE(entry->text(), QStringLiteral("Channels 41-42"));
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("port.set_pins"));
		entry->trigger();
		QCOMPARE(pinsOf(m_target, amp, QStringLiteral("in")).join(QLatin1Char(',')), QStringLiteral("40:0,41:1"));
		QCOMPARE(pinsOf(m_target, amp, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("40:0,41:1"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(pinsOf(m_target, amp, QStringLiteral("in")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));
		QCOMPARE(pinsOf(m_target, amp, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));

		// A click on output pin (track channel 41, processor channel 1): port.set_pins, one step Edit > Undo takes back.
		QWidget* outView = nullptr;
		for (QWidget* child : connector->findChildren<QWidget*>())
		{
			if (child->inherits("lmms::gui::PinConnector::MatrixView") && child->toolTip().contains(QStringLiteral("output")))
			{
				outView = child;
			}
		}
		QVERIFY(outView != nullptr);
		outView->resize(outView->sizeHint());
		const int cell = 16;  // PinConnector.h: a pin is 13 px, an inner margin of 1 on each side and a 1 px line
		QTest::mouseClick(outView, Qt::LeftButton, Qt::NoModifier, QPoint{6 + 1 * cell, 6 + 41 * cell});
		QCOMPARE(pinsOf(m_target, amp, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1,41:1"));
		getGUI()->mainWindow()->undo();
		QCOMPARE(pinsOf(m_target, amp, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));
		delete card;
	}

private:
	QTemporaryDir m_home;
	QString m_target;
};

QTEST_MAIN(WidePinRoutingGuiTest)
#include "WidePinRoutingGuiTest.moc"
