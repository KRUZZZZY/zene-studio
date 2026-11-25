/*
 * WideDiscreteBusTest.cpp - discrete mixer buses through 64 channels (Q050-155.1)
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

/*! Q050-155.1. The engine runs a 128-output device that never starts: the test drives the periods itself,
 *  as SurroundTest does, and reads the hardware bus. Channel 1 is the source, channel 2 the discrete bus
 *  whose direct out is outputs 1..N; the master is moved to outputs 127/128 and nothing sends to it.
 *  - mixer.channel_format takes discrete-9 .. discrete-64 and refuses 65; mixer.get_state and
 *    mixer.surround_get read each back with its count and index labels; each edit is one undo step;
 *  - a save and a load keep a wide discrete layout's format and count;
 *  - 64 distinct constants on the source's channels cross a discrete-64 bus and reach outputs 1..64 in
 *    order, at the route's send level, and nowhere else; a narrower receiver drops the channels it
 *    lacks, and a narrower sender leaves the bus's other channels silent;
 *  - the master mix with that graph allocates nothing. */

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
#include "Engine.h"
#include "HardwareOutputBus.h"
#include "Mixer.h"
#include "MixerSurround.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

constexpr int Outputs = 128;

//! A device with 128 outputs that never runs: the periods are the test's.
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

//! Channel @a c's own constant: every channel distinct, none zero.
float level(int c)
{
	return static_cast<float>(c + 1) / 128.f;
}

//! The source (channel 1): level(0) / level(1) on its pair, level(c) on side channel c of its format.
void feedSource(Mixer& mixer)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> pair(fpp, SampleFrame(level(0), level(1)));
	SampleFrame* buffers[1] = {pair.data()};
	mixer.mixToChannel(AudioBus{buffers, 1, fpp}, 1);
	ChannelSurround& side = mixer.mixerChannel(1)->m_surround;
	for (int c = 2; c < surround::channelCount(side.format()); ++c)
	{
		std::fill_n(side.plane(c), fpp, level(c));
	}
	side.markWritten();
}

//! One mixer period with the source fed, then the bus flipped so it can be read.
void period(Mixer& mixer)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	bus().swap();  // a clean write side
	feedSource(mixer);
	mixer.prepareMasterMix();
	zeroSampleFrames(out.data(), fpp);
	mixer.masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();
	bus().swap();  // the period just mixed is the one the device would be fed
}

//! A master speaker's own level: distinct per channel, none zero.
float masterSideLevel(int c)
{
	return 0.1f * static_cast<float>(c + 1);
}

//! Channel @a index's side channels (2 ..) at their own level, then one mixer period.
void sidesPeriod(Mixer& mixer, int index)
{
	ChannelSurround& sides = mixer.mixerChannel(index)->m_surround;
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	for (int c = 2; c < surround::channelCount(sides.format()); ++c) { std::fill_n(sides.plane(c), fpp, masterSideLevel(c)); }
	sides.markWritten();
	period(mixer);
}

//! A new project's routing: the master alone, then three channels for the feed.
void clearedMixer(Mixer& mixer)
{
	mixer.clear();
	while (mixer.numChannels() <= 3) { mixer.createChannel(); }
}

//! Output @a channel's value on every frame of the period just read, or NaN when it is not constant.
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

/*! Every output: outputs 0 .. @a carried - 1 hold level(c) * @a gain, all the others silence. A message
 *  names the first output that differs (a swap, a fold or a leak). */
QString checkOutputs(int carried, float gain)
{
	for (int c = 0; c < Outputs; ++c)
	{
		const float expected = c < carried ? level(c) * gain : 0.f;
		const float seen = outputLevel(c);
		if (!(std::abs(seen - expected) < 1e-5f))
		{
			return QStringLiteral("output %1: %2, expected %3").arg(c + 1).arg(seen).arg(expected);
		}
	}
	return QString();
}

//! Every device output of the period just mixed, read at once: the bus serves one period at a time.
std::vector<float> outputsOf(int channels)
{
	std::vector<float> levels;
	for (int c = 0; c < channels; ++c) { levels.push_back(outputLevel(c)); }
	return levels;
}

QJsonObject entryFor(const QString& command, const QString& key, const QString& id)
{
	for (const QJsonValue& value : run(command).result.value(QStringLiteral("channels")).toArray())
	{
		if (value.toObject().value(key).toString() == id) { return value.toObject(); }
	}
	return {};
}

QJsonObject mixerState(const QString& id)
{
	return entryFor(QStringLiteral("mixer.get_state"), QStringLiteral("id"), id);
}

QJsonObject surroundState(const QString& id)
{
	return entryFor(QStringLiteral("mixer.surround_get"), QStringLiteral("channel"), id);
}

