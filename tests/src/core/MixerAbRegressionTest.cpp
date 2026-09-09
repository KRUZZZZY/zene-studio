/*
 * MixerAbRegressionTest.cpp - D1 byte-identical A/B render regression
 *
 * Copyright (c) 2026 LMMS developers
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

//! Phase D, decision D1: moving the channel volume multiply after the send
//! loop must not change the rendered output of a project that uses no bus
//! sends. This test renders a deterministic multi-channel graph (no bus
//! sends) through Mixer::masterMix() and compares the raw interleaved
//! float output byte-for-byte against a reference render that was captured
//! and committed before the reorder was applied.
//!
//! Evidence is printed unconditionally (stdout, flushed) so the SHA-256 of
//! both renders can be pasted into PART-D-SIDECHAIN.md:
//!
//!   LMMS_AB_RENDER_OUT=/tmp/ab-before.raw ./MixerAbRegressionTest
//!
//! The reference file lives in tests/reference/mixer-ab-render.raw and is
//! regenerated deliberately (never automatically).

#include <QtTest>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <cstdio>
#include <vector>

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "SampleFrame.h"

#ifndef MIXER_AB_REFERENCE_FILE
#define MIXER_AB_REFERENCE_FILE "tests/reference/mixer-ab-render.raw"
#endif

namespace lmms
{

namespace
{

//! Deterministic asymmetric gain effect (L *= 1.5, R *= 0.5). Exercises the
//! FX-chain path of the render without depending on any real plugin being
//! loadable in the headless test environment.
class AbGainEffect : public Effect
{
public:
	AbGainEffect(Model* parent) :
		Effect{&s_descriptor, parent, nullptr}
	{
	}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			buf[f][0] *= 1.5f;
			buf[f][1] *= 0.5f;
		}
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor AbGainEffect::s_descriptor
{
	"abgainregressiontest",
	"AB regression gain effect",
	"Deterministic asymmetric gain used by MixerAbRegressionTest",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

//! Number of periods rendered. 32 * 256 frames = 8192 frames = 65536 bytes.
constexpr int kPeriods = 32;
//! Master + 8 channels.
constexpr int kChannels = 9;

//! Deterministic signal in [-0.5, 0.5). Integer arithmetic only (no libm),
//! so the exact bit pattern is platform-independent.
inline float abSignal(int period, int frame, int channel, int side)
{
	const int n = period * 7919 + frame * 131 + channel * 17 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

QByteArray sha256Hex(const QByteArray& data)
{
	return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

void printEvidence(const char* label, const QByteArray& data)
{
	std::fprintf(stdout, "AB_EVIDENCE %s bytes=%d sha256=%s\n",
		label, static_cast<int>(data.size()), sha256Hex(data).constData());
	std::fflush(stdout);
}

} // namespace

} // namespace lmms

using namespace lmms;

class MixerAbRegressionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device thread renders in the background; this test drives
		// the mixer synchronously, so stop it to keep the buffers stable.
		Engine::audioEngine()->audioDev()->stopProcessing();
		// A fixed 256-frame period keeps the reference render valid no matter
		// what the host config says.
		QCOMPARE(Engine::audioEngine()->framesPerPeriod(), static_cast<f_cnt_t>(256));

		buildGraph();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! Render twice and require identical bytes (determinism), then compare
	//! against the committed pre-change reference render byte-for-byte.
	void renderIsByteIdenticalToReference()
	{
		const QByteArray first = render();
		const QByteArray second = render();

		QVERIFY2(first.size() == kPeriods * 256 * static_cast<int>(sizeof(SampleFrame)),
			"unexpected render size");
		QVERIFY2(first != QByteArray(first.size(), '\0'),
			"render is silent - the graph did not produce any audio");
		QCOMPARE(second, first);

		printEvidence("render", first);

		const QString outPath = qEnvironmentVariable("LMMS_AB_RENDER_OUT");
		if (!outPath.isEmpty())
		{
			QFile out{outPath};
			QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
			QCOMPARE(out.write(first), static_cast<qint64>(first.size()));
			out.close();
		}

		const QString refPath = QString::fromUtf8(MIXER_AB_REFERENCE_FILE);
		if (!QFile::exists(refPath))
		{
			// Bootstrap mode: the reference is captured and committed with the
			// harness, before any Phase D code change. Never reached once the
			// reference is in the tree.
			std::fprintf(stdout,
				"AB_EVIDENCE reference MISSING at %s (bootstrap capture)\n",
				refPath.toUtf8().constData());
			std::fflush(stdout);
			QWARN("reference render missing - bootstrap capture mode");
			return;
		}

		QFile ref{refPath};
		QVERIFY(ref.open(QIODevice::ReadOnly));
		const QByteArray expected = ref.readAll();
		ref.close();

		printEvidence("reference", expected);

		QCOMPARE(first.size(), expected.size());
		QVERIFY2(first == expected,
			"render is NOT byte-identical to the committed reference");
	}

private:
	void buildGraph()
	{
		auto mixer = Engine::mixer();
		while (mixer->numChannels() < kChannels)
		{
			mixer->createChannel();
		}

		// Faders: exercise the volume multiply with non-unit gains.
		mixer->mixerChannel(1)->m_volumeModel.setValue(1.25f);
		mixer->mixerChannel(2)->m_volumeModel.setValue(0.5f);
		mixer->mixerChannel(3)->m_volumeModel.setValue(2.0f);
		mixer->mixerChannel(4)->m_volumeModel.setValue(0.9f);
		mixer->mixerChannel(5)->m_volumeModel.setValue(1.0f);
		mixer->mixerChannel(6)->m_volumeModel.setValue(0.75f);
		mixer->mixerChannel(7)->m_volumeModel.setValue(1.1f);
		mixer->mixerChannel(8)->m_volumeModel.setValue(0.25f);

		// Send amounts. Every destination is a plain channel (or master):
		// no bus sends anywhere, which is exactly the D1 no-change domain.
		mixer->channelSendModel(1, 0)->setValue(0.8f);
		mixer->channelSendModel(2, 0)->setValue(1.0f);
		mixer->channelSendModel(3, 0)->setValue(0.25f);
		mixer->channelSendModel(4, 0)->setValue(0.6f);
		mixer->channelSendModel(5, 0)->setValue(1.0f);
		mixer->channelSendModel(7, 0)->setValue(0.45f);
		mixer->channelSendModel(8, 0)->setValue(0.15f);
		// Channel 6 additionally feeds channel 7, so channel 7 has an
		// incoming send and is scheduled by the dependency counter.
		mixer->createChannelSend(6, 7, 0.5f);

		// FX chains on three channels.
		for (int ch : {2, 5, 7})
		{
			auto* chain = &mixer->mixerChannel(ch)->m_fxChain;
			chain->appendEffect(new AbGainEffect(chain));
		}
	}

	QByteArray render()
	{
		auto mixer = Engine::mixer();
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();

		QByteArray out;
		out.reserve(kPeriods * static_cast<int>(fpp * sizeof(SampleFrame)));

		std::vector<SampleFrame> masterOut(fpp);
		std::vector<SampleFrame> input(fpp);
		SampleFrame* busData[1] = {input.data()};

		for (int p = 0; p < kPeriods; ++p)
		{
			for (int ch = 1; ch < kChannels; ++ch)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					input[f][0] = abSignal(p, f, ch, 0);
					input[f][1] = abSignal(p, f, ch, 1);
				}
				const AudioBus bus{busData, 1, fpp};
				mixer->mixToChannel(bus, ch);
			}

			mixer->prepareMasterMix();
			zeroSampleFrames(masterOut.data(), fpp);
			mixer->masterMix(masterOut.data());

			out.append(reinterpret_cast<const char*>(masterOut.data()),
				static_cast<int>(fpp * sizeof(SampleFrame)));
		}

		return out;
	}
};

QTEST_GUILESS_MAIN(MixerAbRegressionTest)
#include "MixerAbRegressionTest.moc"
