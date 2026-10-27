/*
 * HardwareIoTest.cpp - hardware outputs, cue mixes and hardware inserts (0.5.0 F1)
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

/*! 0.5.0 F1 (MX-040, MX-041, MX-036, MX-061). Channel 1 carries a constant 0.25 and channel 2 a
 *  varying signal, both at fader 0.5 into the master. Driven through the commands, as the menus drive
 *  them, and read back from the engine's hardware bus (HardwareOutputBus):
 *  - a routed channel plays on its pair after its fader; the master moves to its own pair or nowhere;
 *    a mono output takes the average; with nothing routed the bus is inactive (the old device path);
 *  - a cue mix sums its sends at its level, pre- or post-fader, follows solo and listen, and with
 *    listen_to cue the main output keeps the mix;
 *  - the routed render allocates nothing;
 *  - a hardware insert's ping, through a loop from its send pair back to its return, measures the round
 *    trip exactly, and delay compensation then counts it;
 *  - everything survives a save and load, and a project that uses none of it saves none of it;
 *  - destinations parse ("3/4", "3", a bus name, "none") and refuse what is not one;
 *  - audio.io_setup names the interface's channels and buses, a route can use a bus name, and one undo
 *    puts the setup back. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <cmath>
#include <span>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioIoSetup.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ConfigManager.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "HardwareInsert.h"
#include "HardwareIo.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "MixerListen.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index)
{
	return control::channelIdOf(Engine::mixer()->mixerChannel(index));
}

HardwareOutputBus& bus()
{
	return Engine::audioEngine()->hardwareOutputs();
}

float varying(int f)
{
	return static_cast<float>(((f * 131) % 2048) - 1024) / 2048.f;
}

//! One mixer period (channel 1 constant 0.25, channel 2 varying), then the bus flipped so it can be read.
std::vector<SampleFrame> period(Mixer* mixer)
{
	// Serial model prepass, as AudioEngine runs it once per period before mixing (consumes setter ramps).
	{
		auto changeGuard = Engine::audioEngine()->requestChangesGuard();
		Engine::audioEngine()->prepareAutomatableModelBuffers();
	}
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	std::vector<SampleFrame> one(fpp, SampleFrame(0.25f, 0.25f));
	std::vector<SampleFrame> two(fpp);
	for (f_cnt_t f = 0; f < fpp; ++f) { two[f] = SampleFrame(varying(f), -varying(f)); }
	SampleFrame* a[1] = {one.data()};
	SampleFrame* b[1] = {two.data()};
	bus().swap();  // a clean write side
	mixer->mixToChannel(AudioBus{a, 1, fpp}, 1);
	mixer->mixToChannel(AudioBus{b, 1, fpp}, 2);
	mixer->prepareMasterMix();
	zeroSampleFrames(out.data(), fpp);
	mixer->masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();
	bus().swap();  // the period just mixed is the one the device would be fed
	return out;
}

//! Whether device channel @a channel carries @a level on every frame (with @a out the period's master).
bool channelAt(const std::vector<SampleFrame>& out, int channel, float level)
{
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
	{
		if (std::abs(bus().sample(channel, f, out[f]) - level) > 1e-5f) { return false; }
	}
	return true;
}

bool ok(const QString& id, const QJsonObject& args)
{
	const ControlResult result = run(id, args);
	if (!result.ok) { qWarning("%s: %s", qPrintable(id), qPrintable(result.errorMessage)); }
	return result.ok;
}

} // namespace

class HardwareIoTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 2) { mixer->createChannel(); }
		mixer->mixerChannel(1)->m_volumeModel.setValue(0.5f);
		mixer->mixerChannel(2)->m_volumeModel.setValue(0.5f);
		ControlRegistry::setReady(true);
		for (int i = 0; i < 3; ++i) { period(mixer); }  // let the fader ramps settle
	}

	void cleanupTestCase()
	{
		mixerlisten::clear();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aRoutedChannelPlaysOnItsPairAfterItsFader()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(!period(mixer).empty());
		QVERIFY2(!bus().active(), "with nothing routed the device takes the old path");
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("3/4")}}));
		const std::vector<SampleFrame> out = period(mixer);
		QVERIFY(bus().active());
		QVERIFY(channelAt(out, 2, 0.125f));  // 0.25 through the 0.5 fader
		QVERIFY(channelAt(out, 3, 0.125f));
		for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
		{
			QCOMPARE(bus().sample(0, f, out[f]), out[f][0]);  // the master stays on 1/2, the send to it untouched
		}
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("7")}}));
		QVERIFY(channelAt(period(mixer), 6, 0.125f));  // a mono output: the average of both sides
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("default")}}));
		period(mixer);
		QVERIFY(!bus().active());
	}

	void theMasterMovesToItsOwnPair()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("5/6")}}));
		std::vector<SampleFrame> out = period(mixer);
		for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
		{
			QCOMPARE(bus().sample(0, f, out[f]), 0.f);
			QCOMPARE(bus().sample(4, f, out[f]), out[f][0]);
		}
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("none")}}));
		period(mixer);
		QCOMPARE(bus().channelsInUse(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		period(mixer);
		QVERIFY2(!bus().active(), "two undos put the master back on 1/2");
	}

	void outOfRangeFrameReadsAsSilence()
	{
		// The bus holds 256 frames a period. Channel 0 carries 0.5 and channel 1 carries 0.25, so frame 256
		// of channel 0 - past its plane - reads channel 1's first sample unless the frame is bounded.
		HardwareOutputBus outputs;
		outputs.init(256);
		const std::vector<SampleFrame> half(256, SampleFrame(0.5f, 0.5f));
		const std::vector<SampleFrame> quarter(256, SampleFrame(0.25f, 0.25f));
		outputs.add(0, 1, half.data(), 256);
		outputs.add(1, 1, quarter.data(), 256);
		outputs.setMasterPlacement({-1, 2});  // the master is off the outputs
		outputs.swap();  // the period just written is the one the device reads
		const SampleFrame master(0.f, 0.f);
		QVERIFY(outputs.sample(0, 255, master) == 0.5f);
		QVERIFY(outputs.monoSample(255, master) == 0.5f);
		QVERIFY2(outputs.sample(0, 256, master) == 0.f, "a frame past the period is silence");
		QVERIFY2(outputs.monoSample(256, master) == 0.f, "a frame past the period is silence");
	}

	void cueMasterFieldsRequireACue()
	{
		const ControlResult result = run(QStringLiteral("mixer.cue_master"), {{QStringLiteral("name"), QStringLiteral("Phones")}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	void aCueMixSumsItsSendsAtItsLevel()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")}, {QStringLiteral("output"), QStringLiteral("3/4")},
			{QStringLiteral("level"), 0.5}}));
		QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("cue"), QStringLiteral("A")},
			{QStringLiteral("level"), 1.0}}));
		QVERIFY(channelAt(period(mixer), 2, 0.125f));  // pre-fader 0.25, at the cue's 0.5
		QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("cue"), QStringLiteral("A")},
			{QStringLiteral("pre_fader"), false}, {QStringLiteral("pan"), -1.0}}));
		const std::vector<SampleFrame> out = period(mixer);
		QVERIFY(channelAt(out, 2, 0.0625f));  // after the 0.5 fader
		QVERIFY(channelAt(out, 3, 0.f));      // panned hard left
		QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("cue"), QStringLiteral("A")},
			{QStringLiteral("pre_fader"), true}, {QStringLiteral("pan"), 0.0}}));
	}

	void invalidCueNumbersUseClampedDefaultNames()
	{
		const int lastCue = hardwareio::MaxCues - 1;
		const hardwareio::CueMaster firstMaster = hardwareio::cueMaster(0);
		const hardwareio::CueMaster lastMaster = hardwareio::cueMaster(lastCue);
		hardwareio::setCueMaster(0, hardwareio::CueMaster{});
		hardwareio::setCueMaster(lastCue, hardwareio::CueMaster{});

		const QString belowRange = hardwareio::cueName(-1);
		const QString aboveRange = hardwareio::cueName(hardwareio::MaxCues);

		hardwareio::setCueMaster(0, firstMaster);
		hardwareio::setCueMaster(lastCue, lastMaster);
		QCOMPARE(belowRange, QStringLiteral("Cue A"));
		QCOMPARE(aboveRange, QStringLiteral("Cue D"));
	}

	void aCueFollowsSoloAndListen()
	{
		Mixer* mixer = Engine::mixer();
		mixer->mixerChannel(2)->m_soloModel.setValue(true);  // mutes channel 1, whose send feeds the cue
		QVERIFY(channelAt(period(mixer), 2, 0.f));
		mixer->mixerChannel(2)->m_soloModel.setValue(false);
		QVERIFY(channelAt(period(mixer), 2, 0.125f));

		mixerlisten::setListening(2, true);  // AFL on channel 2: the cue carries it, at the cue's level
		const std::vector<SampleFrame> listened = period(mixer);
		for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(listened.size()); ++f)
		{
			QVERIFY(std::abs(bus().sample(2, f, listened[f]) - varying(f) * 0.5f * 0.5f) < 1e-5f);
			QVERIFY(std::abs(listened[f][0] - varying(f) * 0.5f) < 1e-5f);  // listen_to main: the main output too
		}
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("listen_to"), QStringLiteral("cue")}}));
		mixerlisten::setListening(2, false);
		const std::vector<SampleFrame> mix = period(mixer);
		mixerlisten::setListening(2, true);
		const std::vector<SampleFrame> spared = period(mixer);
		for (std::size_t f = 0; f < mix.size(); ++f) { QCOMPARE(spared[f][0], mix[f][0]); }  // the main output keeps the mix
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")}, {QStringLiteral("level"), 0.0}}));
		const std::vector<SampleFrame> silentCue = period(mixer);
		bool mainCarriesListen = true;
		for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(silentCue.size()); ++f)
		{
			if (std::abs(silentCue[f][0] - varying(f) * 0.5f) >= 1e-5f) { mainCarriesListen = false; break; }
		}
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")}, {QStringLiteral("level"), 0.5}}));
		mixerlisten::clear();
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("listen_to"), QStringLiteral("main")}}));
		QVERIFY2(mainCarriesListen, "a silent following cue must leave the listened signal on the main output");
	}

	void theRoutedRenderAllocatesNothing()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("5/6")}}));
		mixerlisten::setListening(1, true);
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(fpp);
		mixer->prepareMasterMix();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		mixer->masterMix(out.data());
		bus().swap();
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
		mixerlisten::clear();
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("default")}}));
	}

	void aPingMeasuresTheLoopAndFeedsDelayCompensation()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		QCOMPARE(run(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), ch(1)}}).errorKind, ControlErrorKind::Requires);
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("3/4")}, {QStringLiteral("return"), QStringLiteral("1/2")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), ch(1)}}));
		HardwareInsert& insert = Engine::mixer()->mixerChannel(1)->m_hardwareIo.insert;
		// A cable from outputs 3/4 to inputs 1/2: what the device plays on 3/4 is captured for the next period.
		std::vector<SampleFrame> cable(fpp);
		for (int i = 0; i < 64 && insert.pingState() != HardwareInsert::PingState::Done; ++i)
		{
			const std::span<const SampleFrame> played = engine->renderNextPeriod();
			for (f_cnt_t f = 0; f < fpp; ++f) { cable[f] = SampleFrame(bus().sample(2, f, played[f]), bus().sample(3, f, played[f])); }
			engine->pushInputFrames(cable.data(), fpp);
		}
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Done));
		// One period of output double buffer, one of capture staging: the loop the Dummy device has too.
		QCOMPARE(insert.settings().measured, static_cast<int>(2 * fpp));
		QCOMPARE(insert.latencyFrames(), static_cast<int>(2 * fpp));
		engine->renderNextPeriod();
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), static_cast<int>(2 * fpp));
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("latency_frames"), 300}}));
		QCOMPARE(insert.latencyFrames(), 300);  // a typed figure wins over the measured one
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(insert.settings().measured, -1);  // the ping's undo: no figure, as before it
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!insert.enabled());
		engine->renderNextPeriod();
		QCOMPARE(Engine::mixer()->totalLatencyFrames(), 0);
	}

	void masterHardwareInsertLatencyIsIncludedInTotal()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("3/4")}, {QStringLiteral("return"), QStringLiteral("1/2")},
			{QStringLiteral("latency_frames"), 123}}));
		period(mixer);  // updateLatencyCompensation publishes the graph at the period boundary
		QCOMPARE(mixer->mixerChannel(0)->m_hardwareIo.insert.latencyFrames(), 123);
		QCOMPARE(mixer->totalLatencyFrames(), 123);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		period(mixer);
		QCOMPARE(mixer->totalLatencyFrames(), 0);
	}

	void aNonFiniteSavedMixDoesNotContaminateAudio()
	{
		QDomDocument doc;
		QDomElement channel = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channel);
		QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
		element.setAttribute(QStringLiteral("on"), 1);
		element.setAttribute(QStringLiteral("mix"), QStringLiteral("nan"));
		channel.appendChild(element);

		bool parsed = false;
		QVERIFY(std::isnan(element.attribute(QStringLiteral("mix")).toFloat(&parsed)));
		QVERIFY(parsed);

		HardwareInsert insert;
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		insert.load(channel, frames);
		std::vector<SampleFrame> output(static_cast<std::size_t>(frames), SampleFrame(0.25f, -0.25f));
		insert.process(output.data(), frames, *Engine::audioEngine());
		for (const SampleFrame& frame : output)
		{
			QVERIFY(std::isfinite(frame[0]));
			QVERIFY(std::isfinite(frame[1]));
		}
	}

	void malformedInsertLatencyUsesTheMeasuredFigure()
	{
		QDomDocument doc;
		QDomElement channel = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channel);
		QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
		element.setAttribute(QStringLiteral("on"), 1);
		element.setAttribute(QStringLiteral("latency"), QStringLiteral("abc"));
		element.setAttribute(QStringLiteral("measured"), 512);
		channel.appendChild(element);

		HardwareInsert insert;
		insert.load(channel, 64);
		QCOMPARE(insert.settings().latencyOverride, -1);
		QCOMPARE(insert.latencyFrames(), 512);
	}

	void savedInsertMeasurementAtAnotherSampleRateIsDiscarded()
	{
		AudioEngine* engine = Engine::audioEngine();
		const sample_rate_t currentRate = engine->outputSampleRate();
		const sample_rate_t savedRate = currentRate == 1 ? 2 : 1;
		QVERIFY(currentRate != savedRate);
		HardwareInsert source;
		HardwareInsert::Settings measured;
		measured.enabled = true;
		measured.measured = 512;
		source.setSettings(measured, 64);
		QDomDocument doc;
		QDomElement channel = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channel);
		source.save(doc, channel);
		QDomElement element = channel.firstChildElement(QStringLiteral("hardwareinsert"));
		QVERIFY(!element.isNull());
		QCOMPARE(element.attribute(QStringLiteral("measured_rate")).toUInt(), currentRate);
		element.setAttribute(QStringLiteral("measured_rate"), savedRate);

		HardwareInsert insert;
		insert.load(channel, 64);
		QCOMPARE(insert.settings().measured, -1);
		QCOMPARE(insert.latencyFrames(), 0);
	}

	void liveInsertMeasurementIsDiscardedWhenSampleRateChanges()
	{
		AudioEngine* engine = Engine::audioEngine();
		const sample_rate_t currentRate = engine->outputSampleRate();
		const sample_rate_t changedRate = currentRate == 1 ? 2 : 1;
		HardwareInsert insert;
		HardwareInsert::Settings settings;
		settings.enabled = true;
		settings.measured = 512;
		insert.setSettings(settings, engine->framesPerPeriod());
		QCOMPARE(insert.latencyFrames(), 512);

		engine->audioDev()->setSampleRateForTesting(changedRate);
		const int preProcessLatency = insert.latencyFrames();  // PDC queries this before the next insert process period
		const int preProcessMeasurement = insert.settings().measured;
		std::vector<SampleFrame> block(static_cast<std::size_t>(engine->framesPerPeriod()), SampleFrame(0.25f, -0.25f));
		insert.process(block.data(), engine->framesPerPeriod(), *engine);
		const int postProcessMeasurement = insert.settings().measured;
		engine->audioDev()->setSampleRateForTesting(currentRate);
		QCOMPARE(preProcessLatency, 0);
		QCOMPARE(preProcessMeasurement, -1);
		QCOMPARE(postProcessMeasurement, -1);
		QCOMPARE(insert.settings().measured, -1);
		QCOMPARE(insert.latencyFrames(), 0);
	}

	void measuredInsertLatencyIsDiscardedWhenItsRouteChanges()
	{
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		for (const bool changeSend : {true, false})
		{
			HardwareInsert insert;
			HardwareInsert::Settings settings;
			settings.enabled = true;
			settings.measured = 512;
			insert.setSettings(settings, frames);
			QCOMPARE(insert.latencyFrames(), 512);

			if (changeSend) { settings.send = {2, 2}; }
			else { settings.ret = {2, 2}; }
			insert.setSettings(settings, frames);
			QCOMPARE(insert.settings().measured, -1);
			QCOMPARE(insert.latencyFrames(), 0);
		}
	}

	void malformedInsertMeasurementKeepsUnknownSentinel()
	{
		QDomDocument doc;
		QDomElement channel = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channel);
		QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
		element.setAttribute(QStringLiteral("on"), 1);
		element.setAttribute(QStringLiteral("latency"), -1);
		element.setAttribute(QStringLiteral("measured"), QStringLiteral("abc"));
		channel.appendChild(element);

		HardwareInsert insert;
		insert.load(channel, 64);
		QCOMPARE(insert.settings().measured, -1);
		QCOMPARE(insert.latencyFrames(), 0);
	}

	void unparsableSavedLevelsUseTheirDefaults()
	{
		// Qt's toFloat() returns 0 for "abc" and 0 is finite, so only the success flag can tell it apart.
		bool parsed = true;
		QDomDocument doc;
		QDomElement channel = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channel);
		QDomElement element = doc.createElement(QStringLiteral("hardwareinsert"));
		element.setAttribute(QStringLiteral("on"), 1);
		element.setAttribute(QStringLiteral("mix"), QStringLiteral("abc"));
		channel.appendChild(element);
		QVERIFY(element.attribute(QStringLiteral("mix")).toFloat(&parsed) == 0.f);
		QVERIFY(!parsed);

		HardwareInsert insert;
		insert.load(channel, 64);
		QCOMPARE(insert.settings().mix, 1.f);  // the insert's default: fully wet, not fully dry

		QDomDocument mixDoc;
		QDomElement mixerElement = mixDoc.createElement(QStringLiteral("mixer"));
		mixDoc.appendChild(mixerElement);
		QDomElement cuemix = mixDoc.createElement(QStringLiteral("cuemix"));
		cuemix.setAttribute(QStringLiteral("index"), 0);
		cuemix.setAttribute(QStringLiteral("level"), QStringLiteral("abc"));
		mixerElement.appendChild(cuemix);
		hardwareio::loadMixer(mixerElement);
		QCOMPARE(hardwareio::cueMaster(0).level, 1.f);  // the cuemix's default level: not silent
		hardwareio::resetMixer();
	}

	void disablingAnInsertCancelsAnInProgressPing()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		HardwareInsert insert;
		HardwareInsert::Settings settings;
		settings.enabled = true;
		settings.ret = {10000, 2};  // no engine input can accidentally complete the ping
		insert.setSettings(settings, fpp);
		insert.requestPing();
		std::vector<SampleFrame> block(fpp, SampleFrame(0.25f, 0.25f));
		insert.process(block.data(), fpp, *engine);
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Waiting));

		settings.enabled = false;
		insert.setSettings(settings, fpp);
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
		settings.enabled = true;
		insert.setSettings(settings, fpp);
		insert.process(block.data(), fpp, *engine);
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
	}

	void changingAnInsertRouteCancelsAnInProgressPing()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t frames = engine->framesPerPeriod();
		for (const bool changeSend : {true, false})
		{
			HardwareInsert insert;
			HardwareInsert::Settings settings;
			settings.enabled = true;
			settings.ret = {10000, 2};  // no engine input can accidentally complete the ping
			insert.setSettings(settings, frames);
			insert.requestPing();
			std::vector<SampleFrame> block(static_cast<std::size_t>(frames), SampleFrame(0.25f, 0.25f));
			insert.process(block.data(), frames, *engine);
			QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Waiting));

			settings = insert.settings();
			const auto waiting = insert.pingWord();
			if (changeSend) { settings.send = {2, 2}; }
			else { settings.ret = {10002, 2}; }
			insert.setSettings(settings, frames);
			QVERIFY2(!insert.publishPingResult(waiting, 1234), "a response from the old route published latency after a route change");
			QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
			insert.process(block.data(), frames, *engine);
			QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
			QCOMPARE(insert.settings().measured, -1);
		}
	}

	void changingReturnRouteDropsQueuedFrames()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		QVERIFY(fpp > 0);
		const f_cnt_t capturedFrames = 2 * fpp;
		MixerChannel* channel = Engine::mixer()->mixerChannel(1);
		HardwareInsert& insert = channel->m_hardwareIo.insert;
		insert.setSettings(HardwareInsert::Settings{}, fpp);

		std::vector<SampleFrame> captured(static_cast<std::size_t>(capturedFrames), SampleFrame(0.25f, 0.5f));
		engine->pushInputFrames(captured.data(), capturedFrames);
		engine->renderNextPeriod();
		QCOMPARE(engine->inputBufferFrames(), capturedFrames);

		HardwareInsert::Settings settings;
		settings.enabled = true;
		settings.ret = {0, 2};
		settings.mix = 1.f;
		settings.latencyOverride = 0;
		insert.setSettings(settings, fpp);

		std::vector<SampleFrame> first(static_cast<std::size_t>(fpp), SampleFrame{});
		insert.process(first.data(), fpp, *engine);
		bool firstMatches = true;
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			firstMatches = firstMatches && first[static_cast<std::size_t>(f)][0] == 0.25f
				&& first[static_cast<std::size_t>(f)][1] == 0.5f;
		}

		std::vector<SampleFrame> newCapture(static_cast<std::size_t>(capturedFrames), SampleFrame(-0.75f, 0.125f));
		settings.ret = {1, 1};
		insert.setSettings(settings, fpp);
		channel->m_muteModel.setValue(true);
		engine->pushInputFrames(newCapture.data(), capturedFrames);
		engine->renderNextPeriod();
		channel->m_muteModel.setValue(false);
		const bool newCaptureReady = engine->inputBufferFrames() == capturedFrames;

		std::vector<SampleFrame> afterRouteChange(static_cast<std::size_t>(fpp), SampleFrame{});
		insert.process(afterRouteChange.data(), fpp, *engine);
		bool newRouteMatches = true;
		for (f_cnt_t f = 0; f < fpp; ++f)
		{
			newRouteMatches = newRouteMatches && afterRouteChange[static_cast<std::size_t>(f)][0] == 0.125f
				&& afterRouteChange[static_cast<std::size_t>(f)][1] == 0.125f;
		}
		insert.setSettings(HardwareInsert::Settings{}, fpp);
		QVERIFY(newCaptureReady);
		QVERIFY(firstMatches);
		QVERIFY(newRouteMatches);
	}

	void destinationsParse()
	{
		audioio::Side outputs;
		outputs.channels = 6;
		outputs.buses = {{QStringLiteral("Phones"), 2, 2}};
		audioio::Destination d;
		QVERIFY(audioio::parseDestination(QStringLiteral("3/4"), outputs, &d, nullptr) && d == (audioio::Destination{2, 2}));
		QVERIFY(audioio::parseDestination(QStringLiteral("5"), outputs, &d, nullptr) && d == (audioio::Destination{4, 1}));
		QVERIFY(audioio::parseDestination(QStringLiteral("phones"), outputs, &d, nullptr) && d == (audioio::Destination{2, 2}));
		QVERIFY(audioio::parseDestination(QStringLiteral("none"), outputs, &d, nullptr) && d.none());
		QString error;
		QVERIFY(!audioio::parseDestination(QStringLiteral("3/5"), outputs, &d, &error) && !error.isEmpty());
		QVERIFY(!audioio::parseDestination(QStringLiteral("0"), outputs, &d, nullptr));
		QVERIFY(!audioio::parseDestination(QStringLiteral("Talkback"), outputs, &d, nullptr));
		// A number names a channel of the side it is read on: it must be one of its channels
		// (cluster new-p8-269: a destination beyond the side's count was accepted and stored).
		audioio::Side twoOutputs;
		twoOutputs.channels = 2;
		QVERIFY(!audioio::parseDestination(QStringLiteral("3"), twoOutputs, &d, &error));
		QVERIFY(!error.isEmpty());
		error.clear();
		QVERIFY(!audioio::parseDestination(QStringLiteral("2/3"), twoOutputs, &d, &error));
		QVERIFY(!error.isEmpty());
		error.clear();
		QVERIFY(!audioio::parseDestination(QStringLiteral("127"), twoOutputs, &d, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(audioio::destinationLabel({2, 2}, outputs), QStringLiteral("Phones"));
		QCOMPARE(audioio::channelLabel({4, 1}), QStringLiteral("5"));
	}

	void theInterfaceIsNamedPerDeviceAndUndone()
	{
		const QString key = audioio::currentDeviceKey();
		const QJsonObject before = audioio::storedEntry(key);
		const QJsonObject outputsSetup{{QStringLiteral("channels"), 4},
			{QStringLiteral("names"), QJsonArray{QStringLiteral("Main L"), QStringLiteral("Main R"), QStringLiteral("HP L"), QStringLiteral("HP R")}},
			{QStringLiteral("buses"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Main")}, {QStringLiteral("channels"), QStringLiteral("1/2")}},
				QJsonObject{{QStringLiteral("name"), QStringLiteral("Phones")}, {QStringLiteral("channels"), QStringLiteral("3/4")}}}}};
		const QJsonArray inputNames{QStringLiteral("Input 1"), QStringLiteral("Input 2"), QStringLiteral("Input 3"), QStringLiteral("Input 4"),
			QStringLiteral("Input 5"), QStringLiteral("Input 6"), QStringLiteral("Input 7"), QStringLiteral("Input 8")};
		const QJsonObject setupArgs{{QStringLiteral("outputs"), outputsSetup},
			{QStringLiteral("inputs"), QJsonObject{{QStringLiteral("names"), inputNames}}}};
		QVERIFY(ok(QStringLiteral("audio.io_setup"), setupArgs));
		const ControlResult list = run(QStringLiteral("audio.io_list"));
		QVERIFY(list.ok);
		const QJsonObject outputs = list.result.value(QStringLiteral("outputs")).toObject();
		const QJsonObject inputs = list.result.value(QStringLiteral("inputs")).toObject();
		QCOMPARE(outputs.value(QStringLiteral("requested")).toInt(), 4);
		QCOMPARE(outputs.value(QStringLiteral("granted")).toInt(), 2);  // the Dummy opens a stereo pair
		QCOMPARE(inputs.value(QStringLiteral("channels")).toArray().size(), inputs.value(QStringLiteral("requested")).toInt());
		QCOMPARE(outputs.value(QStringLiteral("buses")).toArray().at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Phones"));
		QCOMPARE(outputs.value(QStringLiteral("channels")).toArray().at(2).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("HP L"));
		const ControlResult routed = run(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("output"), QStringLiteral("Phones")}});
		QVERIFY(routed.ok);
		const QJsonObject output = routed.result.value(QStringLiteral("output")).toObject();
		QCOMPARE(output.value(QStringLiteral("channels")).toString(), QStringLiteral("3/4"));
		QCOMPARE(output.value(QStringLiteral("status")).toString(), QStringLiteral("stereo_backend"));  // routed nowhere, and said so
		QCOMPARE(run(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"), QJsonObject{{QStringLiteral("buses"), QJsonArray{
			QJsonObject{{QStringLiteral("name"), QStringLiteral("A")}, {QStringLiteral("channels"), QStringLiteral("1/2")}},
			QJsonObject{{QStringLiteral("name"), QStringLiteral("B")}, {QStringLiteral("channels"), QStringLiteral("2")}}}}}}}).errorKind,
			ControlErrorKind::InvalidArgs);  // overlapping buses
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the route
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the setup
		QVERIFY(audioio::storedEntry(key) == before);
	}

	/*! A store that is set but is not a JSON object holds the setups of every device, and it is kept as it is:
	 *  a write or an undo refuses rather than read it as empty and store that empty object with its one new
	 *  entry, which would drop every other device's setup. */
	void aMalformedSetupStoreIsNeverReplaced()
	{
		ConfigManager* config = ConfigManager::inst();
		const QString section = QStringLiteral("audioio");
		const QString attribute = QStringLiteral("setups");
		const QString stored = config->value(section, attribute);
		const QString key = audioio::currentDeviceKey();
		const QStringList unreadable{QStringLiteral("{\"Other device\": {\"outputs\": {"), QStringLiteral("[1, 2]")};
		for (const QString& text : unreadable)
		{
			config->setValue(section, attribute, text);
			QString error;
			const bool written = audioio::writeSetup(key, audioio::setupFor(key, 2), &error);
			audioio::restoreEntry(key, QJsonObject{});
			const QString after = config->value(section, attribute);
			config->setValue(section, attribute, stored);  // put back what the config held
			QVERIFY2(!written, qPrintable(text));
			QVERIFY2(!error.isEmpty(), qPrintable(text));
			QCOMPARE(after, text);
		}
	}

	/*! A restart is required whenever the CONFIGURED output count and the RUNNING
	 *  device's granted count differ - not only when this very call changed the
	 *  count. A saved setup that already asks for four outputs, met by a device
	 *  that granted a stereo pair, still needs a restart: reporting "none" while
	 *  the counts disagree tells the caller the running device matches what it
	 *  asked for, and it does not. */
	void aStillMismatchedOutputCountAsksForARestart()
	{
		const QString key = audioio::currentDeviceKey();
		const QJsonObject before = audioio::storedEntry(key);
		const QJsonObject listed = run(QStringLiteral("audio.io_list")).result.value(QStringLiteral("outputs")).toObject();
		const int granted = listed.value(QStringLiteral("granted")).toInt();
		QVERIFY2(granted > 0, "the running device granted no outputs to compare against");

		QVERIFY(ok(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"),
			QJsonObject{{QStringLiteral("channels"), granted == 4 ? 6 : 4}}}}));
		// The count now differs from what the running device granted, and it differs
		// whatever this next call does: it changes only the NAMES.
		const ControlResult renamed = run(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"),
			QJsonObject{{QStringLiteral("names"), QJsonArray{QStringLiteral("Renamed L"), QStringLiteral("Renamed R")}}}}});
		QVERIFY2(renamed.ok, qPrintable(renamed.errorMessage));
		const QJsonObject outputs = renamed.result.value(QStringLiteral("outputs")).toObject();
		QCOMPARE(outputs.value(QStringLiteral("requested")).toInt(), granted == 4 ? 6 : 4);
		QCOMPARE(outputs.value(QStringLiteral("granted")).toInt(), granted);
		QVERIFY2(renamed.result.value(QStringLiteral("restart_required")).toBool(),
			"the configured count still differs from the running device's, so a restart is still required");
		// The honest report is the mismatch itself: this call changed no count, so the
		// names are what took effect now, while the count still waits for the next start.
		QCOMPARE(renamed.result.value(QStringLiteral("applied")).toString(), QStringLiteral("now"));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(audioio::storedEntry(key) == before);
	}

	/*! Undo takes the ping back with it. The measurement lands a few periods later,
	 *  on the audio thread; an undo that restored the settings but left the ping
	 *  running would let it finish afterwards and write a measured latency into
	 *  the project the undo just put back. */
	void anUndoCancelsThePingItRequested()
	{
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("9/10")}, {QStringLiteral("return"), QStringLiteral("3")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert_ping"), {{QStringLiteral("channel"), ch(1)}}));
		auto& insert = Engine::mixer()->mixerChannel(1)->m_hardwareIo.insert;
		QVERIFY(insert.pingState() != HardwareInsert::PingState::Idle);  // the click is in flight

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(insert.pingState() == HardwareInsert::PingState::Idle,
			"the undone ping can still land a measured latency after the undo");

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the insert itself
		QVERIFY(!Engine::mixer()->mixerChannel(1)->m_hardwareIo.insert.enabled());
	}

	void anEmptyIoSetupAddsNoUndoStep()
	{
		const int depthBefore = Engine::projectJournal()->undoDepth();
		QVERIFY(ok(QStringLiteral("audio.io_setup"), QJsonObject{}));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);
	}

	void settingHardwareOutputToItsCurrentRouteAddsNoUndoStep()
	{
		const QString id = ch(1);
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("default")}}));
		const int depthBefore = Engine::projectJournal()->undoDepth();
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("default")}}));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore);

		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("3/4")}}));
		const int depthBefore2 = Engine::projectJournal()->undoDepth();
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), id}, {QStringLiteral("output"), QStringLiteral("3/4")}}));
		QCOMPARE(Engine::projectJournal()->undoDepth(), depthBefore2);
	}

	/*! The undo calls the ping off, but a period that had already read the request can still
	 *  move the ping on afterwards: the ping comes back and the answer it hears lands a
	 *  measured latency in the project the undo just put back. The ping's state machine must
	 *  be a compare-and-swap, so a cancel wins against a move that is already on its way.
	 *
	 *  The two halves of the race are driven one after the other, without a thread and
	 *  without a clock: the period reads the state, the interface thread cancels, the period
	 *  makes its move. */
	void aCancelledPingIsNotRevivedByAPeriodThatHadReadTheRequest()
	{
		HardwareInsert insert;

		// The period read the request; the undo calls the ping off; the period claims the
		// request anyway: the claim must not bring the ping back.
		insert.requestPing();
		const auto requested = insert.pingWord();
		insert.cancelPing();
		QVERIFY2(!insert.claimPing(requested), "a period that had read the request put the ping back after the cancel");
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
		QCOMPARE(insert.settings().measured, -1);

		// The click is on its way; the undo calls the ping off; the period hears the click and
		// publishes what it measured: the answer must not land either.
		insert.requestPing();
		QVERIFY(insert.claimPing(insert.pingWord()));
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Waiting));
		const auto waiting = insert.pingWord();
		insert.cancelPing();
		QVERIFY2(!insert.publishPingResult(waiting, 1234), "a ping that was called off published its measurement");
		QCOMPARE(static_cast<int>(insert.pingState()), static_cast<int>(HardwareInsert::PingState::Idle));
		QCOMPARE(insert.settings().measured, -1);
	}

	void everythingSurvivesASaveAndLoad()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();  // a project with nothing routed
		while (mixer->numChannels() <= 2) { mixer->createChannel(); }
		QDomDocument plain;
		QDomElement plainMixer = plain.createElement(QStringLiteral("mixer"));
		plain.appendChild(plainMixer);
		mixer->saveSettings(plain, plainMixer);
		QVERIFY2(!plain.toString().contains(QStringLiteral("hardware")) && !plain.toString().contains(QStringLiteral("cue")),
			"a project that routes nothing to the hardware saves none of it");

		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("5/6")}}));
		QVERIFY(ok(QStringLiteral("mixer.cue_send"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("cue"), QStringLiteral("B")},
			{QStringLiteral("level"), 0.7}, {QStringLiteral("pan"), 0.25}, {QStringLiteral("pre_fader"), false}}));
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("B")}, {QStringLiteral("name"), QStringLiteral("Drummer")},
			{QStringLiteral("output"), QStringLiteral("7/8")}, {QStringLiteral("level"), 0.8}, {QStringLiteral("follow_listen"), false},
			{QStringLiteral("listen_to"), QStringLiteral("cue")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_insert"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("enabled"), true},
			{QStringLiteral("send"), QStringLiteral("9/10")}, {QStringLiteral("return"), QStringLiteral("3")}, {QStringLiteral("mix"), 0.5},
			{QStringLiteral("latency_frames"), 123}}));
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		mixer->saveSettings(doc, element);
		mixer->clear();
		QVERIFY(hardwareio::cueMaster(1) == hardwareio::CueMaster{});
		mixer->loadSettings(element);

		verifyLoaded(*mixer);
		mixer->clear();
	}

	void nonFiniteCueSettingsSanitizeToDefaults()
	{
		// A hand-edited project can carry level="nan" / pan="nan": std::clamp lets NaN through,
		// and it contaminates the cue's output samples. Loading must sanitize to the defaults.
		QDomDocument doc;
		QDomElement channelElement = doc.createElement(QStringLiteral("mixerchannel"));
		doc.appendChild(channelElement);
	 QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
	 channelElement.appendChild(element);
	 QDomElement cue = doc.createElement(QStringLiteral("cue"));
	 cue.setAttribute(QStringLiteral("index"), 0);
	 cue.setAttribute(QStringLiteral("level"), QStringLiteral("nan"));
	 cue.setAttribute(QStringLiteral("pan"), QStringLiteral("nan"));
	 element.appendChild(cue);
	 ChannelHardwareIo loaded;
		 loaded.load(channelElement, Engine::audioEngine()->framesPerPeriod());
	 QVERIFY(std::isfinite(loaded.cues[0].level) && std::isfinite(loaded.cues[0].pan));
	 QCOMPARE(loaded.cues[0].level, 0.f);  // the attribute's own default: no send
	 QCOMPARE(loaded.cues[0].pan, 0.f);    // centred

	 QDomDocument mixDoc;
	 QDomElement mixerElement = mixDoc.createElement(QStringLiteral("mixer"));
	 mixDoc.appendChild(mixerElement);
	 QDomElement cuemix = mixDoc.createElement(QStringLiteral("cuemix"));
	 cuemix.setAttribute(QStringLiteral("index"), 0);
	 cuemix.setAttribute(QStringLiteral("level"), QStringLiteral("nan"));
	 mixerElement.appendChild(cuemix);
	 hardwareio::loadMixer(mixerElement);
	 QVERIFY(std::isfinite(hardwareio::cueMaster(0).level));
	 QCOMPARE(hardwareio::cueMaster(0).level, 1.f);  // the cuemix's default level
	 hardwareio::resetMixer();
	}

	/*! A saved route that does not parse leaves the default in force (cluster new-p14-120-port):
	 *  a hand-edited project can carry output="3/5" (a pair that is not two consecutive channels),
	 *  and the master must not be silenced by it - it keeps its default route, the outputs 1/2.
	 *  An explicit "none" does parse: it is the user's own route nowhere, and it stays. */
	void malformedOutputKeepsTheDefaultRoute()
	{
		QDomDocument doc;
		QDomElement channelElement = doc.createElement(QStringLiteral("mixerchannel"));
		doc.appendChild(channelElement);
		QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
		element.setAttribute(QStringLiteral("output"), QStringLiteral("3/5"));
		channelElement.appendChild(element);

		ChannelHardwareIo io;
		io.load(channelElement, Engine::audioEngine()->framesPerPeriod());
		QVERIFY2(!io.outputSet, "a route that does not parse must not replace the default");
		QVERIFY2(io.effectiveOutput(true) == (audioio::Destination{0, 2}),
			"the master must keep its default route (the outputs 1/2)");

		QDomElement noneChannel = doc.createElement(QStringLiteral("mixerchannel"));
		doc.appendChild(noneChannel);
		QDomElement noneElement = doc.createElement(QStringLiteral("hardwareio"));
		noneElement.setAttribute(QStringLiteral("output"), QStringLiteral("none"));
		noneChannel.appendChild(noneElement);

		ChannelHardwareIo none;
		none.load(noneChannel, Engine::audioEngine()->framesPerPeriod());
		QVERIFY(none.outputSet);
		QVERIFY(none.effectiveOutput(true).none());
	}

	void malformedDuplicateCueOutputKeepsItsPreviousDefaultSemantics()
	{
		QDomDocument doc;
		QDomElement mixerElement = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(mixerElement);
		for (const QString& output : {QStringLiteral("3"), QStringLiteral("3/5")})
		{
			QDomElement cue = doc.createElement(QStringLiteral("cuemix"));
			cue.setAttribute(QStringLiteral("index"), 0);
			cue.setAttribute(QStringLiteral("output"), output);
			mixerElement.appendChild(cue);
		}

		// Before the shared parser refactor, the later malformed record set the cue back to its
		// default (nowhere) instead of retaining the earlier record's route.
		hardwareio::loadMixer(mixerElement);
		QVERIFY(hardwareio::cueMaster(0).output.none());
		hardwareio::resetMixer();
	}

	void channelCueLoadsRequireExplicitIndices()
	{
		bool ignored = true;
		for (const bool omitIndex : {true, false})
		{
			QDomDocument doc;
			QDomElement channel = doc.createElement(QStringLiteral("channel"));
			doc.appendChild(channel);
			QDomElement hardware = doc.createElement(QStringLiteral("hardwareio"));
			channel.appendChild(hardware);
			QDomElement cue = doc.createElement(QStringLiteral("cue"));
			if (!omitIndex) { cue.setAttribute(QStringLiteral("index"), QStringLiteral("broken")); }
			cue.setAttribute(QStringLiteral("level"), QStringLiteral("0.75"));
			hardware.appendChild(cue);

			ChannelHardwareIo io;
			io.load(channel, Engine::audioEngine()->framesPerPeriod());
			ignored = ignored && io.cues[0] == hardwareio::CueSend{};
		}
		QVERIFY(ignored);
	}

	void cueMasterLoadsRequireExplicitIndices()
	{
		bool ignored = true;
		for (const bool omitIndex : {true, false})
		{
			QDomDocument doc;
			QDomElement mixer = doc.createElement(QStringLiteral("mixer"));
			doc.appendChild(mixer);
			QDomElement cue = doc.createElement(QStringLiteral("cuemix"));
			if (!omitIndex) { cue.setAttribute(QStringLiteral("index"), QStringLiteral("broken")); }
			cue.setAttribute(QStringLiteral("name"), QStringLiteral("Invalid cue"));
			mixer.appendChild(cue);

			hardwareio::loadMixer(mixer);
			ignored = ignored && hardwareio::cueMaster(0) == hardwareio::CueMaster{};
		}
		QVERIFY(ignored);
	}

	void malformedPrefaderKeepsItsDefault()
	{
		// "prefader" defaults to true. A malformed value ("abc") is not a zero: it must keep the default,
		// while a well-formed "0" still switches the send to post-fader.
		QDomDocument doc;
		QDomElement channelElement = doc.createElement(QStringLiteral("mixerchannel"));
		doc.appendChild(channelElement);
		QDomElement element = doc.createElement(QStringLiteral("hardwareio"));
		channelElement.appendChild(element);
		QDomElement malformed = doc.createElement(QStringLiteral("cue"));
		malformed.setAttribute(QStringLiteral("index"), 0);
		malformed.setAttribute(QStringLiteral("level"), QStringLiteral("0.5"));
		malformed.setAttribute(QStringLiteral("prefader"), QStringLiteral("abc"));
		element.appendChild(malformed);
		QDomElement zero = doc.createElement(QStringLiteral("cue"));
		zero.setAttribute(QStringLiteral("index"), 1);
		zero.setAttribute(QStringLiteral("level"), QStringLiteral("0.5"));
		zero.setAttribute(QStringLiteral("prefader"), QStringLiteral("0"));
		element.appendChild(zero);
		ChannelHardwareIo loaded;
		loaded.load(channelElement, Engine::audioEngine()->framesPerPeriod());
		QVERIFY(loaded.cues[0].preFader);   // malformed: the default, pre-fader
		QVERIFY(!loaded.cues[1].preFader);  // "0" is a real value: post-fader
	}

	void malformedListenKeepsItsDefault()
	{
		// A cue master's "listen" defaults to true. A malformed value ("abc") must keep the default,
		// while a well-formed "0" still stops the cue following the listen.
		QDomDocument mixDoc;
		QDomElement mixerElement = mixDoc.createElement(QStringLiteral("mixer"));
		mixDoc.appendChild(mixerElement);
		QDomElement malformedMix = mixDoc.createElement(QStringLiteral("cuemix"));
		malformedMix.setAttribute(QStringLiteral("index"), 0);
		malformedMix.setAttribute(QStringLiteral("listen"), QStringLiteral("abc"));
		mixerElement.appendChild(malformedMix);
		QDomElement zeroMix = mixDoc.createElement(QStringLiteral("cuemix"));
		zeroMix.setAttribute(QStringLiteral("index"), 1);
		zeroMix.setAttribute(QStringLiteral("listen"), QStringLiteral("0"));
		mixerElement.appendChild(zeroMix);
		hardwareio::loadMixer(mixerElement);
		QVERIFY(hardwareio::cueMaster(0).followListen);   // malformed: the default, follows listen
		QVERIFY(!hardwareio::cueMaster(1).followListen);  // "0" switches it off
		hardwareio::resetMixer();
	}