//! Whether @a state reads as an N-channel discrete layout: its name, count, no speakers, labels 1..N.
QString discreteReadback(const QJsonObject& state, int channels)
{
	const QString name = QStringLiteral("discrete-%1").arg(channels);
	if (state.value(QStringLiteral("format")).toString() != name) { return QStringLiteral("format is not ") + name; }
	if (state.value(QStringLiteral("channels")).toInt() != channels) { return QStringLiteral("wrong channel count"); }
	if (!state.value(QStringLiteral("discrete")).toBool()) { return QStringLiteral("not reported discrete"); }
	if (!state.value(QStringLiteral("speakers")).toArray().isEmpty()) { return QStringLiteral("has speaker roles"); }
	const QJsonArray labels = state.value(QStringLiteral("channel_roles")).toArray();
	if (labels.size() != channels) { return QStringLiteral("%1 channel labels").arg(labels.size()); }
	for (int c = 0; c < channels; ++c)
	{
		if (labels.at(c).toString() != QString::number(c + 1)) { return QStringLiteral("label %1 out of order").arg(c); }
	}
	return QString();
}

} // namespace

class WideDiscreteBusTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		AudioEngine* engine = Engine::audioEngine();
		engine->setAudioDevice(new WideOutputs(engine), false);  // stops and replaces the Dummy; nothing renders
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 2) { mixer->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void wideDiscreteFormatsReadBackAndUndoInOneStep()
	{
		Mixer* mixer = Engine::mixer();
		for (const int channels : {9, 16, 33, 64})
		{
			const QString name = QStringLiteral("discrete-%1").arg(channels);
			const ControlResult reply = run(QStringLiteral("mixer.channel_format"),
				{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("format"), name}});
			QVERIFY2(reply.ok, qPrintable(name + QStringLiteral(": ") + reply.errorMessage));
			QCOMPARE(reply.result.value(QStringLiteral("format")).toString(), name);
			QCOMPARE(surround::channelCount(mixer->mixerChannel(1)->m_surround.format()), channels);
			const QString general = discreteReadback(mixerState(ch(1)), channels);
			QVERIFY2(general.isEmpty(), qPrintable(name + QStringLiteral(" mixer.get_state: ") + general));
			const QString specialized = discreteReadback(surroundState(ch(1)), channels);
			QVERIFY2(specialized.isEmpty(), qPrintable(name + QStringLiteral(" mixer.surround_get: ") + specialized));

			QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step: stereo again
			QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
			QVERIFY(!mixerState(ch(1)).contains(QStringLiteral("format")));
		}
		QCOMPARE(surround::discreteChannels(surround::discreteFormat(64)), 64);
		QCOMPARE(surround::formatNames().size(), surround::FormatCount);
		QVERIFY(surround::formatNames().contains(QStringLiteral("discrete-64")));
		for (const char* wrong : {"discrete-65", "discrete-1", "discrete-128"})
		{
			QCOMPARE(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(1)},
				{QStringLiteral("format"), QString::fromLatin1(wrong)}}).errorKind, ControlErrorKind::InvalidArgs);
		}
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
	}

	void wideDiscreteLayoutsSurviveASaveAndLoad()
	{
		Mixer* mixer = Engine::mixer();
		for (const int channels : {9, 24, 64})
		{
			const QString name = QStringLiteral("discrete-%1").arg(channels);
			QVERIFY(format(1, name));
			QDomDocument doc;
			QDomElement element = doc.createElement(QStringLiteral("mixer"));
			doc.appendChild(element);
			mixer->saveSettings(doc, element);
			QVERIFY2(doc.toString().contains(QStringLiteral("format=\"%1\"").arg(name)), qPrintable(name + QStringLiteral(" was not written")));

			mixer->clear();
			QCOMPARE(mixer->numChannels(), 1);
			mixer->loadSettings(element);
			MixerChannel* loaded = mixer->mixerChannel(1);
			QCOMPARE(surround::formatName(loaded->m_surround.format()), name);
			QCOMPARE(surround::channelCount(loaded->m_surround.format()), channels);
			const QString read = discreteReadback(mixerState(control::channelIdOf(loaded)), channels);
			QVERIFY2(read.isEmpty(), qPrintable(name + QStringLiteral(" after the load: ") + read));
		}
		mixer->clear();
		while (mixer->numChannels() <= 2) { mixer->createChannel(); }
	}

	void distinctChannelsCrossADiscrete64BusInOrder()
	{
		Mixer& mixer = *Engine::mixer();
		routeThroughTheBus(mixer);
		QVERIFY(format(1, QStringLiteral("discrete-64")));
		QVERIFY(format(2, QStringLiteral("discrete-64")));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		QString wrong = checkOutputs(64, 1.f);
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("64 into 64: ") + wrong));

		// The route's send level applies to every channel alike.
		QVERIFY(ok(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("to"), ch(2)}, {QStringLiteral("amount"), 0.5}}));
		for (int i = 0; i < 3; ++i) { period(mixer); }
		wrong = checkOutputs(64, 0.5f);
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("64 into 64 at half: ") + wrong));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		for (int i = 0; i < 3; ++i) { period(mixer); }

		// A narrower receiver keeps channels 1..16 and drops the rest: nothing folds into them.
		QVERIFY(format(2, QStringLiteral("discrete-16")));
		period(mixer);
		wrong = checkOutputs(16, 1.f);
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("64 into 16: ") + wrong));

		// A narrower sender fills only its own channels of the wide bus.
		QVERIFY(format(2, QStringLiteral("discrete-64")));
		QVERIFY(format(1, QStringLiteral("discrete-12")));
		period(mixer);
		wrong = checkOutputs(12, 1.f);
		QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("12 into 64: ") + wrong));
	}

	void theWideMasterMixAllocatesNothing()
	{
		Mixer& mixer = *Engine::mixer();
		QVERIFY(format(1, QStringLiteral("discrete-64")));
		QVERIFY(format(2, QStringLiteral("discrete-64")));
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
		const QString wrong = checkOutputs(64, 1.f);
		QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
	}

	/*! A surround master's speakers after its pair are the same master signal: the master fader, then the
	 *  engine's master gain, which `AudioEngine::renderStageMix()` applies to the pair after the master mix
	 *  (`src/core/AudioEngine.cpp:1186`; docs/SURROUND-EXPORT.md:27 states it for the export tap). Lowering
	 *  the gain must lower outputs 3..N with outputs 1/2, not leave them at the fader's level. */
	void theWideMasterOutputFollowsTheMasterGain()
	{
		Mixer& mixer = *Engine::mixer();
		AudioEngine* engine = Engine::audioEngine();
		clearedMixer(mixer);
		QVERIFY(format(0, QStringLiteral("5.1")));  // the master's own speakers, on outputs 3-6
		engine->setMasterGain(1.f);
		for (int i = 0; i < 2; ++i) { sidesPeriod(mixer, 0); }  // the new format settles
		const std::vector<float> loud = outputsOf(8);  // read before the next period is mixed
		engine->setMasterGain(0.25f);
		sidesPeriod(mixer, 0);
		const std::vector<float> soft = outputsOf(8);
		engine->setMasterGain(1.f);
		QStringList wrong;
		for (int c = 2; c < 6; ++c)
		{
			const auto at = static_cast<std::size_t>(c);
			if (!(loud[at] > 0.01f) || !(std::abs(soft[at] - loud[at] * 0.25f) < 1e-5f))
			{
				wrong << QStringLiteral("output %1: %2 at unity, %3 at a quarter").arg(c + 1).arg(loud[at]).arg(soft[at]);
			}
		}
		QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("; "))));
		QVERIFY(loud[6] == 0.f && loud[7] == 0.f);  // nothing beyond the master's six speakers
		QVERIFY(format(0, QStringLiteral("stereo")));
	}

	/*! A channel's direct out is a tap of that channel, before the master: the engine's master gain must
	 *  not scale its side planes. Only the master's own wide output follows the gain, because the pair it
	 *  plays after does. */
	void aChannelDirectOutIgnoresTheMasterGain()
	{
		Mixer& mixer = *Engine::mixer();
		AudioEngine* engine = Engine::audioEngine();
		clearedMixer(mixer);
		QVERIFY(format(2, QStringLiteral("5.1")));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("output"), QStringLiteral("1/2")}}));
		mixer.mixerChannel(2)->m_volumeModel.setValue(1.f);
		engine->setMasterGain(1.f);
		for (int i = 0; i < 2; ++i) { sidesPeriod(mixer, 2); }
		const std::vector<float> loud = outputsOf(8);
		engine->setMasterGain(0.25f);
		sidesPeriod(mixer, 2);
		const std::vector<float> soft = outputsOf(8);
		engine->setMasterGain(1.f);
		QStringList wrong;
		for (int c = 2; c < 6; ++c)
		{
			const auto at = static_cast<std::size_t>(c);
			if (!(loud[at] > 0.01f) || !(std::abs(soft[at] - loud[at]) < 1e-5f))
			{
				wrong << QStringLiteral("output %1: %2 at unity, %3 at a quarter").arg(c + 1).arg(loud[at]).arg(soft[at]);
			}
		}
		QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("; "))));
		QVERIFY(format(2, QStringLiteral("stereo")));
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

QTEST_MAIN(WideDiscreteBusTest)
#include "WideDiscreteBusTest.moc"
