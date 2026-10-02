/*
 * Vst3HostTest.cpp - integration tests for the in-process VST3 host
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

#include <QtTest>

#include <QFileInfo>

#include <cmath>
#include <vector>

#include "Midi.h"
#include "Vst3Host.h"
#include "Vst3MidiMapping.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#ifndef VST3_TEST_PLUGIN_PATH
#define VST3_TEST_PLUGIN_PATH ""
#endif

namespace lmms::vst3
{

namespace
{
constexpr double TestSampleRate = 48000.0;
constexpr int TestBlockSize = 512;
constexpr int TestFrames = 256;

//! Plug-in under test: the SDK's "again-sample-accurate" example, a
//! sample-accurate gain plug-in. It has two parameters: a stepped bypass
//! switch (id 0) and a continuous gain (id 1) whose normalized value is the
//! linear gain factor, which makes it easy to assert exact sample values.
constexpr const char* TestPluginClass = "AGain Sample Accurate";
constexpr std::uint32_t BypassParamId = 0;
constexpr std::uint32_t GainParamId = 1;

auto rms(const std::vector<float>& data) -> double
{
	double sum = 0.0;
	for (const auto value : data)
	{
		sum += static_cast<double>(value) * static_cast<double>(value);
	}
	return data.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(data.size()));
}
//! An edit controller whose IMidiMapping maps CC74 on channel 3 to parameter 74 and channel
//! pressure on channel 3 to parameter 128 - the two MPE axes on one member channel.
class MappingController : public Steinberg::Vst::IEditController, public Steinberg::Vst::IMidiMapping
{
public:
	virtual ~MappingController() = default;
	Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32, Steinberg::int16 channel,
		Steinberg::Vst::CtrlNumber number, Steinberg::Vst::ParamID& id) override
	{
		if (channel != 3 || (number != 74 && number != Steinberg::Vst::kAfterTouch)) { return Steinberg::kResultFalse; }
		id = static_cast<Steinberg::Vst::ParamID>(number);
		return Steinberg::kResultTrue;
	}
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
	{
		if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IMidiMapping::iid))
		{
			*obj = static_cast<Steinberg::Vst::IMidiMapping*>(this);
			return Steinberg::kResultOk;
		}
		*obj = nullptr;
		return Steinberg::kNoInterface;
	}
	Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
	Steinberg::uint32 PLUGIN_API release() override { return 1; }
	Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API terminate() override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override { return Steinberg::kResultOk; }
	Steinberg::int32 PLUGIN_API getParameterCount() override { return 0; }
	Steinberg::tresult PLUGIN_API getParameterInfo(Steinberg::int32, Steinberg::Vst::ParameterInfo&) override { return Steinberg::kResultFalse; }
	Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue, Steinberg::Vst::String128) override { return Steinberg::kResultFalse; }
	Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID, Steinberg::Vst::TChar*, Steinberg::Vst::ParamValue&) override { return Steinberg::kResultFalse; }
	Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue v) override { return v; }
	Steinberg::Vst::ParamValue PLUGIN_API plainParamToNormalized(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue v) override { return v; }
	Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID) override { return 0.0; }
	Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override { return Steinberg::kResultOk; }
	Steinberg::tresult PLUGIN_API setComponentHandler(Steinberg::Vst::IComponentHandler*) override { return Steinberg::kResultOk; }
	Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString) override { return nullptr; }
};

} // namespace

class Vst3HostTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void testIdentity();
	void testParameters();
	void testParameterSetGet();
	void testStateRoundTrip();
	void testAudioGain();
	void testAudioSilenceWhenGainIsZero();
	void testChannelPartition();
	void testMidiControllersReachMappedParameters();

private:
	void process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
		std::vector<std::vector<float>>& outputs, int frames);

	HostedPlugin m_plugin;
};

void Vst3HostTest::initTestCase()
{
	const QString path = QStringLiteral(VST3_TEST_PLUGIN_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		QSKIP("no VST3 test plug-in available (build the SDK's 'again-sample-accurate' "
			"sample and configure with -DLMMS_VST3_TEST_PLUGIN=<bundle>)");
	}
	QString error;
	QVERIFY2(m_plugin.load(path, QString::fromLatin1(TestPluginClass), &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QString prepareError;
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &prepareError), qPrintable(prepareError));
}

void Vst3HostTest::testIdentity()
{
	QCOMPARE(m_plugin.className(), QString::fromLatin1(TestPluginClass));
	QVERIFY(m_plugin.vendor().contains(QStringLiteral("Steinberg")));
	QCOMPARE(m_plugin.isInstrument(), false);

	const auto& layout = m_plugin.busLayout();
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
	QCOMPARE(layout.inputBusChannels.size(), std::size_t{1});
	QCOMPARE(layout.outputBusChannels.size(), std::size_t{1});
}

void Vst3HostTest::testParameters()
{
	const auto& parameters = m_plugin.parameters();
	QVERIFY(parameters.size() >= 2);

	// parameters are ordered by id: 0 = Bypass (stepped switch, no unit),
	// 1 = Gain (continuous, normalized == linear gain, displayed in percent)
	QCOMPARE(parameters[0].id, BypassParamId);
	QCOMPARE(parameters[0].title, QStringLiteral("Bypass"));
	QCOMPARE(parameters[0].stepped, true);
	QCOMPARE(parameters[0].bypass, true);

	QCOMPARE(parameters[1].id, GainParamId);
	QCOMPARE(parameters[1].title, QStringLiteral("Gain"));
	QCOMPARE(parameters[1].units, QStringLiteral("%"));
	QCOMPARE(parameters[1].stepped, false);
	QCOMPARE(parameters[1].bypass, false);
	QVERIFY(std::abs(parameters[1].defaultNormalized - 1.0f) < 1e-6f);

	// ids must be unique and indexable without allocation
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		QCOMPARE(m_plugin.paramIndex(parameters[i].id), static_cast<int>(i));
	}
	QCOMPARE(m_plugin.paramIndex(0xdeadbeefu), -1);
}

void Vst3HostTest::testParameterSetGet()
{
	m_plugin.setParamNormalized(GainParamId, 0.25f);
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.25f);

	// out of range values are clamped, not wrapped
	m_plugin.setParamNormalized(GainParamId, 2.0f);
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 1.0f);
	m_plugin.setParamNormalized(GainParamId, -1.0f);
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.0f);

	// unknown ids are ignored
	m_plugin.setParamNormalized(0xdeadbeefu, 0.5f);
	QCOMPARE(m_plugin.paramNormalized(0xdeadbeefu), 0.0f);
}

void Vst3HostTest::testStateRoundTrip()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamNormalized(GainParamId, 0.3f);
	// Parameter changes reach the processor through process(); a host saves the
	// plug-in state after the value has been delivered.
	process(m_plugin, inputs, outputs, TestFrames);

	QByteArray componentState;
	QByteArray controllerState;
	QVERIFY(m_plugin.saveState(&componentState, &controllerState));
	QVERIFY(!componentState.isEmpty());

	m_plugin.setParamNormalized(GainParamId, 0.9f);
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.9f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.45) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	QVERIFY(m_plugin.loadState(componentState, controllerState));
	QVERIFY(std::abs(m_plugin.paramNormalized(GainParamId) - 0.3f) < 1e-6f);

	// the restored state must reach the processor, not just the controller
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.15) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void Vst3HostTest::testAudioGain()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	// the normalized gain value is the linear gain factor
	m_plugin.setParamNormalized(GainParamId, 0.5f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
	QVERIFY2(std::abs(rms(outputs[1]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[1]))));

	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	m_plugin.setParamNormalized(GainParamId, 1.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.5) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void Vst3HostTest::testAudioSilenceWhenGainIsZero()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamNormalized(GainParamId, 0.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QCOMPARE(rms(outputs[0]), 0.0);
	QCOMPARE(rms(outputs[1]), 0.0);
}

void Vst3HostTest::testChannelPartition()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.0f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	std::fill(inputs[0].begin(), inputs[0].end(), 0.5f);
	std::fill(inputs[1].begin(), inputs[1].end(), 0.25f);

	m_plugin.setParamNormalized(GainParamId, 1.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY(std::abs(rms(outputs[0]) - 0.5) < 1e-6);
	QVERIFY(std::abs(rms(outputs[1]) - 0.25) < 1e-6);
}

void Vst3HostTest::process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
	std::vector<std::vector<float>>& outputs, int frames)
{
	std::vector<const float*> inputPointers;
	inputPointers.reserve(inputs.size());
	for (const auto& channel : inputs) { inputPointers.push_back(channel.data()); }

	std::vector<float*> outputPointers;
	outputPointers.reserve(outputs.size());
	for (auto& channel : outputs) { outputPointers.push_back(channel.data()); }

	plugin.process(inputPointers.data(), outputPointers.data(),
		static_cast<int>(inputPointers.size()), static_cast<int>(outputPointers.size()), frames);
}

} // namespace lmms::vst3

namespace lmms::vst3
{

/*! Engine audit E10: an MPE member channel's timbre (CC74) and pressure become parameter changes
 *  for the parameters the plug-in's IMidiMapping names; an unmapped controller is left alone. */
