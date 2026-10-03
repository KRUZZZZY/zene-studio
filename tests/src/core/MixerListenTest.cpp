/*
 * MixerListenTest.cpp - the listen bus: AFL and PFL replace the mix on the output, and leave it alone
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

/*! MX-037 (0.5.0 L09): channel 1 carries a varying signal, channel 2 a constant 0.25, both at
 *  fader 0.5 into the master. Listening to channel 2 makes the output exactly its signal after the
 *  fader (AFL, 0.125) or before it (PFL, 0.25); stopping the listen gives back the mix bit for bit;
 *  deleting a channel clears the bus; the listened render allocates nothing. Driven through
 *  mixer.listen / mixer.listen_mode, as the menu drives it. */

#include <QtTest>

#include <vector>

#include "AllocationProbe.h"
#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
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

//! One period: channel 1 fed a varying signal, channel 2 a constant, the master output returned.
std::vector<SampleFrame> period(Mixer* mixer, int index)
{
	const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
	std::vector<SampleFrame> out(fpp);
	std::vector<SampleFrame> varying(fpp);
	std::vector<SampleFrame> constant(fpp, SampleFrame(0.25f, 0.25f));
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		const float v = static_cast<float>(((index * 7919 + f * 131) % 2048) - 1024) / 2048.f;
		varying[f] = SampleFrame(v, -v);
	}
	SampleFrame* one[1] = {varying.data()};
	SampleFrame* two[1] = {constant.data()};
	mixer->mixToChannel(AudioBus{one, 1, fpp}, 1);
	mixer->mixToChannel(AudioBus{two, 1, fpp}, 2);
	mixer->prepareMasterMix();
	zeroSampleFrames(out.data(), fpp);
	mixer->masterMix(out.data());
	AutomatableModel::incrementPeriodCounter();
	return out;
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

bool constantAt(const std::vector<SampleFrame>& out, float level)
{
	for (const SampleFrame& frame : out)
	{
		if (std::abs(frame[0] - level) > 1e-6f || std::abs(frame[1] - level) > 1e-6f) { return false; }
	}
	return true;
}

} // namespace

class MixerListenTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		Mixer* mixer = Engine::mixer();
		while (mixer->numChannels() <= 2) { mixer->createChannel(); }
		mixer->mixerChannel(1)->m_volumeModel.setValue(0.5f);
		mixer->mixerChannel(2)->m_volumeModel.setValue(0.5f);
		ControlRegistry::setReady(true);
		for (int i = 0; i < 3; ++i) { period(mixer, 0); }  // let the fader ramps settle
	}

	void cleanupTestCase()
	{
		mixerlisten::clear();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void afterAndBeforeTheFader()
	{
		Mixer* mixer = Engine::mixer();
		const QString two = control::channelIdOf(mixer->mixerChannel(2));
		const std::vector<SampleFrame> mix = period(mixer, 5);
		QVERIFY(run(QStringLiteral("mixer.listen"), {{QStringLiteral("channel"), two}}).ok);
		QVERIFY(constantAt(period(mixer, 5), 0.125f));  // AFL: 0.25 through the 0.5 fader
		QVERIFY(run(QStringLiteral("mixer.listen_mode"), {{QStringLiteral("mode"), QStringLiteral("pfl")}}).ok);
		QVERIFY(constantAt(period(mixer, 5), 0.25f));   // PFL: before the fader
		QVERIFY(run(QStringLiteral("mixer.listen"), {{QStringLiteral("channel"), two}, {QStringLiteral("on"), false}}).ok);
		QVERIFY2(identical(period(mixer, 5), mix), "stopping the listen does not give the mix back exactly");
		run(QStringLiteral("mixer.listen_mode"), {{QStringLiteral("mode"), QStringLiteral("afl")}});
	}

	void theListenedRenderAllocatesNothing()
	{
		Mixer* mixer = Engine::mixer();
		mixerlisten::setListening(2, true);
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(fpp);
		mixer->prepareMasterMix();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		mixer->masterMix(out.data());
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
		mixerlisten::clear();
	}

	void deletingAChannelClearsTheBus()
	{
		Mixer* mixer = Engine::mixer();
		const int added = mixer->createChannel();
		mixerlisten::setListening(1, true);
		QVERIFY(mixerlisten::anyListening());
		mixer->deleteChannel(added);
		QVERIFY(!mixerlisten::anyListening());
	}
};

QTEST_MAIN(MixerListenTest)
#include "MixerListenTest.moc"
