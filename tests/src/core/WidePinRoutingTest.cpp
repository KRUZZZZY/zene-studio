/*
 * WidePinRoutingTest.cpp - devices pinned onto the channels of a discrete-64 mixer bus (Q050-155.2)
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

/*! Q050-155.2. The engine runs a 128-output device that never starts: the test drives the periods itself, as
 *  WideDiscreteBusTest does. Channel 1 (discrete-64) is the source: 64 distinct constants on its pair and its
 *  62 side planes. It carries 32 of the build's amplifiers, amplifier k pinned (port.set_pins) onto channels
 *  2k and 2k + 1 at its own volume, and routes only to channel 2 (discrete-64), whose direct out is outputs
 *  1..64. So output c must read level(c) times amplifier c/2's gain: a device that missed its pair, a pin that
 *  landed on the wrong index, or a route that reordered or leaked shows up as a wrong output. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "HardwareOutputBus.h"
#include "Mixer.h"
#include "MixerSurround.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

constexpr int Outputs = 128;
constexpr int Pairs = 32;

class WideOutputs : public AudioDevice
{
public:
	explicit WideOutputs(AudioEngine* engine) : AudioDevice(Outputs, engine) {}

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool ok(const QString& id, const QJsonObject& args)
{
	const ControlResult result = run(id, args);
	if (!result.ok) { qWarning("%s: %s", qPrintable(id), qPrintable(result.errorMessage)); }
	return result.ok;
}

QString ch(int index)
{
	return control::channelIdOf(Engine::mixer()->mixerChannel(index));
}

bool format(int channel, const QString& name)
{
	return ok(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(channel)}, {QStringLiteral("format"), name}});
}

HardwareOutputBus& bus()
{
	return Engine::audioEngine()->hardwareOutputs();
}

float level(int c)
{
	return static_cast<float>(c + 1) / 128.f;
}

//! Amplifier k's volume (percent): every pair its own gain.
int volumeOf(int pair)
{
	return 100 - 2 * pair;
}

QString deviceNamed(const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == name) { return device.toObject().value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

QString loadAmplifier(const QString& target)
{
	const QString device = deviceNamed(QStringLiteral("amplifier"));
	if (device.isEmpty()) { return QString(); }
	return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device}})
		.result.value(QStringLiteral("id")).toString();
}

//! The fx-<n> of channel 1's device at chain position @a k (ids are stable ids, not positions).
QString ampAt(int k)
{
	const auto& effects = Engine::mixer()->mixerChannel(1)->m_fxChain.effects();
	return k < static_cast<int>(effects.size()) ? control::effectIdOf(effects[static_cast<std::size_t>(k)]) : QString();
}

QJsonObject pin(const QString& direction, int track, int processor, bool on)
{
	return {{QStringLiteral("direction"), direction}, {QStringLiteral("track_channel"), track},
		{QStringLiteral("processor_channel"), processor}, {QStringLiteral("enabled"), on}};
}

//! port.set_pins' list moving a stereo device from the pair to channels @a first and @a first + 1.
QJsonArray onto(int first)
{
	QJsonArray pins;
	for (const QString& direction : {QStringLiteral("in"), QStringLiteral("out")})
	{
		pins.append(pin(direction, 0, 0, false));
		pins.append(pin(direction, 1, 1, false));
		pins.append(pin(direction, first, 0, true));
		pins.append(pin(direction, first + 1, 1, true));
	}
	return pins;
}

ControlResult setPins(const QString& target, const QString& device, const QJsonArray& pins)
{
	return run(QStringLiteral("port.set_pins"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device},
		{QStringLiteral("pins"), pins}});
}

QJsonObject portState(const QString& target, const QString& device)
{
	return run(QStringLiteral("port.get_state"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device}}).result;
}

//! The (track, processor) pins of one direction of a port.get_state reply, as "t:p" strings.
QStringList pinsOf(const QJsonObject& state, const QString& direction)
{
	QStringList out;
	for (const QJsonValue& value : state.value(QStringLiteral("ports")).toObject().value(direction).toObject()
		.value(QStringLiteral("pins")).toArray())
	{
		out << QStringLiteral("%1:%2").arg(value.toObject().value(QStringLiteral("track_channel")).toInt())
			.arg(value.toObject().value(QStringLiteral("processor_channel")).toInt());
	}
	return out;
}

void feedSource(Mixer& mixer)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> pair(fpp, SampleFrame(level(0), level(1)));
	SampleFrame* buffers[1] = {pair.data()};
	mixer.mixToChannel(AudioBus{buffers, 1, fpp}, 1);
	ChannelSurround& side = mixer.mixerChannel(1)->m_surround;
	for (int c = 2; c < surround::channelCount(side.format()); ++c) { std::fill_n(side.plane(c), fpp, level(c)); }
	if (surround::channelCount(side.format()) > 2) { side.markWritten(); }
}

void period(Mixer& mixer)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	bus().swap();
	feedSource(mixer);
	mixer.prepareMasterMix();
	zeroSampleFrames(out.data(), fpp);
	mixer.masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();
	bus().swap();
}

float outputLevel(int channel)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	const float first = bus().sample(channel, 0, SampleFrame{});
	for (f_cnt_t f = 1; f < fpp; ++f)
	{
		if (bus().sample(channel, f, SampleFrame{}) != first) { return std::nanf(""); }
	}
	return first;
}

//! Outputs 0..63 hold level(c) * gain(c), 64..127 silence; the first output that differs, named.
template<class Gain>
QString checkOutputs(Gain gain)
{
	for (int c = 0; c < Outputs; ++c)
	{
		const float expected = c < 64 ? level(c) * gain(c) : 0.f;
		const float seen = outputLevel(c);
		if (!(std::abs(seen - expected) < 1e-5f))
		{
			return QStringLiteral("output %1: %2, expected %3").arg(c + 1).arg(seen).arg(expected);
		}
	}
	return QString();
}

QJsonObject sendFrom(const QString& command, const QString& key, const QString& from, const QString& to)
{
	for (const QJsonValue& value : run(command).result.value(QStringLiteral("channels")).toArray())
	{
		if (value.toObject().value(key).toString() != from) { continue; }
		for (const QJsonValue& send : value.toObject().value(QStringLiteral("sends")).toArray())
		{
			if (send.toObject().value(QStringLiteral("to")).toString() == to) { return send.toObject(); }
		}
	}
	return {};
}

} // namespace

class WidePinRoutingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		AudioEngine* engine = Engine::audioEngine();
		engine->setAudioDevice(new WideOutputs(engine), false);  // nothing renders but the test
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 3) { mixer->createChannel(); }
		ControlRegistry::setReady(true);
		if (deviceNamed(QStringLiteral("amplifier")).isEmpty()) { QSKIP("this build has no amplifier module"); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The render: 32 devices, one per pair of a discrete-64 bus, each at its own gain, through a mixer route.
	void everyChannelKeepsItsIndexThroughPinsAndRoutes()
	{
		Mixer& mixer = *Engine::mixer();
		routeThroughTheBus(mixer);
		QVERIFY(format(1, QStringLiteral("discrete-64")));
		QVERIFY(format(2, QStringLiteral("discrete-64")));
		for (int k = 0; k < Pairs; ++k)
		{
			const QString amp = loadAmplifier(ch(1));
			QVERIFY(!amp.isEmpty());
			QCOMPARE(amp, ampAt(k));
			QVERIFY(ok(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), amp},
				{QStringLiteral("name"), QStringLiteral("Volume")}, {QStringLiteral("value"), volumeOf(k)}}));
			const ControlResult set = setPins(ch(1), amp, onto(2 * k));
			QVERIFY2(set.ok, qPrintable(amp + QStringLiteral(": ") + set.errorMessage));
			const QJsonObject state = portState(ch(1), amp);
			const QString pair = QStringLiteral("%1:0,%2:1").arg(2 * k).arg(2 * k + 1);
			QCOMPARE(pinsOf(state, QStringLiteral("in")).join(QLatin1Char(',')), pair);
			QCOMPARE(pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')), pair);
			QCOMPARE(state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("channels")).toInt(), 64);
			QCOMPARE(state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("format")).toString(), QStringLiteral("discrete-64"));
		}
		for (int i = 0; i < 3; ++i) { period(mixer); }
		QString wrong = checkOutputs([](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("32 pinned devices: ") + wrong));

		// One undo step puts the last device back on the pair it came from, and nothing else moves.
		const QString last = ampAt(Pairs - 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const QJsonObject undone = portState(ch(1), last);
		QCOMPARE(pinsOf(undone, QStringLiteral("in")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));
		QCOMPARE(pinsOf(undone, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));
		QCOMPARE(pinsOf(portState(ch(1), ampAt(30)), QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("60:0,61:1"));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		wrong = checkOutputs([](int c) {
			if (c >= 62) { return 1.f; }  // no device on 63/64 now
			if (c < 2) { return volumeOf(Pairs - 1) / 100.f; }  // the pair: amplifier 0 (unity), then the last one
			return volumeOf(c / 2) / 100.f;
		});
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("after one undo: ") + wrong));
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		for (int i = 0; i < 3; ++i) { period(mixer); }
		wrong = checkOutputs([](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("after the redo: ") + wrong));
	}

	//! mixer.get_state and routing.matrix say what each route carries; a stereo route keeps its old shape.
	void routesReportTheChannelsTheyCarry()
	{
		Mixer& mixer = *Engine::mixer();
		QVERIFY(format(1, QStringLiteral("discrete-64")));
		QVERIFY(format(2, QStringLiteral("discrete-64")));
		for (const QString& command : {QStringLiteral("mixer.get_state"), QStringLiteral("routing.matrix")})
		{
			const QString key = command == QLatin1String("mixer.get_state") ? QStringLiteral("id") : QStringLiteral("channel");
			const QJsonObject send = sendFrom(command, key, ch(1), ch(2));
			QCOMPARE(send.value(QStringLiteral("channels")).toInt(), 64);
			QCOMPARE(send.value(QStringLiteral("channel_routing")).toString(), QStringLiteral("index"));
		}
		QVERIFY(format(2, QStringLiteral("discrete-16")));
		QCOMPARE(sendFrom(QStringLiteral("routing.matrix"), QStringLiteral("channel"), ch(1), ch(2)).value(QStringLiteral("channels")).toInt(), 16);
		QVERIFY(format(2, QStringLiteral("discrete-64")));
		// A stereo route (channel 3 to the master) reads back exactly as it always did.
		const QJsonObject stereo = sendFrom(QStringLiteral("mixer.get_state"), QStringLiteral("id"), ch(3), ch(0));
		QVERIFY(stereo.contains(QStringLiteral("amount")));
		QVERIFY(!stereo.contains(QStringLiteral("channels")));
		QVERIFY(!stereo.contains(QStringLiteral("channel_routing")));
		Q_UNUSED(mixer)
	}

	//! A device's pins past the pair are written with the project and read back after a reload.
	void pinsPastThePairSurviveASaveAndLoad()
	{
		Mixer* mixer = Engine::mixer();
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		mixer->saveSettings(doc, element);
		mixer->clear();
		mixer->loadSettings(element);
		QCOMPARE(surround::formatName(mixer->mixerChannel(1)->m_surround.format()), QStringLiteral("discrete-64"));
		for (const int k : {1, 20, Pairs - 1})
		{
			const QJsonObject state = portState(ch(1), ampAt(k));
			const QString pair = QStringLiteral("%1:0,%2:1").arg(2 * k).arg(2 * k + 1);
			QCOMPARE(pinsOf(state, QStringLiteral("in")).join(QLatin1Char(',')), pair);
			QCOMPARE(pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')), pair);
		}
		routeThroughTheBus(*mixer);
		for (int i = 0; i < 3; ++i) { period(*mixer); }
		const QString wrong = checkOutputs([](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("after the reload: ") + wrong));
	}

	void theWideInsertMixAllocatesNothing()
	{
		Mixer& mixer = *Engine::mixer();
		for (int i = 0; i < 2; ++i) { period(mixer); }
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(fpp);
		bus().swap();
		feedSource(mixer);
		mixer.prepareMasterMix();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		mixer.masterMix(out.data());
		test::tlCountAllocations = false;
		bus().swap();
		AutomatableModel::incrementPeriodCounter();
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), 0ull);
		const QString wrong = checkOutputs([](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
	}

	//! A bus narrower than a device's pins: the far pins reach nothing, the pair is untouched, nothing is read
	//! past the bus (the router is clamped to the bus it is handed).
	void aNarrowerBusLeavesFarPinsHarmless()
	{
		Mixer& mixer = *Engine::mixer();
		QVERIFY(format(1, QStringLiteral("stereo")));
		const QJsonObject state = portState(ch(1), ampAt(20));
		QCOMPARE(state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("channels")).toInt(), 2);
		QCOMPARE(state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("reached_track_channels")).toInt(), 2);
		QCOMPARE(pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("40:0,41:1"));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		// Amplifier 0 (unity) is the only device on the pair; the stereo channel carries nothing else.
		const QString wrong = checkOutputs([](int c) { return c < 2 ? 1.f : 0.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("stereo bus: ") + wrong));
	}

	//! Review round 1: pins past the pair saved while the bus is stereo come back, and work again once the bus is
	//! discrete-64 again (save on stereo -> open -> widen).
	void farPinsSurviveASaveWhileTheBusIsNarrow()
	{
		Mixer* mixer = Engine::mixer();
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		mixer->saveSettings(doc, element);
		mixer->clear();
		mixer->loadSettings(element);
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
		for (const int k : {1, 20, Pairs - 1})
		{
			const QJsonObject state = portState(ch(1), ampAt(k));
			const QString pair = QStringLiteral("%1:0,%2:1").arg(2 * k).arg(2 * k + 1);
			QCOMPARE(pinsOf(state, QStringLiteral("in")).join(QLatin1Char(',')), pair);
			QCOMPARE(pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')), pair);
		}
		QVERIFY(format(1, QStringLiteral("discrete-64")));
		routeThroughTheBus(*mixer);
		for (int i = 0; i < 3; ++i) { period(*mixer); }
		const QString wrong = checkOutputs([](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("widened after a stereo save: ") + wrong));
	}

private:
	//! Channel 1 into channel 2 only; channel 2's direct out on outputs 1..N, the master on 127/128.
	static void routeThroughTheBus(Mixer& mixer)
	{
		mixer.deleteChannelSend(1, 0);
		mixer.deleteChannelSend(2, 0);
		mixer.createChannelSend(1, 2);
		mixer.mixerChannel(1)->m_volumeModel.setValue(1.f);
		mixer.mixerChannel(2)->m_volumeModel.setValue(1.f);
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("1/2")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("127/128")}}));
	}
};

QTEST_MAIN(WidePinRoutingTest)
#include "WidePinRoutingTest.moc"
