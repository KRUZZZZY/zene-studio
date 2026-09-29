/*
 * MixerChannelPanTest.cpp - the mixer channel pan (MixerChannel::m_panModel)
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

/*! A mixer channel had no pan (mixer.set_pan was a typed refusal; gate 16's ratchet
 *  listed it as a missing feature). The channel now carries m_panModel, a BALANCE law
 *  applied after its effects and VCA gain. What this file holds, on the real render
 *  path (Mixer::mixToChannel -> masterMix, the VcaGroupTest harness):
 *   * centred is not a stage: a channel panned and centred again renders byte-for-byte
 *     what it rendered before - which is also every project saved before pan existed;
 *   * hard left leaves the left side exactly as it was and the right side silent;
 *     half right halves the left side and leaves the right side exactly as it was;
 *   * the stage allocates nothing on the audio path (I8);
 *   * a centred pan writes nothing to the file, a set one round-trips, and a file
 *     without the attribute loads centred (it resets, so a journal restore can take a
 *     pan back off);
 *   * mixer.set_pan sets it and one control.undo takes it back.
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>

#include <cstdio>
#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

constexpr int kChannel = 1;

float sig(int period, int frame, int side)
{
	const int n = period * 7919 + frame * 131 + side * 4093;
	return static_cast<float>((n % 2048) - 1024) * (1.0f / 2048.0f);
}

//! `periods` periods of channel kChannel fed with `sig`, the master output interleaved.
std::vector<SampleFrame> render(Mixer* mixer, int periods)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out;
	std::vector<SampleFrame> masterOut(fpp);
	std::vector<SampleFrame> input(fpp);
	SampleFrame* busData[1] = {input.data()};
	for (int p = 0; p < periods; ++p)
	{
		for (f_cnt_t f = 0; f < fpp; ++f) { input[f] = SampleFrame(sig(p, f, 0), sig(p, f, 1)); }
		const AudioBus bus{busData, 1, fpp};
		mixer->mixToChannel(bus, static_cast<mix_ch_t>(kChannel));
		mixer->prepareMasterMix();
		zeroSampleFrames(masterOut.data(), fpp);
		mixer->masterMix(masterOut.data());
		out.insert(out.end(), masterOut.begin(), masterOut.end());
		// What the real engine does once per period (AudioEngine's render
		// stage): without the tick, a model's one-period interpolation ramp
		// after setValue() is handed out again on every period - measured: the
		// hard-left render replayed the first ramp samples forever.
		AutomatableModel::incrementPeriodCounter();
	}
	return out;
}

//! render() after one throwaway period: a pan change is SMOOTHED over one
//! period (the model's sample-exact ramp, so a move does not click), and the
//! steady state is what these tests measure.
std::vector<SampleFrame> settledRender(Mixer* mixer, int periods)
{
	render(mixer, 1);
	return render(mixer, periods);
}

bool identical(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i][0] != b[i][0] || a[i][1] != b[i][1]) { return false; }
	}
	return true;
}

FloatModel& pan() { return Engine::mixer()->mixerChannel(kChannel)->m_panModel; }

} // namespace


class MixerChannelPanTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= kChannel) { mixer->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		pan().setValue(0.0f);
		AutomatableModel::incrementPeriodCounter();
	}

	void centredIsNotAStage()
	{
		const std::vector<SampleFrame> before = settledRender(Engine::mixer(), 4);
		pan().setValue(0.5f);
		settledRender(Engine::mixer(), 2);
		pan().setValue(0.0f);
		const std::vector<SampleFrame> after = settledRender(Engine::mixer(), 4);
		QVERIFY2(identical(before, after), "a channel panned and centred again renders differently");
	}

	void hardLeftSilencesTheRightSideOnly()
	{
		const std::vector<SampleFrame> centre = settledRender(Engine::mixer(), 3);
		pan().setValue(-1.0f);
		const std::vector<SampleFrame> left = settledRender(Engine::mixer(), 3);
		QCOMPARE(left.size(), centre.size());
		double rightEnergy = 0.0;
		int leftDiffers = 0;
		for (std::size_t i = 0; i < left.size(); ++i)
		{
			rightEnergy += left[i][1] * left[i][1];
			leftDiffers += left[i][0] != centre[i][0];
		}
		std::printf("PAN_EVIDENCE hard left: right-side energy %.9g, left frames changed %d of %zu\n",
			rightEnergy, leftDiffers, left.size());
		QCOMPARE(rightEnergy, 0.0);
		QCOMPARE(leftDiffers, 0);
	}

	void halfRightHalvesTheLeftSideOnly()
	{
		const std::vector<SampleFrame> centre = settledRender(Engine::mixer(), 3);
		pan().setValue(0.5f);
		const std::vector<SampleFrame> right = settledRender(Engine::mixer(), 3);
		for (std::size_t i = 0; i < right.size(); ++i)
		{
			QCOMPARE(right[i][1], centre[i][1]);
			QCOMPARE(right[i][0], centre[i][0] * 0.5f);
		}
	}

	void thePanStageAllocatesNothing()
	{
		pan().setValue(-0.3f);
		settledRender(Engine::mixer(), 1);
		// render() allocates its own vectors, so the probe wraps only the
		// mixer's calls, with every buffer owned out here.
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> input(fpp, SampleFrame(0.25f, -0.25f));
		std::vector<SampleFrame> masterOut(fpp);
		SampleFrame* busData[1] = {input.data()};
		const AudioBus bus{busData, 1, fpp};
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		Engine::mixer()->mixToChannel(bus, static_cast<mix_ch_t>(kChannel));
		Engine::mixer()->prepareMasterMix();
		Engine::mixer()->masterMix(masterOut.data());
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<unsigned long long>(test::tlAllocationCount), 0ULL);
	}

	void aCentredPanWritesNothingAndASetOneRoundTrips()
	{
		QDomDocument document;
		QDomElement root = document.createElement(QStringLiteral("mixer"));
		document.appendChild(root);
		Engine::mixer()->saveSettings(document, root);
		QVERIFY2(document.toString().contains(QStringLiteral("volume=")),
			"the saved mixer is empty - this check would be vacuous");
		QVERIFY2(!document.toString().contains(QStringLiteral("pan=")), "a centred pan was written");

		pan().setValue(0.25f);
		QDomDocument panned;
		QDomElement pannedRoot = panned.createElement(QStringLiteral("mixer"));
		panned.appendChild(pannedRoot);
		Engine::mixer()->saveSettings(panned, pannedRoot);
		QVERIFY2(panned.toString().contains(QStringLiteral("pan=")), "a set pan was not written");

		pan().setValue(0.0f);
		Engine::mixer()->loadSettings(pannedRoot);
		QCOMPARE(pan().value(), 0.25f);
		// A file without the attribute - every file written before pan - loads centred.
		Engine::mixer()->loadSettings(root);
		QCOMPARE(pan().value(), 0.0f);
	}

	void theCommandSetsThePanAndUndoTakesItBack()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString channel = QStringLiteral("ch-%1").arg(Engine::mixer()->mixerChannel(kChannel)->id());
		const ControlResult set = registry->invoke(QStringLiteral("mixer.set_pan"),
			QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("pan"), -0.75}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(pan().value(), -0.75f);
		const ControlResult undone = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(pan().value(), 0.0f);
	}
};

QTEST_GUILESS_MAIN(MixerChannelPanTest)
#include "MixerChannelPanTest.moc"
