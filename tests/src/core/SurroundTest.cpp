/*
 * SurroundTest.cpp - surround channels, the surround panner and the downmix (0.5.0 F7)
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

/*! 0.5.0 F7 (MX-060). The engine runs an eight-output device that never starts: the test drives the
 *  periods itself, as HardwareIoTest does, and reads the hardware bus (HardwareOutputBus). Channel 1
 *  carries a constant 0.25 at fader 0.5 and channel 2 a varying signal; channel 3 is the surround bus.
 *  - a project that uses none of it renders and saves exactly as before - and a channel set to a
 *    surround format with nothing beyond L/R renders its stereo exactly;
 *  - channel 1 panned centre, front-left and rear-right into a 5.1 bus lands on C, L and Rs of the
 *    bus's six outputs, and nowhere else;
 *  - the downmix coefficients (ITU-R BS.775: centre and surrounds at -3 dB, LFE dropped unless kept,
 *    7.1's rear surrounds into 5.1's surrounds), and the 5.1 bus folded into the stereo master;
 *  - the master mix allocates nothing with a surround bus, a surround master and a direct out;
 *  - formats, panners and downmix options survive a save and load, and each command is one undo step. */

#include <QtTest>

#include <QDomDocument>
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

constexpr float Root = 0.70710678f;
//! Channel 1's mono sum (0.25) through its 0.5 fader.
constexpr float Panned = 0.125f;

//! A device with eight outputs that never runs: the periods are the test's.
class EightOutputs : public AudioDevice
{
public:
	explicit EightOutputs(AudioEngine* engine) : AudioDevice(8, engine) {}

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

HardwareOutputBus& bus()
{
	return Engine::audioEngine()->hardwareOutputs();
}

float varying(int f)
{
	return static_cast<float>(((f * 131) % 2048) - 1024) / 2048.f;
}

//! Feed channel 1 (constant 0.25) and channel 2 (varying) for one period.
void feed(Mixer* mixer)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> one(fpp, SampleFrame(0.25f, 0.25f));
	std::vector<SampleFrame> two(fpp);
	for (f_cnt_t f = 0; f < fpp; ++f) { two[f] = SampleFrame(varying(f), -varying(f)); }
	SampleFrame* a[1] = {one.data()};
	SampleFrame* b[1] = {two.data()};
	mixer->mixToChannel(AudioBus{a, 1, fpp}, 1);
	mixer->mixToChannel(AudioBus{b, 1, fpp}, 2);
}

//! One mixer period, then the bus flipped so it can be read.
std::vector<SampleFrame> period(Mixer* mixer)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	bus().swap();  // a clean write side
	feed(mixer);
	mixer->prepareMasterMix();
	zeroSampleFrames(out.data(), fpp);
	mixer->masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();
	bus().swap();  // the period just mixed is the one the device would be fed
	return out;
}

//! Device output @a channel's level on every frame of the period whose master is @a out.
bool outputAt(const std::vector<SampleFrame>& out, int channel, float level)
{
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(out.size()); ++f)
	{
		if (std::abs(bus().sample(channel, f, out[f]) - level) > 1e-5f) { return false; }
	}
	return true;
}

//! Outputs 1-6 (the 5.1 bus): @a level on @a speaker, silence on the five others.
bool onlySpeaker(const std::vector<SampleFrame>& out, int speaker, float level)
{
	for (int c = 0; c < 6; ++c)
	{
		if (!outputAt(out, c, c == speaker ? level : 0.f)) { return false; }
	}
	return true;
}

bool sameOutput(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t f = 0; f < a.size(); ++f)
	{
		if (a[f][0] != b[f][0] || a[f][1] != b[f][1]) { return false; }
	}
	return true;
}

bool pan(int channel, double x, double y)
{
	return ok(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), ch(channel)}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
}

bool format(int channel, const char* name)
{
	return ok(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(channel)}, {QStringLiteral("format"), QString::fromLatin1(name)}});
}

