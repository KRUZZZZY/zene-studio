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
#include "ControlVocabulary.h"
#include "Engine.h"
#include "HardwareIo.h"
#include "Mixer.h"
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
		mixerlisten::clear();
		QVERIFY(ok(QStringLiteral("mixer.cue_master"), {{QStringLiteral("listen_to"), QStringLiteral("main")}}));
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
		QCOMPARE(audioio::destinationLabel({2, 2}, outputs), QStringLiteral("Phones"));
		QCOMPARE(audioio::channelLabel({4, 1}), QStringLiteral("5"));
	}

	void theInterfaceIsNamedPerDeviceAndUndone()
	{
		const QString key = audioio::currentDeviceKey();
		const QJsonObject before = audioio::storedEntry(key);
		QVERIFY(ok(QStringLiteral("audio.io_setup"), {{QStringLiteral("outputs"), QJsonObject{{QStringLiteral("channels"), 4},
			{QStringLiteral("names"), QJsonArray{QStringLiteral("Main L"), QStringLiteral("Main R"), QStringLiteral("HP L"), QStringLiteral("HP R")}},
			{QStringLiteral("buses"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Main")}, {QStringLiteral("channels"), QStringLiteral("1/2")}},
				QJsonObject{{QStringLiteral("name"), QStringLiteral("Phones")}, {QStringLiteral("channels"), QStringLiteral("3/4")}}}}}}}));
		const ControlResult list = run(QStringLiteral("audio.io_list"));
		QVERIFY(list.ok);
		const QJsonObject outputs = list.result.value(QStringLiteral("outputs")).toObject();
		QCOMPARE(outputs.value(QStringLiteral("requested")).toInt(), 4);
		QCOMPARE(outputs.value(QStringLiteral("granted")).toInt(), 2);  // the Dummy opens a stereo pair
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
