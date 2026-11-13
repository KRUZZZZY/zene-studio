/*
 * SurroundDevicePathTest.cpp - 5.1/7.1 speakers through pinned channel devices to matching outputs (Q050-296.1)
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

/*! Q050-296.1. A named 5.1/7.1 layout must be processed by pinned devices on its own speakers and
 *  delivered on the matching outputs: channel 1 is the source, amplifier k is pinned onto speaker pair
 *  (2k, 2k + 1) at its own volume, and channel 2 carries the bus on outputs 1..N. So output c must read
 *  level(c) times amplifier c/2's gain, and the 5.1/7.1 identity matrix keeps speaker c on output c.
 *  Today named layouts never take the wide bus, so the pinned amps never reach channels 2..7 and the
 *  bus reports 2 channels: this test is RED until the fix. */

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

constexpr int Outputs = 16;

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
	return static_cast<float>(c + 1) / 16.f;
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

//! Outputs 0..channels-1 hold level(c) * gain(c), all others silence; the first output that differs, named.
template<class Gain>
QString checkOutputs(int channels, Gain gain)
{
	for (int c = 0; c < Outputs; ++c)
	{
		const float expected = c < channels ? level(c) * gain(c) : 0.f;
		const float seen = outputLevel(c);
		if (!(std::abs(seen - expected) < 1e-5f))
		{
			return QStringLiteral("output %1: %2, expected %3").arg(c + 1).arg(seen).arg(expected);
		}
	}
	return QString();
}

} // namespace