//! mixer.get_state's entry for @a id: its format, count and roles are what a reader sees.
QJsonObject mixerState(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("mixer.get_state")).result.value(QStringLiteral("channels")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject(); }
	}
	return {};
}

//! mixer.surround_get's entry for @a id: the specialized read stays in step with mixer.get_state.
QJsonObject surroundState(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("mixer.surround_get")).result.value(QStringLiteral("channels")).toArray())
	{
		if (value.toObject().value(QStringLiteral("channel")).toString() == id) { return value.toObject(); }
	}
	return {};
}

//! The role/index labels an entry publishes.
QStringList roleLabels(const QJsonObject& state)
{
	QStringList labels;
	for (const QJsonValue& value : state.value(QStringLiteral("channel_roles")).toArray()) { labels << value.toString(); }
	return labels;
}

} // namespace

class SurroundTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		AudioEngine* engine = Engine::audioEngine();
		engine->setAudioDevice(new EightOutputs(engine), false);  // stops and replaces the Dummy; nothing renders
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 3) { mixer->createChannel(); }
		mixer->mixerChannel(1)->m_volumeModel.setValue(0.5f);
		ControlRegistry::setReady(true);
		for (int i = 0; i < 3; ++i) { period(mixer); }  // let the fader ramps settle
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aValidFormatRejectsANullOutput()
	{
		QVERIFY(!surround::formatFromName(QStringLiteral("5.1"), nullptr));
	}

	void negativeHardwareChannelReadsAsSilence()
	{
		HardwareOutputBus outputs;
		outputs.init(2);
		const SampleFrame master(0.f, 0.f);
		QVERIFY(outputs.sample(-1, 0, master) == 0.f);
	}

	void aStereoProjectIsUnchanged()
	{
		Mixer* mixer = Engine::mixer();
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		mixer->saveSettings(doc, element);
		QVERIFY2(!doc.toString().contains(QStringLiteral("surround")), "a project that uses no surround saves none of it");

		const std::vector<SampleFrame> before = period(mixer);
		QVERIFY(!bus().active());
		QVERIFY(format(1, "5.1"));  // a surround channel fed L/R only: its stereo, bit for bit
		QVERIFY(pan(2, -1.0, -1.0));  // a panner into the stereo master does nothing
		QVERIFY(sameOutput(period(mixer), before));
		QVERIFY2(!bus().active(), "nothing reaches the hardware bus");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
		QVERIFY(!mixer->mixerChannel(2)->m_surround.pan().enabled);
		QVERIFY(sameOutput(period(mixer), before));
	}

	void monoAndDiscreteFormatsReadBack()
	{
		Mixer* mixer = Engine::mixer();
		// Mono: one M role, one channel, reported by both reads.
		QVERIFY(format(1, "mono"));
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Mono);
		QJsonObject state = mixerState(ch(1));
		QCOMPARE(state.value(QStringLiteral("format")).toString(), QStringLiteral("mono"));
		QCOMPARE(state.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(roleLabels(state), QStringList{QStringLiteral("M")});
		QJsonObject specialized = surroundState(ch(1));
		QCOMPARE(specialized.value(QStringLiteral("format")).toString(), QStringLiteral("mono"));
		QCOMPARE(specialized.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(roleLabels(specialized), QStringList{QStringLiteral("M")});
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step: stereo again
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
		QVERIFY(!mixerState(ch(1)).contains(QStringLiteral("format")));  // a stereo channel reads as it always did

		// Every discrete count: N channels, no speaker roles, indexed 1..N, and one undo step each.
		for (int channels = surround::MinDiscreteChannels; channels <= surround::MaxDiscreteChannels; ++channels)
		{
			const QString name = QStringLiteral("discrete-%1").arg(channels);
			const ControlResult reply = run(QStringLiteral("mixer.channel_format"),
				{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("format"), name}});
			QVERIFY2(reply.ok, qPrintable(name + QStringLiteral(": ") + reply.errorMessage));
			QCOMPARE(reply.result.value(QStringLiteral("format")).toString(), name);

			state = mixerState(ch(1));
			QCOMPARE(state.value(QStringLiteral("format")).toString(), name);
			QCOMPARE(state.value(QStringLiteral("channels")).toInt(), channels);
			QVERIFY(state.value(QStringLiteral("discrete")).toBool());
			QVERIFY2(state.value(QStringLiteral("speakers")).toArray().isEmpty(), "a discrete layout has speaker roles");
			const QStringList labels = roleLabels(state);
			QCOMPARE(labels.size(), channels);
			for (int c = 0; c < channels; ++c) { QCOMPARE(labels.at(c), QString::number(c + 1)); }

			QVERIFY(run(QStringLiteral("control.undo")).ok);
			QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
		}

		// The command schema advertises the canonical names; the parser also takes the aliases a
		// hand-edited project may spell.
		surround::Format parsed = surround::Format::Stereo;
		QVERIFY(surround::formatFromName(QStringLiteral("discrete5"), &parsed) && parsed == surround::Format::Discrete5);
		QVERIFY(surround::formatFromName(QStringLiteral("discrete 7"), &parsed) && parsed == surround::Format::Discrete7);
		QVERIFY(!surround::formatFromName(QStringLiteral("discrete-1"), &parsed));
		QVERIFY(!surround::formatFromName(QStringLiteral("discrete-9"), &parsed));
		QCOMPARE(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("format"), QStringLiteral("discrete-9")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(mixer->mixerChannel(1)->m_surround.format(), surround::Format::Stereo);
	}

	void discreteChannelsReachTheirOwnOutputs()
	{
		Mixer* mixer = Engine::mixer();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		HardwareOutputBus outputs;
		outputs.init(frames);
		MixerChannel* channel = mixer->mixerChannel(3);
		channel->m_volumeModel.setValue(1.f);
		channel->m_panModel.setValue(0.f);
		for (int channels = surround::MinDiscreteChannels; channels <= surround::MaxDiscreteChannels; ++channels)
		{
			const QString name = QStringLiteral("discrete-%1").arg(channels);
			QVERIFY2(ok(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(3)}, {QStringLiteral("format"), name}}), qPrintable(name));
			ChannelSurround& side = channel->m_surround;
			std::vector<SampleFrame> pair(frames, SampleFrame(0.f, 0.f));
			for (int c = 0; c < channels; ++c)
			{
				const float level = 0.1f * static_cast<float>(c + 1);
				if (c < 2) { for (f_cnt_t f = 0; f < frames; ++f) { pair[f][c] = level; } }
				else { for (f_cnt_t f = 0; f < frames; ++f) { side.plane(c)[f] = level; } }
			}
			side.markWritten();
			outputs.swap();  // a clean write side
			surround::directOut(outputs, *channel, 0, 2, pair.data(), frames);
			outputs.add(0, 2, pair.data(), frames);
			outputs.swap();
			// Output i carries channel i and nothing else: no speaker reordering, folding or leakage.
			for (int c = 0; c < surround::MaxChannels; ++c)
			{
				const float expected = c < channels ? 0.1f * static_cast<float>(c + 1) : 0.f;
				for (f_cnt_t f = 0; f < frames; ++f)
				{
					const float seen = outputs.sample(c, f, SampleFrame{});
					QVERIFY2(std::abs(seen - expected) < 1e-5f, qPrintable(QStringLiteral("%1 channel %2 frame %3: %4, expected %5")
						.arg(name).arg(c).arg(f).arg(seen).arg(expected)));
				}
			}
			side.clearPeriod();
		}
		// The matrices agree: discrete is channel-for-channel, never speaker-folded.
		const surround::Matrix& toNamed = surround::matrix(surround::Format::Discrete6, surround::Format::Surround51);
		QCOMPARE(toNamed[0][0], 1.f);
		QCOMPARE(toNamed[4][4], 1.f);
		QCOMPARE(toNamed[4][2], 0.f);  // channel 2 does not become a surround speaker
		const surround::Matrix& toMono = surround::matrix(surround::Format::Discrete8, surround::Format::Mono);
		QCOMPARE(toMono[0][0], 1.f);
		QCOMPARE(toMono[0][1], 0.f);  // discrete is index-addressed, not summed
		channel->m_surround.reset();
	}

	void aPannedChannelLandsOnItsSpeakers()
	{
		Mixer* mixer = Engine::mixer();
		route51Bus(*mixer);
		QVERIFY(pan(1, 0.0, 1.0));
		QVERIFY(onlySpeaker(period(mixer), 2, Panned));  // centre: C, output 3
		QVERIFY(pan(1, -1.0, 1.0));
		QVERIFY(onlySpeaker(period(mixer), 0, Panned));  // front-left: L, output 1
		QVERIFY(pan(1, 1.0, -1.0));
		QVERIFY(onlySpeaker(period(mixer), 5, Panned));  // rear-right: Rs, output 6
		QVERIFY(pan(1, 0.0, -1.0));  // between the surrounds: both at -3 dB, constant power
		const std::vector<SampleFrame> back = period(mixer);
		QVERIFY(outputAt(back, 4, Panned * Root) && outputAt(back, 5, Panned * Root) && outputAt(back, 2, 0.f));
	}

	void aSideChannelPanFollowsItsValueBuffer()
	{
		Mixer* mixer = Engine::mixer();
		route51Bus(*mixer);
		QVERIFY(pan(1, 1.0, -1.0));  // a steady signal on the bus's right surround
		FloatModel& sidePan = mixer->mixerChannel(3)->m_panModel;
		sidePan.setValue(0.f);
		period(mixer);  // settle the centred value before moving it

		sidePan.setValue(-1.f);
		const ValueBuffer* panValues = sidePan.valueBuffer();
		QVERIFY(panValues != nullptr);
		const std::vector<SampleFrame> moving = period(mixer);
		int mismatches = 0;
		for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(moving.size()); ++f)
		{
			const float expected = Panned * (1.f + panValues->values()[f]);
			mismatches += std::abs(bus().sample(5, f, moving[f]) - expected) > 1e-5f;
		}
		QCOMPARE(mismatches, 0);
		QVERIFY(bus().sample(5, 0, moving[0]) > bus().sample(5, moving.size() - 1, moving.back()));

		sidePan.setValue(0.f);
		period(mixer);
	}

	void aSurroundPannedSendWakesOnlyItsNonzeroStereoChannel()
	{
		Mixer* mixer = Engine::mixer();
		route51Bus(*mixer);
		QVERIFY(pan(1, -1.0, 1.0));

		MixerRoute* route = nullptr;
		for (MixerRoute* candidate : mixer->mixerChannel(1)->m_sends)
		{
			if (candidate->receiverIndex() == 3) { route = candidate; }
		}
		QVERIFY(route != nullptr);

		std::bitset<MaxTrackChannels>& quiet = mixer->mixerChannel(3)->m_bus.quietChannels();
		quiet.set(0);
		quiet.set(1);
		const SampleFrame src(0.25f, 0.25f);
		surround::panSend(*route, &src, 1);

		const float left = mixer->mixerChannel(3)->m_buffer[0][0];
		const float right = mixer->mixerChannel(3)->m_buffer[0][1];
		const bool leftAwake = !quiet[0];
		const bool rightQuiet = quiet[1];
		period(mixer);  // clear the direct probe before the following test uses the mixer
		QVERIFY(left > 0.f);
		QCOMPARE(right, 0.f);
		QVERIFY(leftAwake);
		QVERIFY(rightQuiet);
	}

	void theSurroundPannerExplainsWhichSourcesItAffects()
	{
		Mixer* mixer = Engine::mixer();
		route51Bus(*mixer);
		const ControlResult stereoSendReply = run(QStringLiteral("mixer.surround_pan"),
			{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("x"), -0.25}, {QStringLiteral("y"), 0.25}});
		QVERIFY(format(1, "5.1"));
		const ControlResult surroundSendReply = run(QStringLiteral("mixer.surround_pan"),
			{{QStringLiteral("channel"), ch(1)}, {QStringLiteral("x"), 0.5}, {QStringLiteral("y"), -0.5}});
		const ControlResult surroundNoSendReply = run(QStringLiteral("mixer.surround_pan"),
			{{QStringLiteral("channel"), ch(3)}, {QStringLiteral("x"), -0.25}, {QStringLiteral("y"), 0.25}});
		const ControlResult stereoNoSendReply = run(QStringLiteral("mixer.surround_pan"),
			{{QStringLiteral("channel"), ch(2)}, {QStringLiteral("x"), -0.25}, {QStringLiteral("y"), 0.25}});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(stereoSendReply.ok);
		QCOMPARE(stereoSendReply.result.value(QStringLiteral("surround_sends")).toArray().size(), 1);
		QVERIFY2(!stereoSendReply.result.contains(QStringLiteral("note")), "a stereo source with a surround send was warned");
		QVERIFY(surroundSendReply.ok);
		QCOMPARE(surroundSendReply.result.value(QStringLiteral("surround_sends")).toArray().size(), 1);
		QVERIFY(surroundSendReply.result.value(QStringLiteral("note")).toString().contains(
			QStringLiteral("does not pan sends from a surround-format channel")));
		QVERIFY(surroundNoSendReply.ok);
		QVERIFY(surroundNoSendReply.result.value(QStringLiteral("surround_sends")).toArray().isEmpty());
		QVERIFY(surroundNoSendReply.result.value(QStringLiteral("note")).toString().contains(
			QStringLiteral("does not pan sends from a surround-format channel")));
		QVERIFY(stereoNoSendReply.ok);
		QVERIFY(stereoNoSendReply.result.value(QStringLiteral("note")).toString().contains(QStringLiteral("this channel has none")));
	}

	void theDownmixFollowsBs775()
	{
		using surround::Format;
		const surround::Matrix& m = surround::matrix(Format::Surround51, Format::Stereo);
		QCOMPARE(m[0][0], 1.f);
		QCOMPARE(m[1][1], 1.f);
		QVERIFY(std::abs(m[0][2] - Root) < 1e-6f && std::abs(m[1][2] - Root) < 1e-6f);  // C into both at -3 dB
		QCOMPARE(m[0][3], 0.f);  // the LFE dropped
		QCOMPARE(m[1][3], 0.f);
		QVERIFY(std::abs(m[0][4] - Root) < 1e-6f && m[1][4] == 0.f);  // Ls into L
		QVERIFY(std::abs(m[1][5] - Root) < 1e-6f && m[0][5] == 0.f);  // Rs into R
		const surround::Matrix& seven = surround::matrix(Format::Surround71, Format::Surround51);
		QCOMPARE(seven[4][4], 1.f);  // 7.1's rear Lrs into 5.1's Ls
		QCOMPARE(seven[4][6], 1.f);  // 7.1's side Ls into 5.1's Ls
		QCOMPARE(seven[3][3], 1.f);  // the LFE carried where there is one
		const surround::Matrix& seven2 = surround::matrix(Format::Surround71, Format::Stereo);
		QVERIFY(std::abs(seven2[0][4] - Root) < 1e-6f && std::abs(seven2[0][6] - Root) < 1e-6f);

		// The 5.1 bus (still sending to the master) folds into the stereo master: C at -3 dB on both sides.
		Mixer* mixer = Engine::mixer();
		QVERIFY(ok(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("x"), 0.0},
			{QStringLiteral("y"), 1.0}, {QStringLiteral("lfe"), 1.0}}));
		std::vector<SampleFrame> out = period(mixer);
		QVERIFY(std::abs(out[0][0] - Panned * Root) < 1e-5f && std::abs(out[0][1] - Panned * Root) < 1e-5f);
		QVERIFY(outputAt(out, 3, Panned));  // the LFE output itself carries the send
		QVERIFY(ok(QStringLiteral("mixer.downmix_options"), {{QStringLiteral("lfe"), true}, {QStringLiteral("lfe_db"), 0.0}}));
		QCOMPARE(surround::matrix(Format::Surround51, Format::Stereo)[0][3], 1.f);
		out = period(mixer);
		QVERIFY(std::abs(out[0][0] - Panned * (Root + 1.f)) < 1e-5f);  // the LFE kept, at 0 dB
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(surround::matrix(Format::Surround51, Format::Stereo)[0][3], 0.f);
	}

	void theMasterMixAllocatesNothing()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(format(0, "5.1"));  // a surround master: its own side channels onto outputs 3-6
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("1/2")}}));
		QCOMPARE(countedMasterMix(*mixer), static_cast<qulonglong>(0));
		QVERIFY(format(0, "stereo"));  // the folding path
		QCOMPARE(countedMasterMix(*mixer), static_cast<qulonglong>(0));
	}

	void everythingSurvivesASaveAndLoad()
	{
		Mixer* mixer = Engine::mixer();
		QVERIFY(format(3, "7.1"));
		QVERIFY(ok(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("x"), -0.5},
			{QStringLiteral("y"), 0.25}, {QStringLiteral("lfe"), 0.3}, {QStringLiteral("spread"), 0.2}}));
		QVERIFY(ok(QStringLiteral("mixer.downmix_options"), {{QStringLiteral("center_db"), -4.5}, {QStringLiteral("surround_db"), -6.0},
			{QStringLiteral("lfe"), true}, {QStringLiteral("lfe_db"), -10.0}}));
		const surround::DownmixOptions options = surround::downmixOptions();
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixer"));
		doc.appendChild(element);
		mixer->saveSettings(doc, element);
		mixer->clear();
		QVERIFY(surround::downmixOptions() == surround::DownmixOptions{});
		mixer->loadSettings(element);

		QCOMPARE(mixer->mixerChannel(3)->m_surround.format(), surround::Format::Surround71);
		const surround::PanPosition loaded = mixer->mixerChannel(1)->m_surround.pan();
		QVERIFY(loaded.enabled && std::abs(loaded.x + 0.5f) < 1e-6f && std::abs(loaded.y - 0.25f) < 1e-6f);
		QVERIFY(std::abs(loaded.lfe - 0.3f) < 1e-6f && std::abs(loaded.spread - 0.2f) < 1e-6f);
		const surround::DownmixOptions read = surround::downmixOptions();
		QVERIFY(std::abs(read.centre - options.centre) < 1e-6f && std::abs(read.surround - options.surround) < 1e-6f);
		QVERIFY(read.lfeOn && std::abs(read.lfe - options.lfe) < 1e-6f);
		QVERIFY(std::abs(read.centre - std::pow(10.f, -4.5f / 20.f)) < 1e-5f);

		QVERIFY(format(3, "quad"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step back: 7.1 again
		QCOMPARE(mixer->mixerChannel(3)->m_surround.format(), surround::Format::Surround71);
		QCOMPARE(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(3)}, {QStringLiteral("format"), QStringLiteral("9.1")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		mixer->clear();
	}

	void monoAndDiscreteLayoutsSurviveASaveAndLoad()
	{
		Mixer* mixer = Engine::mixer();
		mixer->clear();
		while (mixer->numChannels() <= 1) { mixer->createChannel(); }
		const QStringList names{QStringLiteral("mono"), QStringLiteral("discrete-2"), QStringLiteral("discrete-3"),
			QStringLiteral("discrete-4"), QStringLiteral("discrete-5"), QStringLiteral("discrete-6"),
			QStringLiteral("discrete-7"), QStringLiteral("discrete-8")};
		for (const QString& name : names)
		{
			const QString id = control::channelIdOf(mixer->mixerChannel(1));
			QVERIFY2(ok(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), id}, {QStringLiteral("format"), name}}), qPrintable(name));
			surround::Format expected = surround::Format::Stereo;
			QVERIFY(surround::formatFromName(name, &expected));
			const int channels = surround::channelCount(expected);

			QDomDocument doc;
			QDomElement element = doc.createElement(QStringLiteral("mixer"));
			doc.appendChild(element);
			mixer->saveSettings(doc, element);
			QVERIFY2(doc.toString().contains(QStringLiteral("format=\"%1\"").arg(name)), qPrintable(name + QStringLiteral(" was not written")));

			mixer->clear();
			mixer->loadSettings(element);
			MixerChannel* loaded = mixer->mixerChannel(1);
			QCOMPARE(loaded->m_surround.format(), expected);
			QCOMPARE(surround::formatName(loaded->m_surround.format()), name);
			QCOMPARE(surround::channelCount(loaded->m_surround.format()), channels);
			QJsonObject state = mixerState(control::channelIdOf(loaded));
			QCOMPARE(state.value(QStringLiteral("format")).toString(), name);
			QCOMPARE(state.value(QStringLiteral("channels")).toInt(), channels);
			QCOMPARE(roleLabels(state).size(), channels);
		}

		mixer->clear();
	}

	void nonFinitePannerAttributesFallBackToTheDefaults()
	{
		// A <surround> element whose panner values parse as NaN or infinity loads the panner's defaults:
		// NaN in the position or spread would make every gain of every format NaN.
		QDomDocument doc;
		QDomElement channelElement = doc.createElement(QStringLiteral("channel"));
		doc.appendChild(channelElement);
		QDomElement panElement = doc.createElement(QStringLiteral("surround"));
		panElement.setAttribute(QStringLiteral("format"), QStringLiteral("5.1"));
		panElement.setAttribute(QStringLiteral("pan"), QStringLiteral("1"));
		panElement.setAttribute(QStringLiteral("x"), QStringLiteral("nan"));
		panElement.setAttribute(QStringLiteral("y"), QStringLiteral("inf"));
		panElement.setAttribute(QStringLiteral("lfe"), QStringLiteral("-inf"));
		panElement.setAttribute(QStringLiteral("spread"), QStringLiteral("nan"));
		channelElement.appendChild(panElement);

		ChannelSurround side;
		side.init(64);
		side.load(channelElement);

		const surround::PanPosition defaults{};
		QVERIFY(side.pan().enabled);
		QCOMPARE(side.pan().x, defaults.x);
		QCOMPARE(side.pan().y, defaults.y);
		QCOMPARE(side.pan().lfe, defaults.lfe);
		QCOMPARE(side.pan().spread, defaults.spread);
		for (int k = 0; k < surround::FormatCount; ++k)
		{
			for (const float gain : side.gains(static_cast<surround::Format>(k))) { QVERIFY(std::isfinite(gain)); }
		}
	}

