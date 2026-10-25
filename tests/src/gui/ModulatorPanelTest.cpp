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
#include <QLabel>
#include <QPushButton>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "AutomatableModel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "LfoGraph.h"
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

class PickerTestEffect final : public Effect
{
public:
	explicit PickerTestEffect(Model* parent, const QString& parameterName = QStringLiteral("Gain")) :
		Effect{&s_descriptor, parent, nullptr}, m_parameter{0.5f, 0.0f, 1.0f, 0.01f, this, parameterName} {}
	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Continue; }

private:
	static const Plugin::Descriptor s_descriptor;
	FloatModel m_parameter;
};

const Plugin::Descriptor PickerTestEffect::s_descriptor{"modulatorpaneltest", "Picker Test Effect",
	"Test effect for ModulatorPanelTest", "Zene Studio", 0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};

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

	void clickingAnUnboundLfoGraphIsSafe()
	{
		LfoGraph graph{nullptr};
		QVERIFY(graph.model() == nullptr);
		graph.show();
		QTest::mouseClick(&graph, Qt::LeftButton);
		QVERIFY(graph.model() == nullptr);
	}

	void theChannelPickerShowsAChannelAfterItGainsAnEffect()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* channelObject = Engine::mixer()->mixerChannel(index);
		const QString channel = control::channelIdOf(channelObject);

		ModulatorPanel panel;
		QComboBox* channels = nullptr;
		QComboBox* effects = nullptr;
		QComboBox* parameters = nullptr;
		for (QComboBox* combo : panel.findChildren<QComboBox*>())
		{
			if (combo->accessibleName() == QLatin1String("Mixer channel to modulate")) { channels = combo; }
			if (combo->accessibleName() == QLatin1String("Effect to modulate")) { effects = combo; }
			if (combo->accessibleName() == QLatin1String("Parameter to modulate")) { parameters = combo; }
		}
		QVERIFY(channels != nullptr);
		QVERIFY(effects != nullptr);
		QVERIFY(parameters != nullptr);
		QCOMPARE(channels->findData(channel), -1);

		Effect* effect = new PickerTestEffect(&channelObject->m_fxChain);
		channelObject->m_fxChain.appendEffect(effect);
		channels->showPopup();
		const int channelIndex = channels->findData(channel);
		QVERIFY(channelIndex >= 0);
		channels->setCurrentIndex(channelIndex);
		QCOMPARE(effects->count(), 1);
		QVERIFY(parameters->findText(QStringLiteral("Gain")) >= 0);

		channelObject->m_fxChain.removeEffect(effect);
		delete effect;
		channels->showPopup();
		QCOMPARE(channels->findData(channel), -1);
		QCOMPARE(effects->count(), 0);
	}

	void theEffectPickerShowsEffectsAddedWhileThePanelIsOpen()
	{
		const int index = Engine::mixer()->createChannel();
		MixerChannel* channel = Engine::mixer()->mixerChannel(index);
		const QString id = control::channelIdOf(channel);
		auto* first = new PickerTestEffect(&channel->m_fxChain);
		channel->m_fxChain.appendEffect(first);

		ModulatorPanel panel;
		QComboBox* channels = nullptr;
		QComboBox* effects = nullptr;
		QComboBox* parameters = nullptr;
		for (QComboBox* combo : panel.findChildren<QComboBox*>())
		{
			if (combo->accessibleName() == QLatin1String("Mixer channel to modulate")) { channels = combo; }
			if (combo->accessibleName() == QLatin1String("Effect to modulate")) { effects = combo; }
			if (combo->accessibleName() == QLatin1String("Parameter to modulate")) { parameters = combo; }
		}
		QVERIFY(channels != nullptr);
		QVERIFY(effects != nullptr);
		QVERIFY(parameters != nullptr);
		channels->setCurrentIndex(channels->findData(id));
		QCOMPARE(effects->count(), 1);
		QVERIFY(parameters->findText(QStringLiteral("Gain")) >= 0);

		auto* second = new PickerTestEffect(&channel->m_fxChain, QStringLiteral("Mix"));
		channel->m_fxChain.appendEffect(second);
		effects->showPopup();
		QCOMPARE(effects->count(), 2);

		effects->setCurrentIndex(1);
		QVERIFY(parameters->findText(QStringLiteral("Mix")) >= 0);
		const QString selectedEffect = control::effectIdOf(second);
		channel->m_fxChain.moveUp(second);
		effects->showPopup();
		QCOMPARE(effects->currentData(Qt::UserRole + 1).toString(), selectedEffect);
		QCOMPARE(effects->currentData().toInt(), 0);
		QVERIFY(parameters->findText(QStringLiteral("Mix")) >= 0);

		channel->m_fxChain.removeEffect(first);
		delete first;
		effects->showPopup();
		QCOMPARE(effects->count(), 1);
		QCOMPARE(effects->currentData(Qt::UserRole + 1).toString(), selectedEffect);
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

// The rate box shows two decimals, but a modulator's rate is exact. Choosing another shape must
	// change only the shape: the handler used the box's rounded value and wrote it back, so a rate
	// like 1.234 Hz silently became 1.23 Hz.
	void changingOnlyTheShapeKeepsTheExactRate()
	{
		ModulatorPanel panel;
		QCOMPARE(panel.addModulator(QStringLiteral("triangle"), 1.234), QString());
		panel.selectModulator(panel.modulatorCount() - 1);

		QComboBox* shape = nullptr;
		for (QComboBox* combo : panel.findChildren<QComboBox*>())
		{
			if (combo->property("controlCommand").toString() == QLatin1String("modulator.rate_set")) { shape = combo; }
		}
		QVERIFY(shape != nullptr);
		QString other;
		for (int i = 0; i < shape->count(); ++i)
		{
			if (shape->itemText(i) != shape->currentText()) { other = shape->itemText(i); }
		}
		QVERIFY(!other.isEmpty());
		shape->setCurrentText(other);

		const QJsonObject source = modulators().last().toObject().value(QStringLiteral("source")).toObject();
		QCOMPARE(source.value(QStringLiteral("shape")).toString(), other);
		QVERIFY2(std::abs(source.value(QStringLiteral("rate")).toDouble() - 1.234) < 1e-6,
			qPrintable(QStringLiteral("rate became %1").arg(source.value(QStringLiteral("rate")).toDouble())));
	}

// Every command the buttons run reports a refusal when it fails. The handlers threw the message
	// away, so a refused button did nothing at all and said nothing: Add at the modulator limit, or
	// Remove with nothing selected, left the user with no reason.
	void aRefusedButtonSaysWhy()
	{
		ModulatorPanel panel;
		while (panel.modulatorCount() < 16)
		{
			if (!panel.addModulator(QStringLiteral("triangle"), 1.0).isEmpty()) { QVERIFY2(false, "add refused early"); }
		}
		// The layer is full now: Add is refused, and the panel says so.
		QPushButton* add = nullptr;
		for (QPushButton* button : panel.findChildren<QPushButton*>())
		{
			if (button->property("controlCommand").toString() == QLatin1String("modulator.create")) { add = button; }
		}
		QVERIFY(add != nullptr);
		add->click();
		QCOMPARE(panel.modulatorCount(), 16);
		// Some label in the panel carries the refusal, so the user can read why nothing happened.
		QString said;
		for (QLabel* label : panel.findChildren<QLabel*>())
		{
			if (!label->text().isEmpty()) { said += label->text() + QLatin1Char('\n'); }
		}
		QVERIFY2(said.contains(QStringLiteral("full")), qPrintable(said));
	}
};

QTEST_MAIN(ModulatorPanelTest)
#include "ModulatorPanelTest.moc"
