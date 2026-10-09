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
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "../core/AllocationProbe.h"
#include "Midi.h"
#include "Vst3ComponentHandler.h"
#include "Vst3Host.h"
#include "Vst3MidiEvent.h"
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

//! The queue @a changes holds for parameter @a id, or nullptr.
auto queueFor(Steinberg::Vst::IParameterChanges& changes, Steinberg::Vst::ParamID id) -> Steinberg::Vst::IParamValueQueue*
{
	for (Steinberg::int32 i = 0; i < changes.getParameterCount(); ++i)
	{
		auto* queue = changes.getParameterData(i);
		if (queue != nullptr && queue->getParameterId() == id) { return queue; }
	}
	return nullptr;
}

//! Point @a index of @a queue as (sample offset, value); (-1, -1) when there is none.
auto pointOf(Steinberg::Vst::IParamValueQueue* queue, Steinberg::int32 index) -> std::pair<Steinberg::int32, double>
{
	Steinberg::int32 offset = -1;
	Steinberg::Vst::ParamValue value = -1.0;
	if (queue == nullptr || queue->getPoint(index, offset, value) != Steinberg::kResultTrue) { return {-1, -1.0}; }
	return {offset, value};
}

auto addPointTo(Steinberg::Vst::ParameterChanges& changes, Steinberg::Vst::ParamID id, Steinberg::int32 offset, double value)
{
	Steinberg::int32 index = 0;
	changes.addParameterData(id, index)->addPoint(offset, value, index);
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
	void testMidiControllersReachMappedParameters();
	void testMappedControllersKeepTheirFrame();
	void testParameterChangesAreSlicedPerChunk();
	void testAFullParameterQueueKeepsTheNewestValue();
	void testComponentHandlerAnswersInterfaceQueries();
	void testNonFiniteParameterValuesAreRefused();
	void testReprepareGateQuiescesAllActiveAudioCalls();

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

/*! 0.5.0 W4 (review lane20 luna BUG 4): a mapped controller, channel pressure and pitch bend
 *  reach the plug-in's parameter queue at their own frame, not at the request's first frame; a
 *  frame past the request is bounded to its last frame (a point must lie inside the block), and
 *  the mapped controllers are not sent as events too. */
void Vst3HostTest::testMappedControllersKeepTheirFrame()
{
	MappingController controller;
	MidiControllerMap map;
	map.build(&controller, 0);
	constexpr int Frames = 1024;
	auto queue = std::make_unique<MidiQueue>();
	QVERIFY(queue->push({static_cast<std::uint8_t>(MidiChannelPressure), 3, 32, 0, 700}));
	QVERIFY(queue->push({static_cast<std::uint8_t>(MidiControlChange), 3, 74, 127, 300}));
	QVERIFY(queue->push({static_cast<std::uint8_t>(MidiControlChange), 3, 74, 0, Frames + 50}));
	std::vector<MidiEventIn> scratch(kMaxMidiEventsPerBlock);
	Steinberg::Vst::EventList events{kMaxMidiEventsPerBlock};
	Steinberg::Vst::ParameterChanges changes(4);
	QCOMPARE(drainMidiIntoEventList(*queue, scratch, events, 0, Frames, &map, &changes), 3);
	QCOMPARE(events.getEventCount(), 0);
	auto* timbre = queueFor(changes, 74);
	QVERIFY(timbre != nullptr);
	QCOMPARE(timbre->getPointCount(), 2);
	QCOMPARE(pointOf(timbre, 0), std::make_pair(Steinberg::int32{300}, 1.0));
	QCOMPARE(pointOf(timbre, 1), std::make_pair(Steinberg::int32{Frames - 1}, 0.0));
	auto* pressure = queueFor(changes, Steinberg::Vst::kAfterTouch);
	QVERIFY(pressure != nullptr);
	QCOMPARE(pointOf(pressure, 0).first, Steinberg::int32{700});
	QVERIFY(std::abs(pointOf(pressure, 0).second - 32.0 / 127.0) < 1e-9);
}

/*! 0.5.0 W4: a request longer than the prepared block is processed in chunks, and each chunk gets
 *  only the points that fall in it, rebased to its start - so no point lands past numSamples and
 *  none is delivered twice. A parameter with no point in a chunk is not in that chunk's list. */
void Vst3HostTest::testParameterChangesAreSlicedPerChunk()
{
	Steinberg::Vst::ParameterChanges request(4);
	addPointTo(request, 1, 0, 0.25);    // a host change: the start of the request
	addPointTo(request, 1, 600, 0.75);  // a mapped controller in the second chunk
	addPointTo(request, 2, 511, 0.5);   // the last frame of the first chunk
	Steinberg::Vst::ParameterChanges chunk(4);
	sliceParameterChanges(request, chunk, 0, 512);
	QCOMPARE(chunk.getParameterCount(), 2);
	QCOMPARE(queueFor(chunk, 1)->getPointCount(), 1);
	QCOMPARE(pointOf(queueFor(chunk, 1), 0), std::make_pair(Steinberg::int32{0}, 0.25));
	QCOMPARE(pointOf(queueFor(chunk, 2), 0), std::make_pair(Steinberg::int32{511}, 0.5));
	chunk.clearQueue();
	sliceParameterChanges(request, chunk, 512, 512);
	QCOMPARE(chunk.getParameterCount(), 1);
	QVERIFY(queueFor(chunk, 2) == nullptr);
	QCOMPARE(queueFor(chunk, 1)->getPointCount(), 1);
	QCOMPARE(pointOf(queueFor(chunk, 1), 0), std::make_pair(Steinberg::int32{600 - 512}, 0.75));
	chunk.clearQueue();
	sliceParameterChanges(request, chunk, 1024, 37);  // the tail chunk holds nothing
	QCOMPARE(chunk.getParameterCount(), 0);
}

/*! 0.5.0 W4: a burst of controller points never grows a parameter's queue past the points the SDK
 *  reserves (no allocation on the audio thread); the newest value is kept, on the last kept point. */
void Vst3HostTest::testAFullParameterQueueKeepsTheNewestValue()
{
	Steinberg::Vst::ParameterChanges changes(1);
	Steinberg::int32 index = 0;
	auto* queue = changes.addParameterData(7, index);
	for (int i = 0; i < kMaxPointsPerParameter + 3; ++i) { addBoundedPoint(queue, i * 10, i / 10.0); }
	QCOMPARE(queue->getPointCount(), kMaxPointsPerParameter);
	QCOMPARE(pointOf(queue, 0), std::make_pair(Steinberg::int32{0}, 0.0));
	const auto last = pointOf(queue, kMaxPointsPerParameter - 1);
	QCOMPARE(last.first, Steinberg::int32{(kMaxPointsPerParameter - 1) * 10});
	QCOMPARE(last.second, (kMaxPointsPerParameter + 2) / 10.0);
}

/*! 0.5.0 W5: a plug-in that queries the handler it was given for its own interfaces gets them.
 *  queryInterface() returned kNoInterface for EVERY iid, including the two this object
 *  implements, so a plug-in could not confirm it has a component handler at all. */
void Vst3HostTest::testComponentHandlerAnswersInterfaceQueries()
{
	std::atomic<float> values[2] = {0.f, 0.f};
	const std::vector<std::pair<std::uint32_t, std::size_t>> indexById = {{1, 1}};
	auto* handler = new Vst3ComponentHandler{values, &indexById};

	void* queried = nullptr;
	QCOMPARE(handler->queryInterface(Steinberg::Vst::IComponentHandler::iid, &queried),
		Steinberg::kResultOk);
	QVERIFY(queried != nullptr);
	QCOMPARE(queried, static_cast<void*>(static_cast<Steinberg::Vst::IComponentHandler*>(handler)));
	handler->release();

	queried = nullptr;
	QCOMPARE(handler->queryInterface(Steinberg::FUnknown::iid, &queried), Steinberg::kResultOk);
	QVERIFY(queried != nullptr);
	handler->release();

	// an interface this object does not implement still reports kNoInterface,
	// and never leaves a dangling pointer in the caller's out-parameter
	queried = reinterpret_cast<void*>(0x1);
	QCOMPARE(handler->queryInterface(Steinberg::Vst::IMidiMapping::iid, &queried),
		Steinberg::kNoInterface);
	QVERIFY(queried == nullptr);

	QCOMPARE(handler->queryInterface(Steinberg::Vst::IComponentHandler::iid, nullptr),
		Steinberg::kInvalidArgument);

	// the reference count the successful queries took is still owned here
	handler->release();
}

/*! Luna cluster 3625: a plug-in or a caller that reports a value with no
 *  meaning - NaN, or a double that overflows float - cannot have it stored in
 *  the parameter array the audio thread reads: std::clamp() passes NaN through
 *  (both of its comparisons are false for it), so nothing rejected it. */
void Vst3HostTest::testNonFiniteParameterValuesAreRefused()
{
	// the host's own setter: the last good value stays
	m_plugin.setParamNormalized(GainParamId, 0.5f);
	m_plugin.setParamNormalized(GainParamId, std::numeric_limits<float>::quiet_NaN());
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.5f);
	m_plugin.setParamNormalized(GainParamId, std::numeric_limits<float>::infinity());
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.5f);
	m_plugin.setParamNormalized(GainParamId, -std::numeric_limits<float>::infinity());
	QCOMPARE(m_plugin.paramNormalized(GainParamId), 0.5f);

	// a plug-in's own edits, through the handler it was given
	std::atomic<float> values[2] = {0.f, 0.25f};
	const std::vector<std::pair<std::uint32_t, std::size_t>> indexById = {{0, 0}, {1, 1}};
	auto* handler = new Vst3ComponentHandler{values, &indexById};

	QCOMPARE(handler->performEdit(1, 2.5), Steinberg::kResultOk);
	QCOMPARE(values[1].load(), 1.0f);
	QCOMPARE(handler->performEdit(1, -1.0), Steinberg::kResultOk);
	QCOMPARE(values[1].load(), 0.0f);
	QCOMPARE(handler->performEdit(1, std::numeric_limits<double>::quiet_NaN()),
		Steinberg::kResultOk);
	QCOMPARE(values[1].load(), 0.0f);
	QCOMPARE(handler->performEdit(1, std::numeric_limits<double>::infinity()),
		Steinberg::kResultOk);
	QCOMPARE(values[1].load(), 0.0f);
	QCOMPARE(handler->performEdit(1, 1.0e300), Steinberg::kResultOk); // overflows float
	QCOMPARE(values[1].load(), 1.0f);
	handler->release();
}