class SurroundDevicePathTest : public QObject
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

	void everySpeakerIsProcessedOnItsOwnOutput_data()
	{
		QTest::addColumn<QString>("layout");
		QTest::addColumn<int>("channels");
		QTest::newRow("5.1") << QStringLiteral("5.1") << 6;
		QTest::newRow("7.1") << QStringLiteral("7.1") << 8;
	}

	//! One device per speaker pair, each on its own two channels and its own gain, through the bus.
	void everySpeakerIsProcessedOnItsOwnOutput()
	{
		QFETCH(QString, layout);
		QFETCH(int, channels);
		Mixer& mixer = *Engine::mixer();
		const QString bad = pinSpeakers(mixer, layout, channels);
		QVERIFY2(bad.isEmpty(), qPrintable(layout + QStringLiteral(": ") + bad));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		const QString wrong = checkOutputs(channels, [](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(layout + QStringLiteral(" pinned devices: ") + wrong));
	}

	//! A device's speaker pins are written with the project and read back after a reload.
	void pinsSurviveASaveAndLoad()
	{
		Mixer* mixer = Engine::mixer();
		const QString bad = pinSpeakers(*mixer, QStringLiteral("7.1"), 8);
		QVERIFY2(bad.isEmpty(), qPrintable(bad));
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		mixer->saveSettings(doc, element);
		mixer->clear();
		mixer->loadSettings(element);
		QCOMPARE(surround::formatName(mixer->mixerChannel(1)->m_surround.format()), QStringLiteral("7.1"));
		for (const int k : {0, 3})
		{
			const QJsonObject state = portState(ch(1), ampAt(k));
			const QString pair = QStringLiteral("%1:0,%2:1").arg(2 * k).arg(2 * k + 1);
			QCOMPARE(pinsOf(state, QStringLiteral("in")).join(QLatin1Char(',')), pair);
			QCOMPARE(pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')), pair);
		}
		routeThroughTheBus(*mixer);
		for (int i = 0; i < 3; ++i) { period(*mixer); }
		const QString wrong = checkOutputs(8, [](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("after the reload: ") + wrong));
	}

	//! One undo step puts the last device back on the pair it came from, and nothing else moves.
	void undoRestoresThePair()
	{
		Mixer& mixer = *Engine::mixer();
		const QString bad = pinSpeakers(mixer, QStringLiteral("7.1"), 8);
		QVERIFY2(bad.isEmpty(), qPrintable(bad));
		const QString last = ampAt(3);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const QJsonObject undone = portState(ch(1), last);
		QCOMPARE(pinsOf(undone, QStringLiteral("in")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));
		QCOMPARE(pinsOf(undone, QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("0:0,1:1"));
		QCOMPARE(pinsOf(portState(ch(1), ampAt(2)), QStringLiteral("out")).join(QLatin1Char(',')), QStringLiteral("4:0,5:1"));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		const QString wrong = checkOutputs(8, [](int c) {
			if (c >= 6) { return 1.f; }  // no device on 6/7 now
			if (c < 2) { return volumeOf(3) / 100.f; }  // amplifier 0 (unity), then the last one
			return volumeOf(c / 2) / 100.f;
		});
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("after one undo: ") + wrong));
	}

	//! A device with default pins stays on the pair: the speakers past it pass through untouched.
	void aStereoDeviceWithoutPinsLeavesSpeakersUntouched()
	{
		Mixer& mixer = *Engine::mixer();
		mixer.mixerChannel(1)->m_fxChain.clear();
		QVERIFY(format(1, QStringLiteral("5.1")));
		QVERIFY(format(2, QStringLiteral("5.1")));
		routeThroughTheBus(mixer);
		const QString amp = loadAmplifier(ch(1));
		QVERIFY(!amp.isEmpty());
		QVERIFY(ok(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), amp},
			{QStringLiteral("name"), QStringLiteral("Volume")}, {QStringLiteral("value"), 50}}));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		const QString wrong = checkOutputs(6, [](int c) { return c < 2 ? 0.5f : 1.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("stereo device on 5.1: ") + wrong));
	}

	void theWideInsertMixAllocatesNothing()
	{
		Mixer& mixer = *Engine::mixer();
		const QString bad = pinSpeakers(mixer, QStringLiteral("7.1"), 8);
		QVERIFY2(bad.isEmpty(), qPrintable(bad));
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
		const QString wrong = checkOutputs(8, [](int c) { return volumeOf(c / 2) / 100.f; });
		QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
	}

	//! Invalid input: '9.9' is not a speaker layout, so the command fails and the format is unchanged.
	void anInvalidFormatIsRefused()
	{
		Mixer& mixer = *Engine::mixer();
		const QString before = surround::formatName(mixer.mixerChannel(1)->m_surround.format());
		const ControlResult result = run(QStringLiteral("mixer.channel_format"),
			{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("format"), QStringLiteral("9.9")}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(surround::formatName(mixer.mixerChannel(1)->m_surround.format()), before);
	}

private:
	//! Channel 1 into channel 2 only; channel 2's direct out on outputs 1..N, the master beyond the device.
	static void routeThroughTheBus(Mixer& mixer)
	{
		mixer.deleteChannelSend(1, 0);
		mixer.deleteChannelSend(2, 0);
		mixer.createChannelSend(1, 2);
		mixer.mixerChannel(1)->m_volumeModel.setValue(1.f);
		mixer.mixerChannel(2)->m_volumeModel.setValue(1.f);
		mixer.mixerChannel(1)->m_panModel.setValue(0.f);
		mixer.mixerChannel(2)->m_panModel.setValue(0.f);
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("1/2")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("127/128")}}));
	}

	//! Channel 1's chain cleared, both channels on @a layout, routed, amplifier k on speakers 2k/2k+1.
	//! Returns the first failure as text (static, so it cannot use QVERIFY itself).
	static QString pinSpeakers(Mixer& mixer, const QString& layout, int channels)
	{
		mixer.mixerChannel(1)->m_fxChain.clear();
		if (!format(1, layout)) { return QStringLiteral("channel 1 did not take %1").arg(layout); }
		if (!format(2, layout)) { return QStringLiteral("channel 2 did not take %1").arg(layout); }
		routeThroughTheBus(mixer);
		for (int k = 0; k < channels / 2; ++k)
		{
			const QString amp = loadAmplifier(ch(1));
			if (amp.isEmpty()) { return QStringLiteral("no amplifier loaded at pair %1").arg(k); }
			if (!ok(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), amp},
				{QStringLiteral("name"), QStringLiteral("Volume")}, {QStringLiteral("value"), volumeOf(k)}}))
			{
				return QStringLiteral("Volume did not take on pair %1").arg(k);
			}
			const ControlResult set = setPins(ch(1), amp, onto(2 * k));
			if (!set.ok) { return amp + QStringLiteral(": ") + set.errorMessage; }
			const QJsonObject state = portState(ch(1), amp);
			const QString pair = QStringLiteral("%1:0,%2:1").arg(2 * k).arg(2 * k + 1);
			if (pinsOf(state, QStringLiteral("in")).join(QLatin1Char(',')) != pair) { return QStringLiteral("pair %1: in pins ").arg(k) + pinsOf(state, QStringLiteral("in")).join(QLatin1Char(',')); }
			if (pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')) != pair) { return QStringLiteral("pair %1: out pins ").arg(k) + pinsOf(state, QStringLiteral("out")).join(QLatin1Char(',')); }
			if (state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("channels")).toInt() != channels)
			{
				return QStringLiteral("pair %1: bus channels %2, expected %3").arg(k)
					.arg(state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("channels")).toInt()).arg(channels);
			}
			if (state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("format")).toString() != layout)
			{
				return QStringLiteral("pair %1: bus format ").arg(k) + state.value(QStringLiteral("bus")).toObject().value(QStringLiteral("format")).toString();
			}
		}
		return QString();
	}
};

QTEST_MAIN(SurroundDevicePathTest)
#include "SurroundDevicePathTest.moc"
