/*
 * ClapHostTest.cpp - integration tests for the in-process CLAP host
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

#include "ClapHost.h"

#ifndef CLAP_TEST_PLUGIN_PATH
#define CLAP_TEST_PLUGIN_PATH ""
#endif

namespace lmms::clap
{

namespace
{
constexpr double TestSampleRate = 48000.0;
constexpr int TestBlockSize = 512;
constexpr int TestFrames = 256;

//! Plug-in under test: tests/data/clap-test-plugin/clap-test-gain.c, built
//! from the pinned CLAP headers. Two parameters: a continuous gain (id 1,
//! plain value is the linear gain factor) and a stepped bypass switch (id 2).
constexpr const char* TestPluginId = "org.lmms.test.clap-gain";
constexpr std::uint32_t GainParamId = 1;
constexpr std::uint32_t BypassParamId = 2;

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

class ClapHostTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void testModuleListing();
	void testIdentity();
	void testParameters();
	void testParameterSetGet();
	void testStateRoundTrip();
	void testAudioGain();
	void testAudioSilenceWhenGainIsZero();
	void testBypass();
	void testChannelPartition();

private:
	void process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
		std::vector<std::vector<float>>& outputs, int frames);

	HostedPlugin m_plugin;
};

void ClapHostTest::initTestCase()
{
	const QString path = QStringLiteral(CLAP_TEST_PLUGIN_PATH);
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		QSKIP("no CLAP test plug-in available (build tests/data/clap-test-plugin "
			"from the pinned CLAP headers and configure with -DLMMS_CLAP_PATH=<checkout>)");
	}
	QString error;
	QVERIFY2(m_plugin.load(path, QString::fromLatin1(TestPluginId), &error), qPrintable(error));
	QVERIFY(m_plugin.isLoaded());
	QString prepareError;
	QVERIFY2(m_plugin.prepare(TestSampleRate, TestBlockSize, &prepareError), qPrintable(prepareError));
}

void ClapHostTest::testModuleListing()
{
	const QString path = QStringLiteral(CLAP_TEST_PLUGIN_PATH);
	QString error;
	const auto classes = listClasses(path, &error);
	QVERIFY2(!classes.empty(), qPrintable(error));
	QCOMPARE(classes.size(), std::size_t{1});
	QCOMPARE(classes[0].id, QString::fromLatin1(TestPluginId));
	QCOMPARE(classes[0].name, QStringLiteral("LMMS CLAP Test Gain"));
	QCOMPARE(classes[0].isInstrument, false);
	QVERIFY(!classes[0].vendor.isEmpty());
}

void ClapHostTest::testIdentity()
{
	QCOMPARE(m_plugin.className(), QStringLiteral("LMMS CLAP Test Gain"));
	QCOMPARE(m_plugin.vendor(), QStringLiteral("LMMS contributors"));
	QCOMPARE(m_plugin.isInstrument(), false);
	QCOMPARE(m_plugin.latency(), std::uint32_t{0});

	const auto& layout = m_plugin.busLayout();
	QCOMPARE(layout.inputs, 2);
	QCOMPARE(layout.outputs, 2);
	QCOMPARE(layout.hasSideChain, false);
	QCOMPARE(layout.inputPortChannels.size(), std::size_t{1});
	QCOMPARE(layout.outputPortChannels.size(), std::size_t{1});
	QCOMPARE(layout.inputPortChannels[0], 2);
	QCOMPARE(layout.outputPortChannels[0], 2);
}

void ClapHostTest::testParameters()
{
	const auto& parameters = m_plugin.parameters();
	QCOMPARE(parameters.size(), std::size_t{2});

	QCOMPARE(parameters[0].id, GainParamId);
	QCOMPARE(parameters[0].title, QStringLiteral("Gain"));
	QCOMPARE(parameters[0].stepped, false);
	QCOMPARE(parameters[0].minValue, 0.0);
	QCOMPARE(parameters[0].maxValue, 1.0);
	QCOMPARE(parameters[0].defaultValue, 1.0);

	QCOMPARE(parameters[1].id, BypassParamId);
	QCOMPARE(parameters[1].title, QStringLiteral("Bypass"));
	QCOMPARE(parameters[1].stepped, true);
	QCOMPARE(parameters[1].defaultValue, 0.0);

	// ids must be unique and indexable without allocation
	for (std::size_t i = 0; i < parameters.size(); ++i)
	{
		QCOMPARE(m_plugin.paramIndex(parameters[i].id), static_cast<int>(i));
	}
	QCOMPARE(m_plugin.paramIndex(0xdeadbeefu), -1);

	// the plug-in's own value-to-text conversion is used
	QCOMPARE(m_plugin.paramDisplayValue(GainParamId, 0.5), QStringLiteral("0.50"));
}

void ClapHostTest::testParameterSetGet()
{
	m_plugin.setParamPlain(GainParamId, 0.25);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.25);
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.25f);

	// out of range values are clamped, not wrapped
	m_plugin.setParamPlain(GainParamId, 2.0);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 1.0);
	m_plugin.setParamPlain(GainParamId, -1.0);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.0);

	// normalized setters map through the plain range
	m_plugin.setParamNormalized(GainParamId, 0.5f);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.5);

	// unknown ids are ignored
	m_plugin.setParamPlain(0xdeadbeefu, 0.5);
	QCOMPARE(m_plugin.paramPlain(0xdeadbeefu), 0.0);

	m_plugin.setParamPlain(GainParamId, 1.0);
}

void ClapHostTest::testStateRoundTrip()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamPlain(GainParamId, 0.3);
	// Parameter changes reach the processor through process(); a host saves the
	// plug-in state after the value has been delivered.
	process(m_plugin, inputs, outputs, TestFrames);

	QByteArray state;
	QVERIFY(m_plugin.saveState(&state));
	QVERIFY(!state.isEmpty());
	qInfo("state round trip: saved %d bytes", int(state.size()));

	m_plugin.setParamPlain(GainParamId, 0.9);
	QCOMPARE(m_plugin.paramPlain(GainParamId), 0.9);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.45) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	QVERIFY(m_plugin.loadState(state));
	QVERIFY(std::abs(m_plugin.paramPlain(GainParamId) - 0.3) < 1e-6);

	// no process() in between, so the plug-in counter is unchanged: the state
	// must be byte identical when saved again
	QByteArray stateAgain;
	QVERIFY(m_plugin.saveState(&stateAgain));
	QCOMPARE(stateAgain, state);

	// the restored state must reach the processor, not just the host cache
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY2(std::abs(rms(outputs[0]) - 0.15) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void ClapHostTest::testAudioGain()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	// the plain gain value is the linear gain factor
	m_plugin.setParamPlain(GainParamId, 0.5);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED input RMS %.6f -> gain 0.5 output RMS %.6f",
		rms(inputs[0]), rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
	QVERIFY2(std::abs(rms(outputs[1]) - 0.25) < 1e-6, qPrintable(QString::number(rms(outputs[1]))));

	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	std::fill(outputs[1].begin(), outputs[1].end(), 0.0f);
	m_plugin.setParamPlain(GainParamId, 1.0);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED input RMS %.6f -> gain 1.0 output RMS %.6f",
		rms(inputs[0]), rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.5) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));
}

void ClapHostTest::testAudioSilenceWhenGainIsZero()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	m_plugin.setParamPlain(GainParamId, 0.0);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED input RMS %.6f -> gain 0.0 output RMS %.6f",
		rms(inputs[0]), rms(outputs[0]));
	QCOMPARE(rms(outputs[0]), 0.0);
	QCOMPARE(rms(outputs[1]), 0.0);

	m_plugin.setParamPlain(GainParamId, 1.0);
}

void ClapHostTest::testBypass()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.5f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));

	// the stepped bypass parameter must override the gain
	m_plugin.setParamPlain(GainParamId, 0.1);
	m_plugin.setParamPlain(BypassParamId, 1.0);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED bypass=1 with gain 0.1 -> output RMS %.6f", rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.5) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	m_plugin.setParamPlain(BypassParamId, 0.0);
	std::fill(outputs[0].begin(), outputs[0].end(), 0.0f);
	process(m_plugin, inputs, outputs, TestFrames);
	qInfo("MEASURED bypass=0 with gain 0.1 -> output RMS %.6f", rms(outputs[0]));
	QVERIFY2(std::abs(rms(outputs[0]) - 0.05) < 1e-6, qPrintable(QString::number(rms(outputs[0]))));

	m_plugin.setParamPlain(GainParamId, 1.0);
}

void ClapHostTest::testChannelPartition()
{
	const auto& layout = m_plugin.busLayout();
	std::vector<std::vector<float>> inputs(layout.inputs, std::vector<float>(TestFrames, 0.0f));
	std::vector<std::vector<float>> outputs(layout.outputs, std::vector<float>(TestFrames, 0.0f));
	std::fill(inputs[0].begin(), inputs[0].end(), 0.5f);
	std::fill(inputs[1].begin(), inputs[1].end(), 0.25f);

	m_plugin.setParamPlain(GainParamId, 1.0);
	process(m_plugin, inputs, outputs, TestFrames);
	QVERIFY(std::abs(rms(outputs[0]) - 0.5) < 1e-6);
	QVERIFY(std::abs(rms(outputs[1]) - 0.25) < 1e-6);
}

void ClapHostTest::process(HostedPlugin& plugin, const std::vector<std::vector<float>>& inputs,
	std::vector<std::vector<float>>& outputs, int frames)
{
	std::vector<const float*> inputPointers;
	inputPointers.reserve(inputs.size());
	for (const auto& channel : inputs) { inputPointers.push_back(channel.data()); }

	std::vector<float*> outputPointers;
	outputPointers.reserve(outputs.size());
	for (auto& channel : outputs) { outputPointers.push_back(channel.data()); }

	QVERIFY(plugin.process(inputPointers.data(), outputPointers.data(),
		static_cast<int>(inputPointers.size()), static_cast<int>(outputPointers.size()), frames));
}

} // namespace lmms::clap

QTEST_GUILESS_MAIN(lmms::clap::ClapHostTest)

#include "ClapHostTest.moc"