void Vst3HostTest::testReprepareGateQuiescesAllActiveAudioCalls()
{
	AudioProcessGate gate;
	QVERIFY(gate.tryEnterAudioProcess());
	QVERIFY(gate.tryEnterAudioProcess());

	std::atomic<bool> started{false};
	std::atomic<bool> finished{false};
	std::thread reconfigure([&] {
		started.store(true, std::memory_order_release);
		gate.beginReconfigure();
		finished.store(true, std::memory_order_release);
		gate.endReconfigure();
	});
	while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }
	while (!gate.reconfigurationPending() && !finished.load(std::memory_order_acquire))
	{
		std::this_thread::yield();
	}
	const bool pending = gate.reconfigurationPending();
	const bool waitedForBoth = !finished.load(std::memory_order_acquire);
	const bool refusedDuringReconfigure = !gate.tryEnterAudioProcess();
	if (!refusedDuringReconfigure) { gate.leaveAudioProcess(); }
	gate.leaveAudioProcess();
	const bool waitedForLastCall = !finished.load(std::memory_order_acquire);
	gate.leaveAudioProcess();
	reconfigure.join();

	QVERIFY(pending);
	QVERIFY(waitedForBoth);
	QVERIFY(refusedDuringReconfigure);
	QVERIFY(waitedForLastCall);
	QVERIFY(finished.load(std::memory_order_acquire));

	lmms::test::resetAllocationCount();
	lmms::test::tlCountAllocations = true;
	const bool admitted = gate.tryEnterAudioProcess();
	if (admitted) { gate.leaveAudioProcess(); }
	lmms::test::tlCountAllocations = false;
	QVERIFY(admitted);
	QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
}

} // namespace lmms::vst3

QTEST_GUILESS_MAIN(lmms::vst3::Vst3HostTest)

#include "Vst3HostTest.moc"