private:
	//! Channel 1 into channel 3, a 5.1 bus on outputs 1-6; the master moved to 7/8 out of its way, and
	//! carrying only what the bus sends it (channel 2 no longer sends to it).
	static void route51Bus(Mixer& mixer)
	{
		mixer.deleteChannelSend(1, 0);
		mixer.deleteChannelSend(2, 0);
		mixer.createChannelSend(1, 3);
		QVERIFY(format(3, "5.1"));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(3)}, {QStringLiteral("output"), QStringLiteral("1/2")}}));
		QVERIFY(ok(QStringLiteral("mixer.hardware_output"), {{QStringLiteral("channel"), ch(0)}, {QStringLiteral("output"), QStringLiteral("7/8")}}));
		for (int i = 0; i < 2; ++i) { period(&mixer); }  // the new send's level settles
	}

	//! The allocations of one master mix, its channels fed first.
	static qulonglong countedMasterMix(Mixer& mixer)
	{
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(fpp);
		bus().swap();
		feed(&mixer);
		mixer.prepareMasterMix();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		mixer.masterMix(out.data());
		bus().swap();
		test::tlCountAllocations = false;
		AutomatableModel::incrementPeriodCounter();
		return static_cast<qulonglong>(test::tlAllocationCount);
	}
};

QTEST_MAIN(SurroundTest)
#include "SurroundTest.moc"