void Vst3HostTest::testMidiControllersReachMappedParameters()
{
	MappingController controller;
	MidiControllerMap map;
	map.build(&controller, 0);
	QCOMPARE(map.mappedCount(), 2);
	Steinberg::Vst::ParameterChanges changes(4);
	MidiEventIn timbre{static_cast<std::uint8_t>(MidiControlChange), 3, 74, 127, 0};
	MidiEventIn pressure{static_cast<std::uint8_t>(MidiChannelPressure), 3, 64, 0, 0};
	MidiEventIn elsewhere{static_cast<std::uint8_t>(MidiControlChange), 2, 74, 127, 0};
	QVERIFY(map.apply(timbre, &changes));
	QVERIFY(map.apply(pressure, &changes));
	QVERIFY(!map.apply(elsewhere, &changes));
	QCOMPARE(changes.getParameterCount(), 2);
	Steinberg::int32 offset = 0;
	Steinberg::Vst::ParamValue value = 0.0;
	changes.getParameterData(0)->getPoint(0, offset, value);
	QCOMPARE(changes.getParameterData(0)->getParameterId(), Steinberg::Vst::ParamID{74});
	QCOMPARE(value, 1.0);
	changes.getParameterData(1)->getPoint(0, offset, value);
	QCOMPARE(changes.getParameterData(1)->getParameterId(), Steinberg::Vst::ParamID{Steinberg::Vst::kAfterTouch});
	QVERIFY(std::abs(value - 64.0 / 127.0) < 1e-9);
}

} // namespace lmms::vst3

QTEST_GUILESS_MAIN(lmms::vst3::Vst3HostTest)

#include "Vst3HostTest.moc"