private:
	//! What everythingSurvivesASaveAndLoad routed, read back after the load.
	static void verifyLoaded(Mixer& mixer)
	{
		const ChannelHardwareIo& io = mixer.mixerChannel(2)->m_hardwareIo;
		QVERIFY(io.outputSet && io.output == (audioio::Destination{4, 2}));
		QVERIFY(std::abs(io.cues[1].level - 0.7f) < 1e-6f && std::abs(io.cues[1].pan - 0.25f) < 1e-6f && !io.cues[1].preFader);
		const hardwareio::CueMaster& cue = hardwareio::cueMaster(1);
		QCOMPARE(cue.name, QStringLiteral("Drummer"));
		QVERIFY(cue.output == (audioio::Destination{6, 2}) && std::abs(cue.level - 0.8f) < 1e-6f && !cue.followListen);
		QVERIFY(hardwareio::listenToCue());
		const HardwareInsert::Settings insert = io.insert.settings();
		QVERIFY(insert.enabled && insert.send == (audioio::Destination{8, 2}) && insert.ret == (audioio::Destination{2, 1}));
		QVERIFY(std::abs(insert.mix - 0.5f) < 1e-6f && insert.latencyOverride == 123);
	}
};

QTEST_MAIN(HardwareIoTest)
#include "HardwareIoTest.moc"
