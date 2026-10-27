/*
 * InputChannelMeterTest.cpp - Q050-340: a level for every capture channel and every record-armed track's input
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

/*! Q050-340: a four-channel capture device feeds different levels to its inputs - the stereo bus mapped to two
 *  OTHER channels - and every channel reads its own level through the engine's real render path (AudioDevice::
 *  publishCaptured, then a period); a record-armed route reads the level of the input it selected, independent of
 *  monitoring, and silence when that input carries none; meter.inputs reports both with the route's track; the
 *  agent's and the interface's readers never empty each other; the measurement allocates nothing. */

#include <QtTest>

#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <vector>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InputMeter.h"
#include "InputReadings.h"
#include "MultiTrackRecorder.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

class FakeCaptureDevice : public AudioDevice
{
public:
	explicit FakeCaptureDevice(AudioEngine* engine) : AudioDevice(DEFAULT_CHANNELS, engine) {}
	void prepare(f_cnt_t frames, int left, int right) { prepareCapture(frames, left, right); }
	void publish(const std::vector<float>& block, int channels)
	{
		publishCaptured(block.data(), channels, static_cast<f_cnt_t>(block.size() / static_cast<std::size_t>(channels)));
	}

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

//! @a frames frames of four channels, channel c a square wave of peak @a levels[c].
std::vector<float> block(int frames, const std::array<float, 4>& levels)
{
	std::vector<float> data(static_cast<std::size_t>(frames) * 4);
	for (int f = 0; f < frames; ++f)
	{
		for (int c = 0; c < 4; ++c) { data[static_cast<std::size_t>(f * 4 + c)] = (f % 2 == 0 ? 1.f : -1.f) * levels[static_cast<std::size_t>(c)]; }
	}
	return data;
}

//! One captured block through the real path: published by the device, then one engine period.
void capture(FakeCaptureDevice& device, const std::array<float, 4>& levels)
{
	device.publish(block(64, levels), 4);
	Engine::audioEngine()->renderNextPeriod();
}

QJsonArray channelsOf(const QJsonObject& state) { return state.value(QStringLiteral("channels")).toArray(); }

double dbOf(const QJsonArray& list, int index) { return list.at(index).toObject().value(QStringLiteral("peak_db")).toDouble(); }

QString trackId(int position) { return control::trackIdOf(Engine::getSong()->tracks().at(static_cast<std::size_t>(position))); }

} // namespace

class InputChannelMeterTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Four inputs at four levels, the stereo bus on inputs 3 and 4: every input reads its own level.
	void everyCaptureChannelReadsItsOwnLevel()
	{
		Engine::audioEngine()->renderNextPeriod();  // drain whatever the Dummy left
		FakeCaptureDevice device(Engine::audioEngine());
		device.prepare(64, 2, 3);
		run(QStringLiteral("meter.inputs"));  // start the agent's maxima from here
		capture(device, {0.5f, 0.125f, 0.25f, 1.f});
		const QJsonObject state = run(QStringLiteral("meter.inputs")).result;
		QCOMPARE(state.value(QStringLiteral("channel_count")).toInt(), 4);
		const QJsonArray channels = channelsOf(state);
		QCOMPARE(channels.size(), 4);
		QVERIFY(std::abs(dbOf(channels, 0) + 6.02) < 0.01);
		QVERIFY(std::abs(dbOf(channels, 1) + 18.06) < 0.01);
		QVERIFY(std::abs(dbOf(channels, 2) + 12.04) < 0.01);
		QVERIFY(std::abs(dbOf(channels, 3)) < 0.01);
		QCOMPARE(channels.at(3).toObject().value(QStringLiteral("clipped")).toBool(), true);
		QCOMPARE(channels.at(0).toObject().value(QStringLiteral("input_channel")).toInt(), 0);
		// The stereo bus's own meter (meter.input) still reads only its pair - inputs 3 and 4.
		const QJsonObject bus = run(QStringLiteral("meter.input")).result;
		QVERIFY(std::abs(bus.value(QStringLiteral("peak_left_db")).toDouble() + 12.04) < 0.01);
		// Read again with nothing captured since: every input falls to silence - no stale level.
		Engine::audioEngine()->renderNextPeriod();
		const QJsonArray after = channelsOf(run(QStringLiteral("meter.inputs")).result);
		for (int c = 0; c < after.size(); ++c) { QCOMPARE(dbOf(after, c), -120.0); }
	}

	//! The agent and the interface take their own maxima: one reading never empties the other's.
	void theTwoReadersAreIndependent()
	{
		FakeCaptureDevice device(Engine::audioEngine());
		device.prepare(64, 0, 1);
		run(QStringLiteral("meter.inputs"));
		inputreadings::take(inputmeter::Reader::Interface);
		capture(device, {0.5f, 0.5f, 0.5f, 0.5f});
		QVERIFY(std::abs(dbOf(channelsOf(run(QStringLiteral("meter.inputs")).result), 1) + 6.02) < 0.01);
		const inputreadings::Readings shown = inputreadings::take(inputmeter::Reader::Interface);
		QCOMPARE(shown.peaks.at(1), 0.5f);  // still there after the agent's read
		QCOMPARE(inputreadings::take(inputmeter::Reader::Interface).peaks.at(1), 0.f);
	}

	//! Each armed route reads the input it selected - two routes on two inputs, different levels - whatever the
	//! track's monitoring; an input that carries nothing reads silence; a disarmed route is not listed.
	void anArmedTrackFollowsItsSelectedInput()
	{
		QVERIFY(run(QStringLiteral("project.new")).ok);
		while (Engine::getSong()->tracks().size() < 2)
		{
			QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		}
		MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		recorder.setInputChannelCapacity(4);
		QTemporaryDir dir;
		QVERIFY(recorder.trackCount() >= 2);
		QVERIFY(recorder.track(0).arm(dir.filePath(QStringLiteral("a.wav")).toStdString(), 44100, 1));
		QVERIFY(recorder.track(1).arm(dir.filePath(QStringLiteral("b.wav")).toStdString(), 44100, 3));
		FakeCaptureDevice device(Engine::audioEngine());
		device.prepare(64, 0, 1);
		run(QStringLiteral("meter.inputs"));
		capture(device, {0.9f, 0.25f, 0.9f, 0.5f});
		QJsonArray armed = run(QStringLiteral("meter.inputs")).result.value(QStringLiteral("armed")).toArray();
		QCOMPARE(armed.size(), 2);
		const QJsonObject first = armed.at(0).toObject();
		const QJsonObject second = armed.at(1).toObject();
		QCOMPARE(first.value(QStringLiteral("route")).toInt(), 0);
		QCOMPARE(first.value(QStringLiteral("track")).toString(), trackId(0));
		QCOMPARE(first.value(QStringLiteral("input_channel")).toInt(), 1);
		QVERIFY(std::abs(first.value(QStringLiteral("peak_db")).toDouble() + 12.04) < 0.01);
		QCOMPARE(second.value(QStringLiteral("track")).toString(), trackId(1));
		QCOMPARE(second.value(QStringLiteral("input_channel")).toInt(), 3);
		QVERIFY(std::abs(second.value(QStringLiteral("peak_db")).toDouble() + 6.02) < 0.01);
		// Silence on input 2 only: route 0 reads silence, route 1 keeps its level.
		capture(device, {0.9f, 0.f, 0.9f, 0.5f});
		armed = run(QStringLiteral("meter.inputs")).result.value(QStringLiteral("armed")).toArray();
		QCOMPARE(armed.at(0).toObject().value(QStringLiteral("peak_db")).toDouble(), -120.0);
		QVERIFY(std::abs(armed.at(1).toObject().value(QStringLiteral("peak_db")).toDouble() + 6.02) < 0.01);
		recorder.track(0).disarm();
		capture(device, {0.9f, 0.25f, 0.9f, 0.5f});
		armed = run(QStringLiteral("meter.inputs")).result.value(QStringLiteral("armed")).toArray();
		QCOMPARE(armed.size(), 1);
		QCOMPARE(armed.at(0).toObject().value(QStringLiteral("route")).toInt(), 1);
		recorder.track(1).disarm();
	}

	//! The audio-thread half raises fixed atomics: measuring a wide period and the stereo bus allocates nothing.
	void measuringAllocatesNothing()
	{
		std::vector<float> wide(static_cast<std::size_t>(256) * inputmeter::MaxChannels, 0.25f);
		std::vector<SampleFrame> stereo(256, SampleFrame(0.5f, 0.5f));
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int p = 0; p < 100; ++p)
		{
			inputmeter::measureChannels(wide.data(), inputmeter::MaxChannels, 256);
			inputmeter::measureStereoChannels(stereo.data(), 256);
		}
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(inputmeter::channelCount(), 2);
	}
};

QTEST_MAIN(InputChannelMeterTest)
#include "InputChannelMeterTest.moc"
