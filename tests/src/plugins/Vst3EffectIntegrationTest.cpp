/*
 * Vst3EffectIntegrationTest.cpp - headless end-to-end test of the VST3 effect
 *
 * Copyright (c) 2026 LMMS contributors
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

#include <QtTest>

#include <QFileInfo>

#include "AudioBus.h"
#include "BufferManager.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "Vst3Effect.h"
#include "Vst3EffectControls.h"

#ifndef VST3_TEST_PLUGIN_PATH
#define VST3_TEST_PLUGIN_PATH ""
#endif

namespace lmms
{

class Vst3EffectIntegrationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void testProcessesAudioThroughAudioBus();
	void testStateRoundTripThroughMmp();

private:
	auto makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key;
};

extern "C" Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor;

auto Vst3EffectIntegrationTest::makeKey() const -> Plugin::Descriptor::SubPluginFeatures::Key
{
	Plugin::Descriptor::SubPluginFeatures::Key key;
	key.desc = &vst3effect_plugin_descriptor;
	key.name = QStringLiteral("AGain Sample Accurate");
	key.attributes["file"] = QStringLiteral(VST3_TEST_PLUGIN_PATH);
	key.attributes["class"] = QStringLiteral("AGain Sample Accurate");
	return key;
}

void Vst3EffectIntegrationTest::initTestCase()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
	if (!QFileInfo::exists(QStringLiteral(VST3_TEST_PLUGIN_PATH)))
	{
		QSKIP("no VST3 test plug-in available (build the SDK's 'again' sample)");
	}
}

void Vst3EffectIntegrationTest::cleanupTestCase()
{
	Engine::destroy();
}

void Vst3EffectIntegrationTest::testProcessesAudioThroughAudioBus()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);
	QVERIFY(!effect->isCorrupted());

	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	QVERIFY(controls->paramModels().size() >= 2);

	// id 1 is "Gain"; id 0 is the stepped bypass switch
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);
	gain->setValue(0.5f);

	SampleFrame storage[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		storage[f][0] = 0.5f;
		storage[f][1] = -0.25f;
	}
	SampleFrame* busData[1] = { storage };
	AudioBus bus{busData, 1, fpp};

	QVERIFY(chain.processAudioBuffer(bus));

	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		QVERIFY2(std::abs(storage[f][0] - 0.25f) < 1e-6f,
			qPrintable(QStringLiteral("left %1").arg(storage[f][0])));
		QVERIFY2(std::abs(storage[f][1] + 0.125f) < 1e-6f,
			qPrintable(QStringLiteral("right %1").arg(storage[f][1])));
	}
	qInfo("effect chain: input 0.5/-0.25, gain 0.5 -> measured out[0]=%.9f out[1]=%.9f",
		storage[0][0], storage[0][1]);
}

void Vst3EffectIntegrationTest::testStateRoundTripThroughMmp()
{
	constexpr f_cnt_t fpp = 48;
	BufferManager::init(fpp);

	EffectChain chain{nullptr};
	const auto key = makeKey();
	auto* effect = new Vst3Effect{&chain, &key};
	chain.appendEffect(effect);

	auto* controls = dynamic_cast<Vst3EffectControls*>(effect->controls());
	QVERIFY(controls != nullptr);
	auto* gain = controls->modelForParam(1);
	QVERIFY(gain != nullptr);

	gain->setValue(0.3f);

	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	effect->saveSettings(doc, element);

	gain->setValue(0.9f);
	QVERIFY(std::abs(gain->value() - 0.9f) < 1e-6f);

	effect->loadSettings(element);
	QVERIFY2(std::abs(gain->value() - 0.3f) < 1e-6f,
		qPrintable(QStringLiteral("restored %1").arg(gain->value())));

	SampleFrame storage[fpp];
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		storage[f][0] = 0.5f;
		storage[f][1] = 0.5f;
	}
	SampleFrame* busData[1] = { storage };
	AudioBus bus{busData, 1, fpp};
	QVERIFY(chain.processAudioBuffer(bus));

	QVERIFY2(std::abs(storage[0][0] - 0.15f) < 1e-6f,
		qPrintable(QStringLiteral("processed %1").arg(storage[0][0])));
	qInfo("state round trip: input 0.5, restored gain 0.3 -> measured out[0]=%.9f",
		storage[0][0]);
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::Vst3EffectIntegrationTest)

#include "Vst3EffectIntegrationTest.moc"
