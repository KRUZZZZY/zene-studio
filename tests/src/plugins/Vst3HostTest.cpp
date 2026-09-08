/*
 * Vst3HostTest.cpp - integration tests for the in-process VST3 host
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

#include <cmath>
#include <vector>

#include "Vst3Host.h"

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

auto rms(const std::vector<float>& data) -> double
{
	double sum = 0.0;
	for (const auto value : data)
	{
		sum += static_cast<double>(value) * static_cast<double>(value);
	}
	return data.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(data.size()));
}
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
		QSKIP("no VST3 test plug-in available (build the SDK's 'again' sample and "
			"configure with -DLMMS_VST3_TEST_PLUGIN=<bundle>)");
	}
	QString error;
	QVERIFY2(m_plugin.load(path, QString{}, &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QString prepareError;
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &prepareError), qPrintable(prepareError));
}

void Vst3HostTest::testIdentity()
{
	QCOMPARE(m_plugin.className(), QStringLiteral("AGain"));
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
	QVERIFY(parameters.size() >= 3);
	QCOMPARE(parameters[0].id, std::uint32_t{0});
	QCOMPARE(parameters[0].title, QStringLiteral("Gain"));
	QCOMPARE(parameters[0].units, QStringLiteral("dB"));
	QCOMPARE(parameters[0].stepped, false);

	// ids must be unique and indexable without allocation
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		QCOMPARE(m_plugin.paramIndex(parameters[i].id), static_cast<int>(i));
	}
	QCOMPARE(m_plugin.paramIndex(0xdeadbeefu), -1);
}

void Vst3HostTest::testParameterSetGet()
{
	m_plugin.setParamNormalized(0, 0.25f);
	QCOMPARE(m_plugin.paramNormalized(0), 0.25f);

	// out of range values are clamped, not wrapped
	m_plugin.setParamNormalized(0, 2.0f);
	QCOMPARE(m_plugin.paramNormalized(0), 1.0f);
	m_plugin.setParamNormalized(0, -1.0f);
	QCOMPARE(m_plugin.paramNormalized(0), 0.0f);

	// unknown ids are ignored
	m_plugin.setParamNormalized(0xdeadbeefu, 0.5f);
	QCOMPARE(m_plugin.paramNormalized(0xdeadbeefu), 0.0f);
}

void Vst3HostTest::testStateRoundTrip()
{
	m_plugin.setParamNormalized(0, 0.3f);
	QByteArray componentState;
	QByteArray controllerState;
	QVERIFY(m_plugin.saveState(&componentState, &controllerState));
	QVERIFY(!componentState.isEmpty());

	m_plugin.setParamNormalized(0, 0.9f);
	QCOMPARE(m_plugin.paramNormalized(0), 0.9f);

	QVERIFY(m_plugin.loadState(componentState, controllerState));
	QVERIFY(std::abs(m_plugin.paramNormalized(0) - 0.3f) < 1e-6f);

	// the restored state must reach the processor, not just the controller
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY(std::abs(rms(outputs[0]) - 0.15) < 1e-6);
}

void Vst3HostTest::testAudioGain()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	// AGain: the normalized value is the linear gain factor
	m_plugin.setParamNormalized(0, 0.5f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
	QVERIFY2(std::abs(rms(outputs[1]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[1]))));

	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	m_plugin.setParamNormalized(0, 1.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.5) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void Vst3HostTest::testAudioSilenceWhenGainIsZero()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamNormalized(0, 0.0f);
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

	m_plugin.setParamNormalized(0, 1.0f);
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

QTEST_GUILESS_MAIN(lmms::vst3::Vst3HostTest)

#include "Vst3HostTest.moc"
