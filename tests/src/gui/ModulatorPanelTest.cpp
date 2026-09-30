/*
 * ModulatorPanelTest.cpp - Edit > Modulators adds an LFO, shapes it, binds and unbinds a parameter
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

/*! The panel over a real engine with one real effect on a mixer channel (the build's own plugin
 *  modules, LMMS_TEST_PLUGIN_DIR): Add LFO creates a modulator that modulator.get_state reports,
 *  the shape and rate reach modulator.rate_set, the Bind row's pickers offer that channel's effect
 *  and its parameters and bind one at a depth, Unbind and Remove take them off, and one control.undo
 *  puts a removed modulator back. Skipped when this host loads no effect module. */

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "ModulatorPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray modulators()
{
	return run(QStringLiteral("modulator.get_state")).result.value(QStringLiteral("modulators")).toArray();
}

} // namespace

class ModulatorPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
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

	void theLayerIsEditedFromThePanel()
	{
		const int index = Engine::mixer()->createChannel();
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(index));
		bool loaded = false;
		for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")},
			{QStringLiteral("loadable_only"), true}}).result.value(QStringLiteral("devices")).toArray())
		{
			loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), channel},
				{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).ok;
			if (loaded) { break; }
		}
		if (!loaded) { QSKIP("this host loads no effect module"); }

		ModulatorPanel panel;
		QCOMPARE(panel.modulatorCount(), 0);
		QCOMPARE(panel.addModulator(QStringLiteral("triangle"), 1.5), QString());
		QCOMPARE(panel.modulatorCount(), 1);
		QCOMPARE(modulators().size(), 1);
		QCOMPARE(panel.setShapeAndRate(QStringLiteral("square"), 4.0), QString());
		const QJsonObject source = modulators().first().toObject().value(QStringLiteral("source")).toObject();
		QCOMPARE(source.value(QStringLiteral("shape")).toString(), QStringLiteral("square"));
		QCOMPARE(source.value(QStringLiteral("rate")).toDouble(), 4.0);

		// The pickers offer the channel that holds an effect, that effect, and its parameters.
		auto combos = panel.findChildren<QComboBox*>();
		QComboBox* channels = nullptr;
		for (QComboBox* combo : combos) { if (combo->findData(channel) >= 0) { channels = combo; } }
		QVERIFY(channels != nullptr);
		channels->setCurrentIndex(channels->findData(channel));
		const QJsonArray chains = run(QStringLiteral("dsp.get_state"), {{QStringLiteral("target"), channel}})
			.result.value(QStringLiteral("chains")).toArray();
		const QJsonArray parameters = chains.first().toObject().value(QStringLiteral("devices")).toArray()
			.first().toObject().value(QStringLiteral("parameters")).toArray();
		if (parameters.isEmpty()) { QSKIP("the loaded effect has no parameter to modulate"); }
		const QString parameter = parameters.first().toObject().value(QStringLiteral("name")).toString();

		QCOMPARE(panel.bind(channel, 0, parameter, 0.3), QString());
		QCOMPARE(panel.targetCount(), 1);
		const QJsonObject target = modulators().first().toObject().value(QStringLiteral("targets")).toArray()
			.first().toObject();
		QCOMPARE(target.value(QStringLiteral("parameter")).toString(), parameter);
		QVERIFY(std::abs(target.value(QStringLiteral("depth")).toDouble() - 0.3) < 1e-6);
		QVERIFY(!panel.bind(channel, 0, QStringLiteral("no such parameter"), 0.3).isEmpty());

		QCOMPARE(panel.unbindRow(0), QString());
		QCOMPARE(panel.targetCount(), 0);
		QCOMPARE(panel.removeSelected(), QString());
		QCOMPARE(panel.modulatorCount(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		panel.refresh();
		QCOMPARE(panel.modulatorCount(), 1);
	}

	void theWindowCommandOpensIt()
	{
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("window.modulators")));
	}
};

QTEST_MAIN(ModulatorPanelTest)
#include "ModulatorPanelTest.moc"
